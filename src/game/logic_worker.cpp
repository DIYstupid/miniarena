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
    RoomState rs;
    rs.room = room;
    rs.players = std::move(init_players);
    rooms_[room->id()] = std::move(rs);
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

    // Flush override messages
    bc_.flushOverrides();

    // P5: Check disconnected players timeout (30s)
    auto now = std::chrono::steady_clock::now();
    for (auto& [room_id, rs] : rooms_) {
        for (auto it = rs.disconnected.begin(); it != rs.disconnected.end(); ) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - it->second).count();
            if (elapsed >= 30) {
                // Timeout: remove player from battle
                PlayerId pid = it->first;
                rs.players.erase(pid);
                aoi_.remove(pid);
                spdlog::info("LogicWorker {}: player {} disconnected timeout, removed from room {}",
                             id_, pid, room_id);
                it = rs.disconnected.erase(it);
            } else {
                ++it;
            }
        }
    }
}

std::string LogicWorker::getSnapshot(RoomId room_id) const {
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return "";

    miniarena::BattleSnapshotNotify snapshot;
    snapshot.set_room_id(room_id);
    snapshot.set_current_tick(0);

    for (auto& [pid, bp] : it->second.players) {
        auto* ps = snapshot.add_players();
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
    snapshot.SerializeToString(&data);
    return data;
}

void LogicWorker::markDisconnected(PlayerId player_id, RoomId room_id) {
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return;

    it->second.disconnected[player_id] = std::chrono::steady_clock::now();
    spdlog::info("LogicWorker {}: player {} marked disconnected in room {}",
                 id_, player_id, room_id);
}

}  // namespace miniarena
