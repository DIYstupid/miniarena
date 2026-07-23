#include <gtest/gtest.h>
#include "game/match_manager.h"
#include "game/room_manager.h"
#include "game/session_manager.h"
#include "storage/mysql_client.h"
#include "storage/redis_client.h"

using namespace miniarena;

class MatchManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(mysql_.connect("127.0.0.1", 3306, "miniarena", "miniarena", "miniarena"));
        ASSERT_TRUE(redis_.connect("127.0.0.1", 6379));
        mysql_.ensureSchema();
        mysql_.execute("DELETE FROM players WHERE username LIKE 'mm_test_%'");
        mysql_.execute("DELETE FROM login_records");

        sessions_ = std::make_unique<SessionManager>(&mysql_, &redis_);
        rooms_    = std::make_unique<RoomManager>();
        matcher_  = std::make_unique<MatchManager>(rooms_.get(), sessions_.get(), &redis_, 2);

        // Clean match queue
        while (redis_.matchQueueSize(0) > 0) redis_.popMatchQueue(0);

        notify_count_ = 0;
        matcher_->setNotifyCallback([this](PlayerId, uint32_t msg_id,
                                            const std::string&) {
            if (msg_id == 2004) ++notify_count_;  // MatchSuccessNotify
        });
    }

    // Login helper
    uint64_t loginPlayer(ConnectionId conn, const std::string& name) {
        auto r = sessions_->login(conn, name, name);  // password = username
        EXPECT_EQ(r.error_code, 0);
        sessions_->setState(r.session_id, SessionState::IN_LOBBY);
        return r.player_id;
    }

    MysqlClient mysql_;
    RedisClient redis_;
    std::unique_ptr<SessionManager> sessions_;
    std::unique_ptr<RoomManager> rooms_;
    std::unique_ptr<MatchManager> matcher_;
    int notify_count_ = 0;
};

TEST_F(MatchManagerTest, JoinQueueIncreasesSize) {
    auto pid = loginPlayer(1, "mm_test_a");
    EXPECT_EQ(matcher_->joinQueue(pid, 0), 0);
    EXPECT_GE(redis_.matchQueueSize(0), 1);
}

TEST_F(MatchManagerTest, NoMatchWhenInsufficientPlayers) {
    auto pid = loginPlayer(1, "mm_test_x");
    matcher_->joinQueue(pid, 0);
    RoomId rid = matcher_->tryMatch(0);
    EXPECT_EQ(rid, 0);  // Only 1 player, need 2
    EXPECT_EQ(notify_count_, 0);
}

TEST_F(MatchManagerTest, MatchCreatesRoomWhenEnoughPlayers) {
    auto p1 = loginPlayer(10, "mm_test_p1");
    auto p2 = loginPlayer(20, "mm_test_p2");

    matcher_->joinQueue(p1, 0);
    matcher_->joinQueue(p2, 0);
    EXPECT_GE(redis_.matchQueueSize(0), 2);

    RoomId rid = matcher_->tryMatch(0);
    EXPECT_NE(rid, 0);
    EXPECT_EQ(notify_count_, 2);

    auto* room = rooms_->getRoom(rid);
    ASSERT_NE(room, nullptr);
    EXPECT_EQ(room->playerCount(), 2);
}
