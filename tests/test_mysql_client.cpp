#include <gtest/gtest.h>
#include "storage/mysql_client.h"

using namespace miniarena;

class MysqlClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(client_.connect("127.0.0.1", 3306, "miniarena", "miniarena", "miniarena"));
        client_.ensureSchema();
        // Clean test data
        client_.execute("DELETE FROM battle_records");
        client_.execute("DELETE FROM login_records");
        client_.execute("DELETE FROM players WHERE username LIKE 'test_%'");
    }

    void TearDown() override {
        client_.close();
    }

    MysqlClient client_;
};

TEST_F(MysqlClientTest, ConnectAndSchemaInit) {
    EXPECT_TRUE(client_.isConnected());
    // ensureSchema should not throw
    client_.ensureSchema();
}

TEST_F(MysqlClientTest, CreateAndGetPlayer) {
    uint64_t id = client_.createPlayer("test_alice", "hash123");
    EXPECT_GT(id, 0);

    auto player = client_.getPlayer("test_alice");
    ASSERT_TRUE(player.has_value());
    EXPECT_EQ(player->username, "test_alice");
    EXPECT_EQ(player->password_hash, "hash123");
    EXPECT_EQ(player->rating, 1000);
}

TEST_F(MysqlClientTest, GetNonexistentPlayer) {
    auto player = client_.getPlayer("test_nobody");
    EXPECT_FALSE(player.has_value());
}

TEST_F(MysqlClientTest, RecordLogin) {
    uint64_t id = client_.createPlayer("test_bob", "h2");
    EXPECT_GT(id, 0);

    client_.recordLogin(id);
    // Verify via raw query
    EXPECT_TRUE(client_.execute(
        "SELECT * FROM login_records WHERE player_id=" + std::to_string(id)));
}

TEST_F(MysqlClientTest, SaveBattleResult) {
    uint64_t pid = client_.createPlayer("test_charlie", "h3");
    EXPECT_GT(pid, 0);

    client_.saveBattleResult(pid, 100, 5, 2, 5000, 3000, 1);
    EXPECT_TRUE(client_.execute(
        "SELECT * FROM battle_records WHERE player_id=" + std::to_string(pid)));
}
