#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include "player.h"

namespace miniarena {

// Combat attributes for a player inside a battle room.
struct BattlePlayer {
    PlayerId id = 0;
    std::string username;

    // Position & direction
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float dir_x = 0.0f;
    float dir_y = 1.0f;

    // Stats
    int32_t hp         = 1000;
    int32_t max_hp     = 1000;
    int32_t atk        = 100;
    int32_t def        = 50;
    float   move_speed = 200.0f;  // pixels/sec

    bool alive = true;

    // Skill cooldowns: skill_id → remaining cooldown (ms)
    std::unordered_map<int32_t, int32_t> cooldowns;

    // Battle statistics
    int32_t kills        = 0;
    int32_t deaths       = 0;
    int32_t damage_dealt = 0;
    int32_t damage_taken = 0;
};

}  // namespace miniarena
