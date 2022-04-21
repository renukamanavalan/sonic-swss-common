/*
 * Events library 
 *
 *  APIs are for publishing & receiving events with source, tag and params along with timestamp.
 *
 */


class events_base
{
    public:
        virtual ~events_base() = default;
};

typedef events_base* event_handle_t;

/*
 * One event publisher per source per caller
 * Multiple calls for same source will return the same instance
 *
 * NOTE: The init is done asynchronously; an event published before
 *       init completes will take longer. Hence it is suggested to call
 *       this init API as soon as the process starts.
 *
 * Event source is mandatory
 *
 * Return 
 *  Non NULL handle
 *  NULL on failure
 */

event_handle_t events_init_publisher(const char *event_sender, const char *event_source);

typedef std::map<std::string, std::string> event_params_t;

/*
 * Publish an event
 *
 * input:
 *  handle -- As obtained from events_init_publisher
 *  tag -- Event tag
 *  params -- Params associated with event, if any
 *  timestamp -- Timestamp for the event; optional; 
 *              format:Apr 20 00:00:35.358636
 *                     Apr  5 17:52:42.568940
 *
 */
void event_publish(event_handle_t handle, const char *tag, const event_params_t *params=NULL,
        const char *timestamp=NULL);


typedef std::vector<std::string> event_subscribe_sources_t;

/*
 * Initialize subscriber.
 *
 * Input:
 *  lst_subscribe_sources_t
 *      List of subscription sources of interest.
 *      Absence implies for alll
 *
 * Return:
 *  Non NULL handle on success
 *  NULL on failure
 */
event_handle_t events_init_subscriber(const event_subscribe_sources_t *sources=NULL);


/*
 * Every event is associated with 
 *  sender, source, tag & timestamp
 *
 * event_metadata_t is a map of tag vs value.
 * The tags are listed below.
 */
typedef std::map<std::string, std::string> event_metadata_t;

#define EVENT_METADATA_TAG_SENDER "sender"
#define EVENT_METADATA_TAG_SOURCE "source"
#define EVENT_METADATA_TAG_TAG "tag"
#define EVENT_METADATA_TAG_TIMESTAMP "timestamp"
#define EVENT_METADATA_INDEX "index"

/*
 * Revieve an event
 *
 * input:
 *  handle -- As obtained from events_init_subscriber
 *
 * output:
 *  metadata -- Event's metadata.
 *  params -- Params associated with event, if any
 *  missed_count -- Cumulative count of missed messages across all
 *                  sources of this subscription.
 *
 */
void event_receive(event_handle_t handle, event_metadata_t &metadata,
        event_params_t &params, unsigned long *missed_count = NULL);


