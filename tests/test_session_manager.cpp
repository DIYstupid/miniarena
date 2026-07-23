#include "messages.pb.h"
#include <gtest/gtest.h>
#include "game/session_manager.h"
#include "storage/mysql_client.h"
#include "storage/redis_client.h"

using namespace miniarena;

class SessionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(mysql_.connect("127.0.0.1", 3306, "miniarena", "miniarena", "miniarena"));
        ASSERT_TRUE(redis_.connect("127.0.0.1", 6379));
        mysql_.ensureSchema();

        // Clean test data
        mysql_.execute("DELETE FROM login_records");
        mysql_.execute("DELETE FROM battle_records");
        mysql_.execute("DELETE FROM players WHERE username LIKE 'sm_test_%'");

        mgr_ = std::make_unique<SessionManager>(&mysql_, &redis_);

        sent_msgs_.clear();
        mgr_->setSendCallback([this](ConnectionId cid, uint32_t msg_id,
                                      const std::string& payload) {
            sent_msgs_.push_back({cid, msg_id, payload});
        });
    }

    struct SentMsg {
        ConnectionId conn_id;
        uint32_t msg_id;
        std::string payload;
    };

    MysqlClient mysql_;
    RedisClient redis_;
    std::unique_ptr<SessionManager> mgr_;
    std::vector<SentMsg> sent_msgs_;
};

TEST_F(SessionManagerTest, LoginCreatesSession) {
    auto result = mgr_->login(100, "sm_test_alice", "pass1");
    EXPECT_EQ(result.error_code, 0);
    EXPECT_NE(result.session_id, 0);
    EXPECT_NE(result.player_id, 0);
}

TEST_F(SessionManagerTest, LoginTwiceKicksOldConnection) {
    // First login
    auto r1 = mgr_->login(100, "sm_test_bob", "pass2");
    EXPECT_EQ(r1.error_code, 0);

    // Second login from different connection
    auto r2 = mgr_->login(200, "sm_test_bob", "pass2");
    EXPECT_EQ(r2.error_code, 0);
    EXPECT_EQ(r2.player_id, r1.player_id);
    EXPECT_NE(r2.session_id, r1.session_id);

    // Old connection should receive kick notification
    bool kicked = false;
    for (auto& m : sent_msgs_) {
        if (m.conn_id == 100 && m.msg_id == 1002) {
            // LoginResponse with error_code 20003
            miniarena::LoginResponse resp;
            resp.ParseFromString(m.payload);
            if (resp.error_code() == 20003) kicked = true;
        }
    }
    EXPECT_TRUE(kicked);
}

TEST_F(SessionManagerTest, GetByConnFindsSession) {
    auto r = mgr_->login(300, "sm_test_charlie", "p3");
    EXPECT_EQ(r.error_code, 0);

    auto* s = mgr_->getByConn(300);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->player_id, r.player_id);
    EXPECT_EQ(s->username, "sm_test_charlie");
}

TEST_F(SessionManagerTest, GetByConnReturnsNullForUnknown) {
    EXPECT_EQ(mgr_->getByConn(9999), nullptr);
}

TEST_F(SessionManagerTest, LogoutRemovesSession) {
    auto r = mgr_->login(400, "sm_test_dave", "p4");
    EXPECT_EQ(r.error_code, 0);

    mgr_->logout(r.session_id);
    EXPECT_EQ(mgr_->getByConn(400), nullptr);
    EXPECT_EQ(mgr_->getBySession(r.session_id), nullptr);
}

TEST_F(SessionManagerTest, StateTransition) {
    auto r = mgr_->login(500, "sm_test_eve", "p5");
    EXPECT_EQ(r.error_code, 0);

    auto* s = mgr_->getBySession(r.session_id);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->state, SessionState::AUTHENTICATED);

    EXPECT_TRUE(mgr_->setState(r.session_id, SessionState::IN_LOBBY));
    EXPECT_EQ(s->state, SessionState::IN_LOBBY);

    EXPECT_TRUE(mgr_->setState(r.session_id, SessionState::MATCHING));
    EXPECT_EQ(s->state, SessionState::MATCHING);
}

TEST_F(SessionManagerTest, MarkDisconnectedAndReconnect) {
    auto r = mgr_->login(600, "sm_test_frank", "p6");
    EXPECT_EQ(r.error_code, 0);
    mgr_->setState(r.session_id, SessionState::IN_LOBBY);

    // Simulate disconnect
    mgr_->markDisconnected(600);
    auto* s = mgr_->getBySession(r.session_id);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->state, SessionState::DISCONNECTED);

    // Reconnect from new connection
    auto r2 = mgr_->tryReconnect(r.session_id, 601);
    EXPECT_EQ(r2.error_code, 0);
    EXPECT_EQ(r2.player_id, r.player_id);

    auto* s2 = mgr_->getByConn(601);
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(s2->state, SessionState::IN_LOBBY);
}
