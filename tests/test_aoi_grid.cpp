#include <gtest/gtest.h>
#include "game/aoi_grid.h"

using namespace miniarena;

TEST(AoiGridTest, SinglePlayer) {
    AoiGrid grid;
    grid.update(1, 100, 200);
    EXPECT_EQ(grid.playerCount(), 1);
}

TEST(AoiGridTest, SameCell) {
    AoiGrid grid;
    grid.update(1, 0, 0);
    grid.update(2, 50, 50);  // same cell (0,0)

    auto nearby = grid.getNearby(1);
    EXPECT_EQ(nearby.size(), 2);

    grid.remove(2);
    EXPECT_EQ(grid.playerCount(), 1);
}

TEST(AoiGridTest, AdjacentCells) {
    AoiGrid grid;
    grid.update(1, 0, 0);       // cell (0,0)
    grid.update(2, 199, 0);     // cell (0,0) — same cell
    grid.update(3, 201, 0);     // cell (1,0) — adjacent
    grid.update(4, 500, 0);     // cell (2,0) — not adjacent to (0,0)

    auto nearby = grid.getNearby(1);
    // Should include 1,2,3 but not 4
    EXPECT_GE(nearby.size(), 2);
    // Verify player 4 is NOT in nearby
    bool has4 = false;
    for (auto id : nearby) if (id == 4) has4 = true;
    EXPECT_FALSE(has4);
}

TEST(AoiGridTest, MoveAcrossCells) {
    AoiGrid grid;
    grid.update(1, 0, 0);           // cell (0,0)
    grid.update(1, 250, 0);         // cell (1,0)
    // Should not crash and player count unchanged
    EXPECT_EQ(grid.playerCount(), 1);
}

TEST(AoiGridTest, RemoveCleansCell) {
    AoiGrid grid;
    grid.update(1, 0, 0);
    grid.remove(1);
    EXPECT_EQ(grid.playerCount(), 0);

    auto nearby = grid.getNearby(1);
    EXPECT_TRUE(nearby.empty());
}
