#include "logic_worker.h"
#include "room.h"
#include "messages.pb.h"

#include <spdlog/spdlog.h>
#include <chrono>

namespace miniarena {

LogicWorker::LogicWorker(int id)
    : id_(id) {}

LogicWorker::~LogicWorker() {
    stop();
}

void LogicWorker::run() {
    running_ = true;
    spdlog::info("LogicWorker {}: starting", id_);

    auto last_tick = std::chrono::steady_clock::now();

    while (running_) {
        drainPendingOps();

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_tick).count();

        if (elapsed >= TickEngine::kTickIntervalMs) {
            auto t0 = std::chrono::steady_clock::now();
            tick();
            auto t1 = std::chrono::steady_clock::now();
            last_tick = now;
            int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            tick_durations_.push_back(us);
            if (++tick_count_ % 200 == 0) {
                auto cp = tick_durations_;
                std::sort(cp.begin(), cp.end());
                size_t n = cp.size();
                char buf[128];
                snprintf(buf, sizeof(buf), "Tick[%ld] P50=%ldus P95=%ldus P99=%ldus Max=%ldus\n",
                         tick_count_, cp[n*50/100], cp[n*95/100], cp[n*99/100], cp.back());
                write(2, buf, strlen(buf));
                tick_durations_.clear();
            }
        }

        auto remaining = TickEngine::kTickIntervalMs - elapsed;
        if (remaining > 0) {
            std::unique_lock<std::mutex> lock(pending_mtx_);
            pending_cv_.wait_for(lock, std::chrono::milliseconds(remaining),
                                 [this] { return !pending_ops_.empty(); });
        }
    }

    spdlog::info("LogicWorker {}: stopped", id_);
}

void LogicWorker::stop() {
    running_ = false;
    pending_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void LogicWorker::addRoom(Room* room,
                           std::unordered_map<PlayerId, BattlePlayer> init_players) {
    auto data = std::make_shared<std::unordered_map<PlayerId, BattlePlayer>>(
        std::move(init_players));
    {
        std::lock_guard<std::mutex> lock(pending_mtx_);
        pending_ops_.push({RoomOp::AddRoom, room->id(), 0, data, room});
    }
    pending_cv_.notify_one();
}

void LogicWorker::removeRoom(RoomId room_id) {
    {
        std::lock_guard<std::mutex> lock(pending_mtx_);
        pending_ops_.push({RoomOp::RemoveRoom, room_id, 0, nullptr, nullptr});
    }
    pending_cv_.notify_one();
}

void LogicWorker::markDisconnected(PlayerId player_id, RoomId room_id) {
    {
        std::lock_guard<std::mutex> lock(pending_mtx_);
        pending_ops_.push({RoomOp::MarkDisconnected, room_id, player_id, nullptr, nullptr});
    }
    pending_cv_.notify_one();
}

void LogicWorker::drainPendingOps() {
    std::unique_lock<std::mutex> lock(pending_mtx_);
    while (!pending_ops_.empty()) {
        auto op = std::move(pending_ops_.front());
        pending_ops_.pop();
        lock.unlock();

        std::unique_lock<std::shared_mutex> wlock(rooms_mtx_);
        switch (op.type) {
        case RoomOp::AddRoom: {
            auto players = std::static_pointer_cast<
                std::unordered_map<PlayerId, BattlePlayer>>(op.data);
            RoomState rs;
            rs.room = op.room_ptr;
            rs.players = std::move(*players);
            rooms_[op.room_id] = std::move(rs);
            spdlog::info("LogicWorker {}: room {} added ({} players)",
                         id_, op.room_id, rooms_[op.room_id].players.size());
            break;
        }
        case RoomOp::RemoveRoom:
            rooms_.erase(op.room_id);
            break;
        case RoomOp::MarkDisconnected:
            if (auto it = rooms_.find(op.room_id); it != rooms_.end()) {
                it->second.disconnected[op.player_id] =
                    std::chrono::steady_clock::now();
            }
            break;
        }

        lock.lock();
    }
}

void LogicWorker::pushCommand(Command cmd) {
    cmd_queue_.push(std::move(cmd));
}

void LogicWorker::setSendCallback(SendCallback cb) {
    bc_.setSendCallback(std::move(cb));
}

void LogicWorker::tick() {
    auto cmds = cmd_queue_.drain();

    std::unique_lock<std::shared_mutex> lock(rooms_mtx_);

    for (auto& [room_id, rs] : rooms_) {
        auto result = tick_engine_.processTick(rs.players, cmds, &bc_, &aoi_);

        miniarena::BattleStateNotify state;
        state.set_room_id(room_id);
        state.set_tick(0);

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

        lock.unlock();

        std::string data;
        state.SerializeToString(&data);

        // Broadcast to each player's AOI neighbors
        for (auto& [pid, bp] : rs.players) {
            auto nearby = aoi_.getNearby(pid);
            bc_.broadcastToAoi(nearby, 4004, data);
        }
        bc_.flushOverrides();

        lock.lock();
        // Check disconnected timeout (30s)
        auto now = std::chrono::steady_clock::now();
        for (auto it2 = rs.disconnected.begin(); it2 != rs.disconnected.end(); ) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - it2->second).count();
            if (elapsed > 30) {
                auto pit = rs.players.find(it2->first);
                if (pit != rs.players.end()) {
                    pit->second.alive = false;
                    pit->second.hp = 0;
                }
                it2 = rs.disconnected.erase(it2);
            } else {
                ++it2;
            }
        }
    }
}

std::string LogicWorker::getSnapshot(RoomId room_id) const {
    std::shared_lock<std::shared_mutex> lock(rooms_mtx_);

    miniarena::BattleSnapshotNotify snap;
    snap.set_room_id(room_id);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return {};

    for (auto& [pid, bp] : it->second.players) {
        auto* ps = snap.add_players();
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
    snap.SerializeToString(&data);
    return data;
}

}  // namespace miniarena
