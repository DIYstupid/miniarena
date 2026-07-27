#include <gtest/gtest.h>
#include "game/tick_engine.h"
#include "game/broadcaster.h"
#include "game/aoi_grid.h"

using namespace miniarena;

class TickEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup two players
        BattlePlayer p1;
        p1.id = 1; p1.username = "alice";
        p1.pos_x = 0; p1.pos_y = 0;
        players_[1] = p1;

        BattlePlayer p2;
        p2.id = 2; p2.username = "bob";
        p2.pos_x = 50; p2.pos_y = 0;
        players_[2] = p2;

        bc_.setSendCallback([](PlayerId, uint32_t, const std::string&) {});
    }

    std::unordered_map<PlayerId, BattlePlayer> players_;
    TickEngine engine_;
    Broadcaster bc_;
    AoiGrid aoi_;
};

TEST_F(TickEngineTest, MoveChangesPosition) {
    Command cmd;
    cmd.type = CommandType::MOVE;
    cmd.player_id = 1;
    cmd.sequence = 1;
    cmd.move_dir_x = 1.0f;
    cmd.move_dir_y = 0.0f;

    float old_x = players_[1].pos_x;
    engine_.processTick(players_, {cmd}, &bc_, &aoi_);

    // 200 px/s * 0.05s = 10 pixels
    EXPECT_GT(players_[1].pos_x, old_x);
    EXPECT_FLOAT_EQ(players_[1].dir_x, 1.0f);
    EXPECT_FLOAT_EQ(players_[1].dir_y, 0.0f);
}

TEST_F(TickEngineTest, StopMoveZeroesDirection) {
    players_[1].dir_x = 0.5f;
    players_[1].dir_y = 0.5f;

    Command cmd;
    cmd.type = CommandType::STOP_MOVE;
    cmd.player_id = 1;
    cmd.sequence = 1;

    engine_.processTick(players_, {cmd}, &bc_, &aoi_);
    EXPECT_FLOAT_EQ(players_[1].dir_x, 0.0f);
    EXPECT_FLOAT_EQ(players_[1].dir_y, 0.0f);
}

TEST_F(TickEngineTest, AttackReducesHp) {
    Command cmd;
    cmd.type = CommandType::ATTACK;
    cmd.player_id = 1;
    cmd.target_id = 2;
    cmd.sequence = 1;

    int32_t old_hp = players_[2].hp;
    engine_.processTick(players_, {cmd}, &bc_, &aoi_);

    EXPECT_LT(players_[2].hp, old_hp);
    EXPECT_GT(players_[1].damage_dealt, 0);
    EXPECT_EQ(players_[2].damage_taken, players_[1].damage_dealt);
}

TEST_F(TickEngineTest, AttackKillsTarget) {
    players_[2].hp = 1;  // Almost dead
    players_[1].atk = 10000;  // Overkill

    Command cmd;
    cmd.type = CommandType::ATTACK;
    cmd.player_id = 1;
    cmd.target_id = 2;
    cmd.sequence = 1;

    engine_.processTick(players_, {cmd}, &bc_, &aoi_);
    EXPECT_FALSE(players_[2].alive);
    EXPECT_EQ(players_[2].hp, 0);
    EXPECT_EQ(players_[1].kills, 1);
    EXPECT_EQ(players_[2].deaths, 1);
}

TEST_F(TickEngineTest, SkillOnCooldownIgnored) {
    Command cmd;
    cmd.type = CommandType::SKILL;
    cmd.player_id = 1;
    cmd.target_id = 2;
    cmd.skill_id = 1;
    cmd.sequence = 1;

    // First use sets cooldown
    engine_.processTick(players_, {cmd}, &bc_, &aoi_);
    int32_t hp_after_first = players_[2].hp;

    // Second use should be ignored (3000ms cooldown)
    cmd.sequence = 2;
    engine_.processTick(players_, {cmd}, &bc_, &aoi_);
    EXPECT_EQ(players_[2].hp, hp_after_first);
}

TEST_F(TickEngineTest, SequenceDedup) {
    Command cmd;
    cmd.type = CommandType::MOVE;
    cmd.player_id = 1;
    cmd.sequence = 42;
    cmd.move_dir_x = 1.0f;
    cmd.move_dir_y = 0.0f;

    engine_.processTick(players_, {cmd}, &bc_, &aoi_);
    float pos_after_first = players_[1].pos_x;

    // Same sequence again → should be ignored
    engine_.processTick(players_, {cmd}, &bc_, &aoi_);
    EXPECT_FLOAT_EQ(players_[1].pos_x, pos_after_first);

    // Older sequence → also ignored
    cmd.sequence = 41;
    engine_.processTick(players_, {cmd}, &bc_, &aoi_);
    EXPECT_FLOAT_EQ(players_[1].pos_x, pos_after_first);
}

TEST_F(TickEngineTest, DeadPlayerCannotAct) {
    players_[1].alive = false;

    Command cmd;
    cmd.type = CommandType::ATTACK;
    cmd.player_id = 1;
    cmd.target_id = 2;
    cmd.sequence = 1;

    int32_t old_hp = players_[2].hp;
    engine_.processTick(players_, {cmd}, &bc_, &aoi_);
    EXPECT_EQ(players_[2].hp, old_hp);
}

TEST_F(TickEngineTest, CooldownDecreasesOverTicks) {
    Command cmd;
    cmd.type = CommandType::SKILL;
    cmd.player_id = 1;
    cmd.skill_id = 1;
    cmd.sequence = 1;

    engine_.processTick(players_, {cmd}, &bc_, &aoi_);
    EXPECT_TRUE(players_[1].cooldowns.count(1) > 0);

    // Run many ticks to clear cooldown
    for (int i = 0; i < 80; ++i) {  // 80 * 50ms = 4000ms > 3000ms cooldown
        engine_.processTick(players_, {}, &bc_, &aoi_);
    }
    EXPECT_EQ(players_[1].cooldowns.count(1), 0);
}
