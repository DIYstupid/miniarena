#include <gtest/gtest.h>
#include "game/broadcaster.h"

using namespace miniarena;

TEST(BroadcasterTest, QueueAndFlushOverride) {
    Broadcaster bc;
    int count = 0;
    PlayerId last_pid = 0;
    std::string last_payload;

    bc.setSendCallback([&](PlayerId pid, uint32_t, const std::string& payload) {
        ++count;
        last_pid = pid;
        last_payload = payload;
    });

    bc.queueOverride(1, 4004, "data_v1");
    bc.queueOverride(1, 4004, "data_v2");  // should overwrite
    bc.queueOverride(2, 4004, "data_p2");

    bc.flushOverrides();
    EXPECT_EQ(count, 2);  // player 1 once + player 2
}

TEST(BroadcasterTest, BroadcastToAoi) {
    Broadcaster bc;
    std::vector<PlayerId> received;

    bc.setSendCallback([&](PlayerId pid, uint32_t, const std::string&) {
        received.push_back(pid);
    });

    bc.broadcastToAoi({10, 20, 30}, 4004, "payload");
    EXPECT_EQ(received.size(), 3);
    EXPECT_EQ(received[0], 10);
    EXPECT_EQ(received[1], 20);
    EXPECT_EQ(received[2], 30);
}

TEST(BroadcasterTest, BroadcastToRoom) {
    Broadcaster bc;
    bool called = false;
    bc.setSendCallback([&](PlayerId pid, uint32_t msg_id, const std::string&) {
        called = true;
        EXPECT_EQ(pid, 0);  // broadcast marker
        EXPECT_EQ(msg_id, 4005);
    });

    bc.broadcastToRoom(4005, "death");
    EXPECT_TRUE(called);
}
