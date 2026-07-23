#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include "player.h"

namespace miniarena {

using RoomId = uint64_t;

enum class RoomState {
    CREATED,
    WAITING_PLAYERS,
    LOADING,
    RUNNING,
    SETTLING,
    DESTROYED,
};

struct RoomPlayerInfo {
    PlayerId    player_id = 0;
    std::string username;
    bool        ready = false;
};

class Room {
public:
    Room(RoomId id, int max_players);

    // --- Player management ---
    // Returns 0 on success, or error code (40002=full, 40003=wrong state).
    int addPlayer(PlayerId player_id, const std::string& username);
    void removePlayer(PlayerId player_id);
    void playerReady(PlayerId player_id);
    [[nodiscard]] bool allReady() const;

    // --- State transitions ---
    void startLoading();
    void startBattle();  // P4
    void endBattle();    // P4
    void destroy();

    // --- Queries ---
    [[nodiscard]] RoomId id()         const noexcept { return room_id_; }
    [[nodiscard]] RoomState state()   const noexcept { return state_; }
    [[nodiscard]] int playerCount()   const noexcept { return static_cast<int>(players_.size()); }
    [[nodiscard]] int maxPlayers()    const noexcept { return max_players_; }
    [[nodiscard]] bool isFull()       const noexcept { return playerCount() >= max_players_; }
    [[nodiscard]] bool hasPlayer(PlayerId pid) const;
    [[nodiscard]] const auto& players() const { return players_; }

private:
    RoomId room_id_;
    RoomState state_ = RoomState::CREATED;
    int max_players_;
    int ready_count_ = 0;
    std::unordered_map<PlayerId, RoomPlayerInfo> players_;
};

}  // namespace miniarena
