#include <fstream>
#include string.h
#include json.hpp
#include zmq.h

#include logger.h

using namespace std;


#define XSUB_END "tcp://*:5570"
#define XPUB_END "tcp://*:5571"
#define REQ_REP_END "tcp://*:5572"

#define INIT_CFG "/etc/sonic/init_cfg.json"
#define EVENTS_KEY  "events"
#define XSUB_END_KEY "xsub_path"
#define XPUB_END_KEY "xpub_path"
#define REQ_REP_END_KEY "req_rep_path"

#define REQ_SEND "hello"

string xsub_path;
string xpub_path;
string req_rep_path;

typedef uint64_t index_data_t;

#define INDEX_EPOCH_BITS 0xFFFF
#define INDEX_VAL_BITS 48
#define INDEX_EPOCH_MASK ((INDEX_EPOCH_BITS) << (INDEX_VAL_BITS))

#define ERR_CHECK(res, fmt, ...) \
    if (!res) {\
        stringstream _err_ss; \
        _err_ss << __FUNCTION__ << ":" << __LINE__ << " " << fmt; \
        SWSS_LOG_ERROR(_err_ss.str().c_str(), __VA_ARGS__);


string &get_path(auto &data, const char *key, string &def)
{
    auto it = data.find(key);
    if (it != data.end())
        return *it;
    else
        return def;
}

void init_path()
{
    if (!xsub_path.empty()) {
        return;
    }

    xsub_path = XSUB_END;
    xpub_path = XPUB_END;
    req_rep_path = REQ_REP_END;

    ifstream fs (INIT_CFG);

    if (!fs.is_open())
        return;

    stringstream buffer;
    buffer << fs.rdbuf();

    auto &data = nlohmann::json::parse(buffer.str());

    auto it = data.find(EVENTS_KEY);
    if (it == data.end())
        return;

    data = *it;

    xsub_path = get_path(data, XSUB_END_KEY, xsub_path);
    xpub_path = get_path(data, XPUB_END_KEY, xpub_path);
    req_rep_path = get_path(data, REQ_REP_END_KEY, req_rep_path);

    return;
}


uin64_t get_epoch()
{
    auto timepoint = system_clock::now();
    return duration_cast<microseconds>(timepoint.time_since_epoch()).count();
}

string get_index(index_data_t &index)
{
    stringstream s;
    index_data_t prefix = index & INDEX_EPOCH_MASK;
    index_data_t val = index & ~(INDEX_EPOCH_MASK);
    val += 1;
    val &= ~(INDEX_EPOCH_MASK);
    index = prefix | val;

    s << index;
    return s.str();
}


const string& get_timestamp()
{
    std::stringstream s, sfrac;

    auto timepoint = system_clock::now();
    std::time_t tt = system_clock::to_time_t (timepoint);
    struct std::tm * ptm = std::localtime(&tt);

    uint64_t ms = duration_cast<microseconds>(timepoint.time_since_epoch()).count();
    uint64_t sec = duration_cast<seconds>(timepoint.time_since_epoch()).count();
    uint64_t mfrac = ms - (sec * 1000 * 1000);

    sfrac << mfrac;

    s << std::put_time(&tm, "%b %e %H:%M:%S.") << sfrac.str().substr(0, 6);
    return s.str();
}


template <typename Map>
const string serialize(const Map& data)
{
    std::stringstream ss;
    boost::archive::text_oarchive oarch(ss);
    oarch << data;
    return ss.str();
}

template <typename Map>
void deserialize(const string& s, Map& data)
{
    std::stringstream ss;
    boost::archive::text_iarchive iarch(ss);
    iarch >> data;
    return;
}


template <typename Map>
void map_to_zmsg(const Map& data, zmq_msg_t &msg)
{
    string s = serialize(data);

    zmq_msg_init_size(&msg, s.size());
    strncpy((char *)zmq_msg_data(&msg), s.c_str(), s.size());
}


template <typename Map>
void zmsg_to_map(const zmq_msg_t &msg, Map& data)
{
    string s((const char *)zmq_msg_data(&msg), zmq_msg_size(&msg));
    deserialize(s, data);
}



/*
 * A single instance per sender & source.
 * A sender is very likely to use single source only.
 * There can be multiple sender processes/threads for a single source.
 *
 */

class EventPublisher
{
    index_data_t m_index;

    void *m_zmq_ctx;
    void *m_socket;

    // REQ socket is connected and a message is sent & received, more to 
    // ensure PUB socket had enough time to establish connection.
    // Any message published before connection establishment is dropped.
    //
    void *m_req_socket;

    zmq_msg_t m_zmsg_source;
    event_metadata_t m_metadata;


    public:
        EventPublisher(const char *client_name, const char *event_source)
        {
            init_path();

            m_zmq_ctx = zmq_ctx_new();
            m_socket = zmq_socket (m_zmq_ctx, ZMQ_PUB);
            m_req_socket = zmq_socket (m_zmq_ctx, ZMQ_REQ);

            rc = zmq_connect (m_socket, xsub_path);
            ERR_CHECK(rc == 0, "Publisher fails to connect %s", xsub_path);


            rc = zmq_connect (m_req_socket, req_rep_path);
            ERR_CHECK (rc == 0, "Failed to connect %s", req_rep_path);

            {
                // Send a dummy message
                //
                zmq_msg_t snd_msg;
                rc = zmq_msg_init_size(&snd_msg, 10);
                ERR_CHECK(rc == 0, "Failed to init zmq msg for 10 bytes");
                memcpy((void *)zmq_msg_data(&snd_msg), REQ_SEND, strlen(REQ_SEND));
                rc = zmq_msg_send(&snd_msg, m_req_socket, 0);
                ERR_CHECK (rc != -1, "Failed to send to %s", req_rep_path);
            }

            zmq_msg_init_size(&m_zmsg_source, strlen(event_source));
            strncpy((char *)zmq_msg_data(&m_zmsg_source), event_source, strlen(event_source));

            m_metadata[EVENT_METADATA_TAG_SENDER] = client_name;
            m_metadata[EVENT_METADATA_TAG_SOURCE] = event_source;

            m_index = (get_epoch() & INDEX_EPOCH_BITS) << INDEX_VAL_BITS;
        }

        ~EventPublisher()
        {
            zmq_msg_close(&m_zmsg_source);
            zmq_close(m_socket);
            zmq_ctx_term(m_zmq_ctx);
        }


        void event_publish(const char *tag, const event_params_t *params,
                const char *timestamp = NULL)
        {
            msg_part2_flag = 0;

            /* Make struct & serialize */
            m_metadata[EVENT_METADATA_TAG_TAG] = tag;
            m_metadata[EVENT_METADATA_TAG_TIMESTAMP] = (timestamp != NULL ?
                    timestamp : get_timestamp());
            m_metadata[EVENT_METADATA_INDEX] = get_index(m_index);

            zmq_msg_t msg_metadata;
            map_to_zmsg(m_metadata, msg_metadata);

            zmq_msg_t msg_params;
            bool has_params = false;

            if ((params != NULL) && (params->size() != 0)) {
                msg_part2_flag = ZMQ_SNDMORE;
                map_to_zmsg(*params, msg_params);
            }

            // Receive reply if not done
            if (m_req_socket != NULL) {
                zmq_msg_t rcv_msg;

                zmq_msg_init(&rcv_msg);
                rc = zmq_msg_recv(&rcv_msg, req, 0);
                ERR_CHECK (rc != -1, "Failed to receive message from req-rep %s", req_rep_path);

                zmq_msg_close(&rcv_msg);
                zmq_close(m_req_socket);
                m_req_socket = NULL;
            }

            // Send the message
            // First part -- The event-source/pattern
            // Second part -- Metadata
            // Third part -- Params, if given
            //
            ERR_CHECK(zmq_msg_send (&m_zmsg_source, m_socket, ZMQ_SNDMORE) != -1,
                    "Failed to publish part 1 to %s", xsub_path);
            ERR_CHECK(zmq_msg_send (&msg_metadata, m_socket, msg_part2_flag) != -1,
                    "Failed to publish part 2 to %s", xsub_path);
            if (msg_part2_flag != 0) {
                ERR_CHECK(zmq_msg_send(&msg_params, m_socket, 0) != -1,
                        "Failed to publish part 3 to %s", xsub_path);
                zmq_msg_close(&msg_params);
            }
            zmq_msg_close(&msg_metadata);

        }
}


event_handle_t
events_init_publisher(const char *event_sender, const char *event_source)
{
    return new EventPublisher(event_sender, event_source);
}

void
event_publish(event_handle_t handle, const char *tag, const event_params_t *params,
        const char *timestamp)
{
    EventPublisher *pub = dynamic_cast<EventPublisher *>(handle);

    pub->event_publish(tag, params, timestamp);
}

/*
 *  Receiver's instance to receive.
 *
 *  An instance could be created as one per consumer, so a slow customer
 *  would have nearly no impact on others.
 *
 */

class EventSubscriber
{
    void *m_zmq_ctx;
    void *m_socket;

    // Indices are tracked per sendefr & source.
    //
    map<string, index_data_t> m_indices;

    // Cumulative missed count across all subscribed sources.
    //
    uint64_t m_missed_cnt;


    public:
        EventSubscriber(event_subscribe_sources_t *subs_sources):
            m_missed_cnt(0)
        {
            init_path();

            m_zmq_ctx = zmq_ctx_new();
            m_socket = zmq_socket (m_zmq_ctx, ZMQ_SUB);

            rc = zmq_connect (m_socket, xpub_path);
            ERR_CHECK(rc == 0, "Publisher fails to connect %s", xpub_path);

            if subs_sources {
                for (auto & pat : *subs_sources) {
                    zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, pat.c_str(), strlen(pat.c_str()));
                }
            }
            if ((subs_sources == NULL) || (subs_sources->empty())) {
                zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);
            }
        }

        ~EventSubscriber()
        {
            zmq_close(m_socket);
            zmq_ctx_term(m_zmq_ctx);
        }


        template <typename Map>
        bool do_receive(Map *data)
        {
            zmq_msg_t msg;
            int more = 0;
            size_t more_size = sizeof (more);

            zmq_msg_init(&msg);
            ERR_CHECK (zmq_msg_recv(&msg, m_socket, 0) != -1,
                    "Failed to receive from %s", xpub_path);

            if (data != NULL) {
                zmsg_to_map(&msg, *data);
            }
            zmq_msg_close(&msg);
            ERR_CHECK (zmq_getsockopt (subscriber, ZMQ_RCVMORE, &more, &more_size) == 0,
                    "Failed to get sockopt for %s", xpub_path);

            return more != 0;

        }

        template <typename Map>
        void validate_meta(meta)
        {
            static std::string keys = { \
                EVENT_METADATA_TAG_SENDER, \
                EVENT_METADATA_TAG_SOURCE, \
                EVENT_METADATA_TAG_TAG, \
                EVENT_METADATA_TAG_TIMESTAMP, \
                EVENT_METADATA_INDEX }
            for (const auto key : keys) {
                if (meta.find(key) == meta.end()") {
                    ERR_CHECK(false, "missing key %s", key);
                    return false;
                }
            }
            return true;
        }


        index_data_t update_missed(meta)
        {
            stringstream key;
            index_data_t missed = 0;

            key << meta[EVENT_METADATA_TAG_SENDER] << ":" << meta[EVENT_METADATA_TAG_SOURCE];

            index_data_t index = stoull(meta[EVENT_METADATA_INDEX]);
            index_data_t last_index = m_indices[key.str()];

            if ((index & INDEX_EPOCH_MASK) == (last_index & INDEX_EPOCH_MASK)) {
                // Same session
                index_data_t val = index & (~INDEX_EPOCH_MASK);
                index_data_t last_val = last_index & (~INDEX_EPOCH_MASK);

                if (val >= last_val) {
                    // +1 is expected as next message index increment.
                    missed = val - last_val - 1;
                }
                // less implies a sender restart 
            }

            // Save current
            m_indices[key.str()] = index;

            if ((missed >> 16) != 0) {
                // Diff is more than 64K; This is not possible;
                // Likely a sender resart; Ignore
                missed = 0;
            }
            m_missed_cnt += missed;
            return missed;
        }

        void event_receive(event_metadata_t &metadata, event_params_t &params,
                unsigned long *missed_count = NULL)
        {

            do {
                event_params_t().swap(params);
                event_metadata_t().swap(metadata);

                // Receive part 1 -- pattern/event-source
                // The event_source is part of metadata in part 2.
                // Hence ignore
                //
                ERR_CHECK(do_receive(NULL), "Expect atleast 1 more part (metadata) %s", xpub_path);

                // Receive metadata
                //
                if(do_receive(&metadata)) {
                    // Receive params
                    //
                    ERR_CHECK(!do_receive(&params), "Don't expect any more part %s", xpub_path);
                }
            } while (!validate_meta(metadata))

            update_missed(metadata);
            if (missed_count != NULL) *missed_count = m_missed_cnt;
        }
}


/* Public APIs */

event_handle_t
events_init_subscriber(const lst_subscribe_sources)
{
    return new EventSubscriber(lst_subscribe_sources);
}

void
event_receive(event_handle_t handle, event_metadata_t &metadata,
        event_params_t &params, unsigned long *missed_count)
{
    EventSubscriber *sub = dynamic_cast<EventSubscriber>(handle);

    sub->event_receive(metadata, params, missed_count);
}





