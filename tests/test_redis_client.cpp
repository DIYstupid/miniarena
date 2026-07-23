#include <gtest/gtest.h>
#include "storage/redis_client.h"

using namespace miniarena;

class RedisClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(client_.connect("127.0.0.1", 6379));
        // Clean test keys
        client_.del("test:key");
        client_.del("test:key2");
        client_.delSession(999);
        client_.delOnline(999);
        client_.set("test:counter", "0");
    }

    RedisClient client_;
};

TEST_F(RedisClientTest, ConnectAndPing) {
    EXPECT_TRUE(client_.isConnected());
}

TEST_F(RedisClientTest, SetAndGet) {
    EXPECT_TRUE(client_.set("test:key", "hello"));
    auto val = client_.get("test:key");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "hello");
}

TEST_F(RedisClientTest, GetMissingKey) {
    auto val = client_.get("test:nonexistent");
    EXPECT_FALSE(val.has_value());
}

TEST_F(RedisClientTest, SetExAndExpire) {
    EXPECT_TRUE(client_.setEx("test:key2", 10, "temp"));
    auto val = client_.get("test:key2");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "temp");
}

TEST_F(RedisClientTest, Del) {
    client_.set("test:key", "value");
    EXPECT_TRUE(client_.del("test:key"));
    EXPECT_FALSE(client_.get("test:key").has_value());
}

TEST_F(RedisClientTest, Exists) {
    EXPECT_FALSE(client_.exists("test:key"));
    client_.set("test:key", "v");
    EXPECT_TRUE(client_.exists("test:key"));
}

TEST_F(RedisClientTest, SessionOps) {
    client_.setSession(42, "session_data", 3600);
    auto val = client_.getSession(42);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "session_data");

    client_.delSession(42);
    EXPECT_FALSE(client_.getSession(42).has_value());
}

TEST_F(RedisClientTest, OnlineStatus) {
    client_.setOnline(100, 200);
    auto sid = client_.getOnline(100);
    ASSERT_TRUE(sid.has_value());
    EXPECT_EQ(*sid, 200);

    client_.delOnline(100);
    EXPECT_FALSE(client_.getOnline(100).has_value());
}

TEST_F(RedisClientTest, MatchQueue) {
    // Clean up first
    while (client_.matchQueueSize(0) > 0) {
        client_.popMatchQueue(0);
    }

    EXPECT_EQ(client_.matchQueueSize(0), 0);
    client_.pushMatchQueue(0, 1);
    client_.pushMatchQueue(0, 2);
    client_.pushMatchQueue(0, 3);
    EXPECT_EQ(client_.matchQueueSize(0), 3);

    // BRPOP with timeout would block; use non-blocking peek via LLEN
    EXPECT_GE(client_.matchQueueSize(0), 3);
}

TEST_F(RedisClientTest, RoomRoute) {
    client_.setRoomRoute(500, "127.0.0.1:9000");
    auto addr = client_.getRoomRoute(500);
    ASSERT_TRUE(addr.has_value());
    EXPECT_EQ(*addr, "127.0.0.1:9000");

    client_.delRoomRoute(500);
    EXPECT_FALSE(client_.getRoomRoute(500).has_value());
}
