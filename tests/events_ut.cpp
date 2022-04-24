#include <iostream>
#include <memory>
#include <thread>
#include <algorithm>
#include "gtest/gtest.h"
#include "common/events_pi.h"

#define QU(x) #x
#define QUH(x) QU(x)

#define TEST_PATH "tcp://test/*.1234"

#define paths_json "\
{ " QUH(EVENTS_KEY) ": { \
  " QUH(XSUB_END_KEY) ": " QUH(TEST_PATH) ", \
  " QUH(REQ_REP_END_KEY) ": " QUH(TEST_PATH) " }}"

#define TMP_FILE "/tmp/test_events"

TEST(TestCommon, TestPaths) 
{
    {
        ofstream tmp(TMP_FILE);
        tmp << paths_json;
    }

    INIT_CFG = TMP_FILE;
    init_path();

    EXPECT_TRUE(strcmp(XSUB_PATH, TEST_PATH) == 0);
    EXPECT_TRUE(strcmp(XPUB_PATH, XPUB_END) == 0);
    EXPECT_TRUE(strcmp(REQ_REP_PATH, TEST_PATH) == 0);

    INIT_CFG = INIT_CFG_PATH;
    init_path();

    EXPECT_TRUE(strcmp(XSUB_PATH, XSUB_END) == 0);
    EXPECT_TRUE(strcmp(XPUB_PATH, XPUB_END) == 0);
    EXPECT_TRUE(strcmp(REQ_REP_PATH, REQ_REP_END) == 0);

}


TEST(TestCommon, TestIndex) 
{
    uint64_t index = 0, t;
    stringstream ss;

    EXPECT_EQ("1", get_index(index));

    EXPECT_EQ(1, index);

    index = INDEX_EPOCH_MASK;
    t = index + 1;
    ss << t;

    EXPECT_EQ(ss.str(), get_index(index));
    EXPECT_EQ(index, t);
}

vector<string> split_string(const string &s, const char delim)
{
    stringstream ss(s);
    string seg;
    vector<string> ret;

    while(getline(ss, seg, delim)) {
        ret.push_back(seg);
    }
    return ret;
}


TEST(TestCommon, TestTimestamp) 
{
    static const string l[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    string seg;
    vector<string> lst,lstT;

    lst = split_string(get_timestamp(), ' ');
    EXPECT_EQ(3, lst.size());

    bool found = false;
    for (const auto &mon : l) {
        if (mon == lst[0]) {
            found = true;
            break;
        }
    }
    EXPECT_EQ(true, found);

    int day = stoi(lst[1]);
    EXPECT_TRUE((day >= 1) && (day <= 31));

    lstT = split_string(lst[2], ':');
    EXPECT_EQ(3, lstT.size());

    int h = stoi(lstT[0]);
    EXPECT_TRUE((h >= 0) && (h < 24));

    int m = stoi(lstT[0]);
    EXPECT_TRUE((m >= 0) && (m < 60));

    lst = split_string(lstT[2], '.');
    EXPECT_EQ(2, lst.size());

    int s = stoi(lst[0]);
    EXPECT_TRUE((s >= 0) && (s < 60));

    int ms = stoi(lst[1]);
    EXPECT_TRUE((ms >= 0) && (ms < 999999));
}


TEST(TestCommon, TestSerialize)
{
    map<string, string> m = { 
        { "foo", "bar" },
        { "ts", "Apr  5 17:52:44.472026" },
        { "index", "32432432343" },
        { "source", "bgp" },
        { "tag",  "admin_down"},
        { "ip", "10.10.10.10" } };

    zmq_msg_t msg;
    map_to_zmsg(m, msg);

    map<string, string> m1;
    zmsg_to_map(msg, m1);

    EXPECT_EQ(m, m1);
}

TEST(TestEvent, TestPublisher)
{
    const char *sender = "test_sender";
    const char *source = "test_source";

    event_handle_t h = events_init_publisher(sender, source);

    EventPublisher *pub = dynamic_cast<EventPublisher *>(h);

    EXPECT_TRUE(NULL != pub->m_zmq_ctx);
    EXPECT_TRUE(NULL != pub->m_socket);
    EXPECT_TRUE(NULL != pub->m_req_socket);

    EXPECT_EQ(2, pub->m_metadata.size());

    EXPECT_EQ(pub->m_metadata[EVENT_METADATA_TAG_SOURCE], string(source));
    EXPECT_EQ(pub->m_metadata[EVENT_METADATA_TAG_SENDER], string(sender));

    EXPECT_EQ(pub->m_metadata[EVENT_METADATA_TAG_SOURCE].size(), 
            zmq_msg_size(&pub->m_zmsg_source));

    EXPECT_EQ(0, memcmp(zmq_msg_data(&pub->m_zmsg_source), source, strlen(source)));

}


TEST(TestEvent, TestSubscriber)
{
    event_handle_t h = events_init_subscriber();

    EventSubscriber *sub = dynamic_cast<EventSubscriber *>(h);

    EXPECT_TRUE(NULL != sub->m_zmq_ctx);
    EXPECT_TRUE(NULL != sub->m_socket);

    EXPECT_EQ(0, sub->m_missed_cnt);

}

