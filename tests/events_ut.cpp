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
    init_path();

    EXPECT_EQ(XSUB_PATH, XSUB_END);
    EXPECT_EQ(XPUB_PATH, XPUB_END);
    EXPECT_EQ(REQ_REP_PATH, REQ_REP_END);

    {
        ofstream tmp(TMP_FILE);
        tmp << paths_json;
    }

    INIT_CFG = TMP_FILE;
    init_path();

    EXPECT_EQ(XSUB_PATH, TEST_PATH);
    EXPECT_EQ(XPUB_PATH, XPUB_END);
    EXPECT_EQ(REQ_REP_PATH, TEST_PATH);

}










