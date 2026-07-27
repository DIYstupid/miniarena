#include <gtest/gtest.h>
#include <thread>
#include "game/command.h"

using namespace miniarena;

TEST(CommandQueueTest, PushAndDrain) {
    CommandQueue q;
    EXPECT_EQ(q.size(), 0);

    Command cmd;
    cmd.type = CommandType::MOVE;
    cmd.player_id = 1;
    cmd.move_dir_x = 1.0f;
    q.push(cmd);
    EXPECT_EQ(q.size(), 1);

    auto cmds = q.drain();
    EXPECT_EQ(q.size(), 0);
    ASSERT_EQ(cmds.size(), 1);
    EXPECT_EQ(cmds[0].type, CommandType::MOVE);
    EXPECT_EQ(cmds[0].player_id, 1);
    EXPECT_FLOAT_EQ(cmds[0].move_dir_x, 1.0f);
}

TEST(CommandQueueTest, DrainClearsAll) {
    CommandQueue q;
    for (int i = 0; i < 10; ++i) {
        Command cmd;
        cmd.player_id = i;
        q.push(cmd);
    }
    auto cmds = q.drain();
    EXPECT_EQ(cmds.size(), 10);
    EXPECT_EQ(q.size(), 0);

    // Second drain is empty
    auto cmds2 = q.drain();
    EXPECT_TRUE(cmds2.empty());
}

TEST(CommandQueueTest, ConcurrentPushAndDrain) {
    CommandQueue q;
    std::atomic<int> count{0};

    // Producer threads
    std::thread p1([&]() {
        for (int i = 0; i < 1000; ++i) {
            Command cmd;
            cmd.player_id = i;
            q.push(cmd);
        }
    });
    std::thread p2([&]() {
        for (int i = 1000; i < 2000; ++i) {
            Command cmd;
            cmd.player_id = i;
            q.push(cmd);
        }
    });

    p1.join();
    p2.join();

    // Drain
    auto cmds = q.drain();
    EXPECT_EQ(cmds.size(), 2000);
}
