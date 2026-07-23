#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace miniarena {

using PlayerId = uint64_t;

// Persistent player record (from MySQL).
struct PlayerRecord {
    uint64_t    id       = 0;
    std::string username;
    std::string password_hash;
    int32_t     rating      = 1000;
    int32_t     total_games = 0;
    int32_t     wins        = 0;
};

// Runtime player info inside a room/battle.
struct PlayerInfo {
    uint64_t    player_id = 0;
    std::string username;
    bool        ready = false;
};

}  // namespace miniarena
