#include "tick_engine.h"
#include "broadcaster.h"
#include "aoi_grid.h"
#include "messages.pb.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace miniarena {

const SkillDef kSkills[3] = {
    {1, "重击",   3000, 1.5f, 50.0f,  false, 0.0f},
    {2, "旋风斩", 5000, 0.8f, 80.0f,  true,  60.0f},
    {3, "突进",   8000, 0.5f, 200.0f, false, 0.0f},
};

int32_t TickEngine::calcDamage(int32_t atk, int32_t def) {
    int32_t raw = atk - def / 2;
    return std::max(1, raw);
}

bool TickEngine::isDuplicate(PlayerId pid, uint64_t seq) {
    auto it = last_seq_.find(pid);
    if (it != last_seq_.end() && seq <= it->second) {
        return true;
    }
    last_seq_[pid] = seq;
    return false;
}

void TickEngine::processMove(BattlePlayer& p, float dir_x, float dir_y,
                              float dt_sec) {
    // Normalize direction
    float len = std::sqrt(dir_x * dir_x + dir_y * dir_y);
    if (len < 0.0001f) return;

    float ndx = dir_x / len;
    float ndy = dir_y / len;

    float dist = p.move_speed * dt_sec;
    p.pos_x += ndx * dist;
    p.pos_y += ndy * dist;
    p.dir_x = ndx;
    p.dir_y = ndy;
}

void TickEngine::processAttack(BattlePlayer& attacker, BattlePlayer& target,
                                Broadcaster* bc) {
    if (!attacker.alive || !target.alive) return;

    // Range check (simple: arbitrary 60px)
    float dx = target.pos_x - attacker.pos_x;
    float dy = target.pos_y - attacker.pos_y;
    float dist = std::sqrt(dx * dx + dy * dy);

    if (dist > 60.0f) {
        // Out of range — ignore
        return;
    }

    int32_t dmg = calcDamage(attacker.atk, target.def);
    target.hp -= dmg;
    target.damage_taken += dmg;
    attacker.damage_dealt += dmg;

    if (target.hp <= 0) {
        target.hp = 0;
        target.alive = false;
        target.deaths++;
        attacker.kills++;

        // Broadcast death
        if (bc) {
            miniarena::PlayerDeathNotify notify;
            notify.set_player_id(target.id);
            notify.set_killer_id(attacker.id);
            notify.set_death_count(target.deaths);
            std::string data;
            notify.SerializeToString(&data);
            bc->broadcastToRoom(4005, data);
        }
    }
}

void TickEngine::processSkill(BattlePlayer& caster, BattlePlayer* target,
                               const Command& cmd, Broadcaster* bc) {
    if (!caster.alive) return;
    if (cmd.skill_id < 1 || cmd.skill_id > kNumSkills) return;

    const auto& skill = kSkills[cmd.skill_id - 1];

    // Cooldown check
    auto it = caster.cooldowns.find(cmd.skill_id);
    if (it != caster.cooldowns.end() && it->second > 0) {
        return;  // on cooldown
    }

    // Set cooldown
    caster.cooldowns[cmd.skill_id] = skill.cooldown_ms;

    if (skill.aoe) {
        // AOE: damage all nearby players (handled by caller with AOI)
        // For now, just damage the primary target
        if (target && target->alive) {
            int32_t dmg = static_cast<int32_t>(calcDamage(caster.atk, target->def) * skill.damage_ratio);
            target->hp -= dmg;
            target->damage_taken += dmg;
            caster.damage_dealt += dmg;

            if (target->hp <= 0) {
                target->hp = 0;
                target->alive = false;
                target->deaths++;
                caster.kills++;
            }
        }
    } else {
        if (target && target->alive) {
            float dx = target->pos_x - caster.pos_x;
            float dy = target->pos_y - caster.pos_y;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist <= skill.range) {
                int32_t dmg = static_cast<int32_t>(calcDamage(caster.atk, target->def) * skill.damage_ratio);
                target->hp -= dmg;
                target->damage_taken += dmg;
                caster.damage_dealt += dmg;

                if (target->hp <= 0) {
                    target->hp = 0;
                    target->alive = false;
                    target->deaths++;
                    caster.kills++;

                    if (bc) {
                        miniarena::PlayerDeathNotify notify;
                        notify.set_player_id(target->id);
                        notify.set_killer_id(caster.id);
                        notify.set_death_count(target->deaths);
                        std::string data;
                        notify.SerializeToString(&data);
                        bc->broadcastToRoom(4005, data);
                    }
                }
            }
        }
    }
}

void TickEngine::updateCooldowns(
    std::unordered_map<PlayerId, BattlePlayer>& players, int32_t dt_ms) {
    for (auto& [pid, p] : players) {
        for (auto it = p.cooldowns.begin(); it != p.cooldowns.end(); ) {
            it->second -= dt_ms;
            if (it->second <= 0) {
                it = p.cooldowns.erase(it);
            } else {
                ++it;
            }
        }
    }
}

TickEngine::TickResult TickEngine::processTick(
    std::unordered_map<PlayerId, BattlePlayer>& players,
    std::vector<Command> cmds,
    Broadcaster* bc,
    AoiGrid* aoi) {

    TickResult result;
    float dt_sec = kTickIntervalMs / 1000.0f;

    // 1. Process commands (with sequence dedup)
    for (auto& cmd : cmds) {
        auto pit = players.find(cmd.player_id);
        if (pit == players.end() || !pit->second.alive) continue;
        if (isDuplicate(cmd.player_id, cmd.sequence)) continue;

        auto& p = pit->second;

        switch (cmd.type) {
        case CommandType::MOVE:
            processMove(p, cmd.move_dir_x, cmd.move_dir_y, dt_sec);
            break;
        case CommandType::STOP_MOVE:
            p.dir_x = 0;
            p.dir_y = 0;
            break;
        case CommandType::ATTACK: {
            auto tit = players.find(cmd.target_id);
            if (tit != players.end()) {
                processAttack(p, tit->second, bc);
            }
            break;
        }
        case CommandType::SKILL: {
            auto tit = players.find(cmd.target_id);
            processSkill(p, tit != players.end() ? &tit->second : nullptr, cmd, bc);
            break;
        }
        case CommandType::LEAVE:
            p.alive = false;
            break;
        }
    }

    // 2. Update cooldowns
    updateCooldowns(players, kTickIntervalMs);

    // 3. Build state notifications (AOI-aware + overridable)
    for (auto& [pid, p] : players) {
        // Queue position update as override message
        if (bc) {
            miniarena::BattleStateNotify state;
            state.set_room_id(0);  // set by broadcaster
            state.set_tick(0);

            auto* ps = state.add_players();
            ps->set_player_id(p.id);
            ps->set_position_x(p.pos_x);
            ps->set_position_y(p.pos_y);
            ps->set_direction_x(p.dir_x);
            ps->set_direction_y(p.dir_y);
            ps->set_hp(p.hp);
            ps->set_max_hp(p.max_hp);
            ps->set_alive(p.alive);

            std::string data;
            state.SerializeToString(&data);
            bc->queueOverride(pid, 4004, data);
        }

        // AOI update
        if (aoi) {
            aoi->update(pid, p.pos_x, p.pos_y);
        }
    }

    return result;
}

}  // namespace miniarena
