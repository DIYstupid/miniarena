#include <gtest/gtest.h>
#include "network/timer_wheel.h"

using namespace miniarena;

TEST(TimerWheelTest, StartsEmpty) {
    TimerWheel tw;
    EXPECT_EQ(tw.size(), 0);
}

TEST(TimerWheelTest, AddAndFire) {
    TimerWheel tw;
    bool fired = false;
    tw.add(1, 10, [&](ConnectionId id) {
        fired = true;
        EXPECT_EQ(id, 1);
    });

    EXPECT_EQ(tw.size(), 1);

    // Tick 1: 10ms delay → should fire at slot 1 (10ms / 10ms tick = 1)
    tw.tick();  // advance to slot 1
    EXPECT_EQ(tw.size(), 0);
    EXPECT_TRUE(fired);
}

TEST(TimerWheelTest, DoesNotFirePrematurely) {
    TimerWheel tw;
    bool fired = false;
    tw.add(1, 50, [&](ConnectionId) { fired = true; });

    EXPECT_EQ(tw.size(), 1);

    // Tick 4 times (40ms) — should NOT fire yet (50ms delay)
    for (int i = 0; i < 4; ++i) {
        tw.tick();
    }
    EXPECT_EQ(tw.size(), 1);
    EXPECT_FALSE(fired);
}

TEST(TimerWheelTest, FiresAfterDelay) {
    TimerWheel tw;
    bool fired = false;
    tw.add(1, 50, [&](ConnectionId) { fired = true; });

    // Tick 5 times (50ms)
    for (int i = 0; i < 5; ++i) {
        tw.tick();
    }
    EXPECT_EQ(tw.size(), 0);
    EXPECT_TRUE(fired);
}

TEST(TimerWheelTest, RemovePreventsFire) {
    TimerWheel tw;
    bool fired = false;
    tw.add(1, 10, [&](ConnectionId) { fired = true; });
    tw.remove(1);
    EXPECT_EQ(tw.size(), 0);

    tw.tick();
    EXPECT_FALSE(fired);
}

TEST(TimerWheelTest, RemoveUnknownIsSafe) {
    TimerWheel tw;
    tw.remove(999);  // should not crash
    EXPECT_EQ(tw.size(), 0);
}

TEST(TimerWheelTest, RefreshExtendsTimeout) {
    TimerWheel tw;
    bool fired = false;
    tw.add(1, 30, [&](ConnectionId) { fired = true; });

    // Tick 2 (20ms), then refresh to 30ms from now
    tw.tick();
    tw.tick();
    tw.refresh(1, 30);

    // Tick 2 more (20ms from refresh = 40ms total from start, but only 20ms from refresh)
    tw.tick();
    tw.tick();
    EXPECT_FALSE(fired);

    // One more tick should fire
    tw.tick();
    EXPECT_TRUE(fired);
}

TEST(TimerWheelTest, MultipleTimers) {
    TimerWheel tw;
    int count = 0;
    tw.add(1, 10, [&](ConnectionId) { ++count; });
    tw.add(2, 20, [&](ConnectionId) { ++count; });
    tw.add(3, 30, [&](ConnectionId) { ++count; });
    EXPECT_EQ(tw.size(), 3);

    tw.tick();  // 10ms: id=1 fires
    EXPECT_EQ(count, 1);
    EXPECT_EQ(tw.size(), 2);

    tw.tick();  // 20ms: id=2 fires
    EXPECT_EQ(count, 2);
    EXPECT_EQ(tw.size(), 1);

    tw.tick();  // 30ms: id=3 fires
    EXPECT_EQ(count, 3);
    EXPECT_EQ(tw.size(), 0);
}

TEST(TimerWheelTest, WrapAround) {
    TimerWheel tw;
    bool fired = false;

    // Current slot is 0. Set a timer for exactly kSlots ticks (6s).
    // That wraps around to slot 0 again after full rotation.
    uint64_t wrap_ms = TimerWheel::kSlots * TimerWheel::kTickMs;
    tw.add(1, wrap_ms, [&](ConnectionId) { fired = true; });

    // Tick kSlots-1 times — should NOT fire yet
    for (size_t i = 0; i < TimerWheel::kSlots - 1; ++i) {
        tw.tick();
    }
    EXPECT_FALSE(fired);
    EXPECT_EQ(tw.size(), 1);

    // One more tick completes the wrap
    tw.tick();
    EXPECT_TRUE(fired);
    EXPECT_EQ(tw.size(), 0);
}

TEST(TimerWheelTest, ReplaceExistingTimer) {
    TimerWheel tw;
    int count1 = 0, count2 = 0;
    tw.add(1, 10, [&](ConnectionId) { ++count1; });
    tw.add(1, 50, [&](ConnectionId) { ++count2; });  // replaces

    EXPECT_EQ(tw.size(), 1);

    tw.tick();  // 10ms: original timer would fire, but replaced → no fire
    EXPECT_EQ(count1, 0);
    EXPECT_EQ(count2, 0);

    // Tick to 50ms
    for (int i = 0; i < 4; ++i) tw.tick();
    EXPECT_EQ(count2, 1);
}
