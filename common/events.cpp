#include "events_pi.h"
#include <boost/serialization/map.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

const char *INIT_CFG = INIT_CFG_PATH;

// Final set of paths
//
events_json_data_t tx_paths_default = {
    events_json_data_t::value_type(XSUB_END_KEY, XSUB_END),
    events_json_data_t::value_type(XPUB_END_KEY, XPUB_END),
    events_json_data_t::value_type(REQ_REP_END_KEY, REQ_REP_END) };

events_json_data_t tx_paths;


void init_path()
{
    ifstream fs (INIT_CFG);

    // Set defaults
    //
    events_json_data_t(tx_paths_default).swap(tx_paths);

    if (!fs.is_open())
        return;

    stringstream buffer;
    buffer << fs.rdbuf();

    const auto &data = nlohmann::json::parse(buffer.str());

    const auto it = data.find(EVENTS_KEY);
    if (it == data.end())
        return;

    const auto edata = *it;
    for (events_json_data_t::iterator itJ = tx_paths.begin();
            itJ != tx_paths.end(); ++itJ) {
        auto itE = edata.find(itJ->first);
        if (itE != edata.end()) {
            itJ->second = *itE;
        }
    }

    /*
    for (const auto itE = it->second.begin(); itE != it->second.end(); ++itE) {
        for (events_json_data_t::iterator itJ = tx_paths.begin();
                itJ != tx_paths.end(); ++itJ) {
            if (itE->first == itJ->first) {
                itJ->second = itE->second;
            }
        }
    }
    */

    return;
}


uint64_t get_epoch()
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


const string get_timestamp()
{
    std::stringstream ss, sfrac;

    auto timepoint = system_clock::now();
    std::time_t tt = system_clock::to_time_t (timepoint);
    struct std::tm * ptm = std::localtime(&tt);

    uint64_t ms = duration_cast<microseconds>(timepoint.time_since_epoch()).count();
    uint64_t sec = duration_cast<seconds>(timepoint.time_since_epoch()).count();
    uint64_t mfrac = ms - (sec * 1000 * 1000);

    sfrac << mfrac;

    ss << put_time(ptm, "%b %e %H:%M:%S.") << sfrac.str().substr(0, 6);
    return ss.str();
}


const string serialize(const map_str_str_t& data)
{
    std::stringstream ss;
    boost::archive::text_oarchive oarch(ss);
    oarch << data;
    return ss.str();
}

void deserialize(const string& s, map_str_str_t& data)
{
    std::stringstream ss;
    ss << s;
    boost::archive::text_iarchive iarch(ss);
    iarch >> data;
    return;
}


void map_to_zmsg(const map_str_str_t& data, zmq_msg_t &msg)
{
    string s = serialize(data);

    zmq_msg_init_size(&msg, s.size());
    strncpy((char *)zmq_msg_data(&msg), s.c_str(), s.size());
}


void zmsg_to_map(zmq_msg_t &msg, map_str_str_t& data)
{
    string s((const char *)zmq_msg_data(&msg), zmq_msg_size(&msg));
    deserialize(s, data);
}



EventPublisher::EventPublisher(const char *client_name, const char *event_source)
{
    init_path();

    m_zmq_ctx = zmq_ctx_new();
    m_socket = zmq_socket (m_zmq_ctx, ZMQ_PUB);
    m_req_socket = zmq_socket (m_zmq_ctx, ZMQ_REQ);

    int rc = zmq_connect (m_socket, XSUB_PATH);
    ERR_CHECK(rc == 0, "Publisher fails to connect %s", XSUB_PATH);

    rc = zmq_connect (m_req_socket, REQ_REP_PATH);
    ERR_CHECK (rc == 0, "Failed to connect %s", REQ_REP_PATH);

    {
        // Send a dummy message
        //
        zmq_msg_t snd_msg;
        rc = zmq_msg_init_size(&snd_msg, 10);
        ERR_CHECK(rc == 0, "Failed to init zmq msg for 10 bytes");
        memcpy((void *)zmq_msg_data(&snd_msg), REQ_SEND, strlen(REQ_SEND));
        rc = zmq_msg_send(&snd_msg, m_req_socket, 0);
        ERR_CHECK (rc != -1, "Failed to send to %s", REQ_REP_PATH);
    }

    zmq_msg_init_size(&m_zmsg_source, strlen(event_source));

    memcpy((char *)zmq_msg_data(&m_zmsg_source), event_source, strlen(event_source));

    m_metadata[EVENT_METADATA_TAG_SENDER] = client_name;
    m_metadata[EVENT_METADATA_TAG_SOURCE] = event_source;

    m_index = (get_epoch() & INDEX_EPOCH_BITS) << INDEX_VAL_BITS;
}

EventPublisher::~EventPublisher()
{
    zmq_msg_close(&m_zmsg_source);
    zmq_close(m_socket);
    zmq_ctx_term(m_zmq_ctx);
}


