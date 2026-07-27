#include "logic_worker.h"
#include "room.h"
#include "messages.pb.h"

#include <spdlog/spdlog.h>
#include <chrono>

namespace miniarena {

LogicWorker::LogicWorker(int id)
    : id_(id) {}

void LogicWorker::run() {
    running_ = true;
    spdlog::info("LogicWorker {}: starting", id_);

    auto last_tick = std::chrono::steady_clock::now();

    while (running_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_tick).count();

        if (elapsed >= TickEngine::kTickIntervalMs) {
            tick();
            last_tick = now;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    spdlog::info("LogicWorker {}: stopped", id_);
}

void LogicWorker::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void LogicWorker::addRoom(Room* room,
                           std::unordered_map<PlayerId, BattlePlayer> init_players) {
    rooms_[room->id()] = {room, std::move(init_players)};
    spdlog::info("LogicWorker {}: room {} added ({} players)",
                 id_, room->id(), rooms_[room->id()].players.size());
}

void LogicWorker::removeRoom(RoomId room_id) {
    rooms_.erase(room_id);
}

void LogicWorker::pushCommand(Command cmd) {
    cmd_queue_.push(std::move(cmd));
}

void LogicWorker::setSendCallback(SendCallback cb) {
    bc_.setSendCallback(std::move(cb));
}

void LogicWorker::tick() {
    auto cmds = cmd_queue_.drain();

    for (auto& [room_id, rs] : rooms_) {
        auto result = tick_engine_.processTick(rs.players, cmds, &bc_, &aoi_);

        // Build and send BattleStateNotify for the whole room
        miniarena::BattleStateNotify state;
        state.set_room_id(room_id);
        state.set_tick(0);  // increment tick counter

        for (auto& [pid, bp] : rs.players) {
            auto* ps = state.add_players();
            ps->set_player_id(bp.id);
            ps->set_position_x(bp.pos_x);
            ps->set_position_y(bp.pos_y);
            ps->set_direction_x(bp.dir_x);
            ps->set_direction_y(bp.dir_y);
            ps->set_hp(bp.hp);
            ps->set_max_hp(bp.max_hp);
            ps->set_alive(bp.alive);
        }

        std::string data;
        state.SerializeToString(&data);

        // Send via broadcaster (per-player override for position)
        for (auto& [pid, bp] : rs.players) {
            auto nearby = aoi_.getNearby(pid);
            bc_.broadcastToAoi(nearby, 4004, data);
        }
    }

    // Flush any override messages from this tick
    bc_.flushOverrides();
}

}  // namespace miniarena
