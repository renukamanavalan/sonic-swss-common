/*
 * Private header file
 * Required to run white box testing via unit tests.
 */
#include <fstream>
#include string.h
#include json.hpp
#include zmq.h

#include logger.h

using namespace std;


#define XSUB_END "tcp://*:5570"
#define XPUB_END "tcp://*:5571"
#define REQ_REP_END "tcp://*:5572"

#define INIT_CFG_PATH "/etc/sonic/init_cfg.json"
extern const *INIT_CFG;

#define EVENTS_KEY  "events"
#define XSUB_END_KEY "xsub_path"
#define XPUB_END_KEY "xpub_path"
#define REQ_REP_END_KEY "req_rep_path"

#define REQ_SEND "hello"

extern string xsub_path;
extern string xpub_path;
extern string req_rep_path;

typedef uint64_t index_data_t;

#define INDEX_EPOCH_BITS 0xFFFF
#define INDEX_VAL_BITS 48
#define INDEX_EPOCH_MASK ((INDEX_EPOCH_BITS) << (INDEX_VAL_BITS))

#define ERR_CHECK(res, fmt, ...) \
    if (!res) {\
        stringstream _err_ss; \
        _err_ss << __FUNCTION__ << ":" << __LINE__ << " " << fmt; \
        SWSS_LOG_ERROR(_err_ss.str().c_str(), __VA_ARGS__);


string &get_path(auto &data, const char *key, string &def);

void init_path();

uin64_t get_epoch();

string get_index(index_data_t &index);

const string& get_timestamp();

template <typename Map> const string serialize(const Map& data);

template <typename Map> void deserialize(const string& s, Map& data);

template <typename Map> void map_to_zmsg(const Map& data, zmq_msg_t &msg);

template <typename Map> void zmsg_to_map(const zmq_msg_t &msg, Map& data);

/*
 * A single instance per sender & source.
 * A sender is very likely to use single source only.
 * There can be multiple sender processes/threads for a single source.
 *
 */

class EventPublisher {
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
        EventPublisher(const char *client_name, const char *event_source);

        ~EventPublisher();

        void event_publish(const char *tag, const event_params_t *params,
                const char *timestamp = NULL);
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
    public:

        void *m_zmq_ctx;
        void *m_socket;

        // Indices are tracked per sendefr & source.
        //
        map<string, index_data_t> m_indices;

        // Cumulative missed count across all subscribed sources.
        //
        uint64_t m_missed_cnt;

        EventSubscriber(event_subscribe_sources_t *subs_sources):
            m_missed_cnt(0);

        ~EventSubscriber();

        template <typename Map> bool do_receive(Map *data);

        template <typename Map> void validate_meta(meta);

        index_data_t update_missed(meta);

        void event_receive(event_metadata_t &metadata, event_params_t &params,
                unsigned long *missed_count = NULL);
}

