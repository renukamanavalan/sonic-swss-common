/*
 * Private header file
 * Required to run white box testing via unit tests.
 */
#include <chrono>
#include <fstream>
#include "string.h"
#include "json.hpp"
#include "zmq.h"

#include "logger.h"
#include "events.h"

using namespace std;
using namespace chrono;


#define XSUB_END "tcp://*:5570"
#define XPUB_END "tcp://*:5571"
#define REQ_REP_END "tcp://*:5572"

#define INIT_CFG_PATH "/etc/sonic/init_cfg.json"
extern const char *INIT_CFG;

#define EVENTS_KEY  "events"
#define XSUB_END_KEY "xsub_path"
#define XPUB_END_KEY "xpub_path"
#define REQ_REP_END_KEY "req_rep_path"

#define REQ_SEND "hello"

typedef uint64_t index_data_t;

#define INDEX_EPOCH_BITS 0xFFFFll
#define INDEX_VAL_BITS 48
#define INDEX_EPOCH_MASK ((INDEX_EPOCH_BITS) << (INDEX_VAL_BITS))

#define ERR_CHECK(res, ...) {\
    if (!(res)) \
        SWSS_LOG_ERROR(__VA_ARGS__); }


typedef map<const char *, string> events_json_data_t;
extern events_json_data_t tx_paths;

#define XSUB_PATH tx_paths[XSUB_END_KEY].c_str()
#define XPUB_PATH tx_paths[XPUB_END_KEY].c_str()
#define REQ_REP_PATH tx_paths[REQ_REP_END_KEY].c_str()

void init_path();

uint64_t get_epoch();

string get_index(index_data_t &index);

const string get_timestamp();

template <typename Map> const string serialize(const Map& data);

template <typename Map> void deserialize(const string& s, Map& data);

template <typename Map> void map_to_zmsg(const Map& data, zmq_msg_t &msg);

template <typename Map> void zmsg_to_map(zmq_msg_t &msg, Map& data);

/*
 * A single instance per sender & source.
 * A sender is very likely to use single source only.
 * There can be multiple sender processes/threads for a single source.
 *
 */

class EventPublisher : public events_base {
    public:
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


        EventPublisher(const char *client_name, const char *event_source);

        virtual ~EventPublisher();

        void event_publish(const char *tag, const event_params_t *params,
                const char *timestamp = NULL);
};

/*
 *  Receiver's instance to receive.
 *
 *  An instance could be created as one per consumer, so a slow customer
 *  would have nearly no impact on others.
 *
 */

class EventSubscriber : public events_base
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

        EventSubscriber(const event_subscribe_sources_t *subs_sources);

        virtual ~EventSubscriber();

        template <typename Map> bool do_receive(Map *data);

        template <typename Map> bool validate_meta(const Map &meta);

        template <typename Map>
        index_data_t update_missed(Map &meta);

        void event_receive(event_metadata_t &metadata, event_params_t &params,
                unsigned long *missed_count = NULL);
};