void
EventPublisher::event_publish(const char *tag, const event_params_t *params,
        const char *timestamp)
{
    int msg_part2_flag = 0;

    /* Make struct & serialize */
    m_metadata[EVENT_METADATA_TAG_TAG] = tag;
    m_metadata[EVENT_METADATA_TAG_TIMESTAMP] = (timestamp != NULL ?
            timestamp : get_timestamp());
    m_metadata[EVENT_METADATA_INDEX] = get_index(m_index);

    zmq_msg_t msg_metadata;
    map_to_zmsg(m_metadata, msg_metadata);

    zmq_msg_t msg_params;

    if ((params != NULL) && (params->size() != 0)) {
        msg_part2_flag = ZMQ_SNDMORE;
        map_to_zmsg(*params, msg_params);
    }

    // Receive reply if not done
    if (m_req_socket != NULL) {
        zmq_msg_t rcv_msg;

        zmq_msg_init(&rcv_msg);
        ERR_CHECK (zmq_msg_recv(&rcv_msg, m_req_socket, 0) != -1,
                "Failed to receive message from req-rep %s", REQ_REP_PATH);

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
            "Failed to publish part 1 to %s", XSUB_PATH);
    ERR_CHECK(zmq_msg_send (&msg_metadata, m_socket, msg_part2_flag) != -1,
            "Failed to publish part 2 to %s", XSUB_PATH);
    if (msg_part2_flag != 0) {
        ERR_CHECK(zmq_msg_send(&msg_params, m_socket, 0) != -1,
                "Failed to publish part 3 to %s", XSUB_PATH);
        zmq_msg_close(&msg_params);
    }
    zmq_msg_close(&msg_metadata);

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

EventSubscriber::EventSubscriber(const event_subscribe_sources_t *subs_sources)
{
    init_path();

    m_zmq_ctx = zmq_ctx_new();
    m_socket = zmq_socket (m_zmq_ctx, ZMQ_SUB);

    ERR_CHECK (zmq_connect (m_socket, XPUB_PATH) == 0,
            "Publisher fails to connect %s", XPUB_PATH);

    if (subs_sources != NULL) {
        for (const auto & pat : *subs_sources) {
            ERR_CHECK (zmq_setsockopt(m_socket, ZMQ_SUBSCRIBE,
                        pat.c_str(), strlen(pat.c_str())) == 0,
                    "Failing to set subscribe filter %s", pat.c_str());
        }
    }
    if ((subs_sources == NULL) || (subs_sources->empty())) {
        ERR_CHECK (zmq_setsockopt(m_socket, ZMQ_SUBSCRIBE, "", 0) == 0,
                "Failing to set subscribe filter for all");
    }
    m_missed_cnt = 0;
}

EventSubscriber::~EventSubscriber()
{
    zmq_close(m_socket);
    zmq_ctx_term(m_zmq_ctx);
}


bool
EventSubscriber::do_receive(map_str_str_t *data)
{
    zmq_msg_t msg;
    int more = 0;
    size_t more_size = sizeof (more);

    zmq_msg_init(&msg);
    ERR_CHECK (zmq_msg_recv(&msg, m_socket, 0) != -1,
            "Failed to receive from %s", XPUB_PATH);

    if (data != NULL) {
        zmsg_to_map(msg, *data);
    }
    zmq_msg_close(&msg);
    ERR_CHECK (zmq_getsockopt (m_socket, ZMQ_RCVMORE, &more, &more_size) == 0,
            "Failed to get sockopt for %s", XPUB_PATH);

    return more != 0;

}

bool
EventSubscriber::validate_meta(const map_str_str_t &meta)
{
    static string keys[] = { \
        EVENT_METADATA_TAG_SENDER, \
        EVENT_METADATA_TAG_SOURCE, \
        EVENT_METADATA_TAG_TAG, \
        EVENT_METADATA_TAG_TIMESTAMP, \
        EVENT_METADATA_INDEX };

    for (const auto key : keys) {
        if (meta.find(key) == meta.end()) {
            ERR_CHECK(false, "missing key %s", key.c_str());
            return false;
        }
    }
    return true;
}


index_data_t
EventSubscriber::update_missed(map_str_str_t &meta)
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

void
EventSubscriber::event_receive(event_metadata_t &metadata, event_params_t &params,
        unsigned long *missed_count)
{

    do {
        event_params_t().swap(params);
        event_metadata_t().swap(metadata);

        // Receive part 1 -- pattern/event-source
        // The event_source is part of metadata in part 2.
        // Hence ignore
        //
        bool more = do_receive((event_metadata_t *)NULL);
        ERR_CHECK(more, "Expect atleast 1 more part (metadata) %s", XPUB_PATH);

        // Receive metadata
        //
        if(do_receive(&metadata)) {
            // Receive params
            //
            ERR_CHECK(!do_receive(&params), "Don't expect any more part %s", XPUB_PATH);
        }
    } while (!validate_meta(metadata));

    update_missed(metadata);
    if (missed_count != NULL) *missed_count = m_missed_cnt;
}


/* Public APIs */

event_handle_t
events_init_subscriber(const event_subscribe_sources_t *sources)
{
    return new EventSubscriber(sources);
}

void
event_receive(event_handle_t handle, event_metadata_t &metadata,
        event_params_t &params, unsigned long *missed_count)
{
    EventSubscriber *sub = dynamic_cast<EventSubscriber *>(handle);

    sub->event_receive(metadata, params, missed_count);
}





