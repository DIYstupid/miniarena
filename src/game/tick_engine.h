#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include "player.h"
#include "battle_player.h"
#include "command.h"

namespace miniarena {

class Broadcaster;
class AoiGrid;
class Room;

// Predefined skill definitions
struct SkillDef {
    int32_t id;
    const char* name;
    int32_t cooldown_ms;
    float   damage_ratio;
    float   range;
    bool    aoe;
    float   aoe_radius;
};

extern const SkillDef kSkills[3];

// Core tick processing: 20 Hz battle logic.
// Pure logic — no I/O, no threads. Testable in isolation.
class TickEngine {
public:
    static constexpr int64_t kTickIntervalMs = 50;
    static constexpr int32_t kNumSkills      = 3;

    // Process one tick for a room.
    // Returns battle state payloads to broadcast.
    struct TickResult {
        std::vector<std::pair<uint32_t, std::string>> critical_msgs; // msg_id→payload
        std::vector<std::pair<PlayerId, std::string>> state_updates; // per-player state
    };

    TickResult processTick(
        std::unordered_map<PlayerId, BattlePlayer>& players,
        std::vector<Command> cmds,
        Broadcaster* bc,
        AoiGrid* aoi);

private:
    // Movement
    void processMove(BattlePlayer& p, float dir_x, float dir_y, float dt_sec);

    // Attack
    void processAttack(BattlePlayer& attacker, BattlePlayer& target,
                       Broadcaster* bc);

    // Skill
    void processSkill(BattlePlayer& caster, BattlePlayer* target,
                      const Command& cmd, Broadcaster* bc);

    // Damage
    static int32_t calcDamage(int32_t atk, int32_t def);

    // Cooldown tick
    void updateCooldowns(std::unordered_map<PlayerId, BattlePlayer>& players,
                         int32_t dt_ms);

    // Sequence dedup
    bool isDuplicate(PlayerId pid, uint64_t seq);

    std::unordered_map<PlayerId, uint64_t> last_seq_;
};

}  // namespace miniarena
