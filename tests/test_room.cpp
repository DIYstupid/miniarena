#include <gtest/gtest.h>
#include "game/room.h"
#include "game/room_manager.h"

using namespace miniarena;

TEST(RoomTest, InitialStateIsCreated) {
    Room r(1, 10);
    EXPECT_EQ(r.id(), 1);
    EXPECT_EQ(r.state(), RoomState::CREATED);
    EXPECT_EQ(r.playerCount(), 0);
    EXPECT_FALSE(r.isFull());
}

TEST(RoomTest, AddPlayerTransitionsToWaiting) {
    Room r(1, 10);
    int err = r.addPlayer(100, "alice");
    EXPECT_EQ(err, 0);
    EXPECT_EQ(r.state(), RoomState::WAITING_PLAYERS);
    EXPECT_EQ(r.playerCount(), 1);
}

TEST(RoomTest, RoomFullRejectsExtraPlayer) {
    Room r(1, 2);
    EXPECT_EQ(r.addPlayer(1, "a"), 0);
    EXPECT_EQ(r.addPlayer(2, "b"), 0);
    EXPECT_TRUE(r.isFull());
    int err = r.addPlayer(3, "c");
    EXPECT_EQ(err, 40002);
}

TEST(RoomTest, PlayerReadyAndAllReady) {
    Room r(1, 2);
    r.addPlayer(1, "a");
    r.addPlayer(2, "b");

    EXPECT_FALSE(r.allReady());
    r.playerReady(1);
    EXPECT_FALSE(r.allReady());
    r.playerReady(2);
    EXPECT_TRUE(r.allReady());
}

TEST(RoomTest, RemovePlayer) {
    Room r(1, 10);
    r.addPlayer(1, "a");
    r.addPlayer(2, "b");
    r.playerReady(1);
    EXPECT_EQ(r.playerCount(), 2);
    r.removePlayer(1);
    EXPECT_EQ(r.playerCount(), 1);
    EXPECT_FALSE(r.allReady());
    EXPECT_TRUE(r.hasPlayer(2));
    EXPECT_FALSE(r.hasPlayer(1));
}

TEST(RoomTest, StateTransitions) {
    Room r(1, 2);
    r.addPlayer(1, "a");
    r.addPlayer(2, "b");
    EXPECT_EQ(r.state(), RoomState::WAITING_PLAYERS);

    r.startLoading();
    EXPECT_EQ(r.state(), RoomState::LOADING);

    // Not ready yet, can't start battle
    r.startBattle();
    EXPECT_EQ(r.state(), RoomState::LOADING);

    r.playerReady(1);
    r.playerReady(2);
    r.startBattle();
    EXPECT_EQ(r.state(), RoomState::RUNNING);

    r.endBattle();
    EXPECT_EQ(r.state(), RoomState::SETTLING);

    r.destroy();
    EXPECT_EQ(r.state(), RoomState::DESTROYED);
}

TEST(RoomManagerTest, CreateAndGetRoom) {
    RoomManager mgr;
    RoomId id = mgr.createRoom(10);
    EXPECT_GT(id, 0);

    auto* r = mgr.getRoom(id);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->id(), id);
    EXPECT_EQ(r->maxPlayers(), 10);
}

TEST(RoomManagerTest, GetNonexistentRoom) {
    RoomManager mgr;
    EXPECT_EQ(mgr.getRoom(999), nullptr);
}

TEST(RoomManagerTest, DestroyRoom) {
    RoomManager mgr;
    RoomId id = mgr.createRoom(5);
    EXPECT_EQ(mgr.roomCount(), 1);

    mgr.destroyRoom(id);
    EXPECT_EQ(mgr.roomCount(), 0);
    EXPECT_EQ(mgr.getRoom(id), nullptr);
}
