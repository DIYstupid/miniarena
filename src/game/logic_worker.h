#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <unordered_map>
#include <variant>
#include <vector>

#include "player.h"
#include "room.h"
#include "battle_player.h"
#include "command.h"
#include "tick_engine.h"
#include "aoi_grid.h"
#include "broadcaster.h"

namespace miniarena {

class Room;

// Operation types for thread-safe async dispatch to LogicWorker.
enum class RoomOp : uint8_t { AddRoom, RemoveRoom, MarkDisconnected };

struct PendingOp {
    RoomOp type;
    RoomId room_id = 0;
    PlayerId player_id = 0;
    // Owned pointer for addRoom data (to avoid copying std::unordered_map in variant)
    std::shared_ptr<void> data;  // actually std::unordered_map<PlayerId, BattlePlayer>
    Room* room_ptr = nullptr;
};

// A single logic thread that owns a set of rooms.
// Each room is bound to exactly one LogicWorker for its lifetime.
// Runs a 20 Hz tick loop.
class LogicWorker {
public:
    explicit LogicWorker(int id);
    ~LogicWorker();

    LogicWorker(const LogicWorker&) = delete;
    LogicWorker& operator=(const LogicWorker&) = delete;

    void run();
    void stop();

    // Room management (thread-safe: enqueues operation)
    void addRoom(Room* room,
                 std::unordered_map<PlayerId, BattlePlayer> init_players);
    void removeRoom(RoomId room_id);

    // IO thread pushes commands here (already thread-safe via CommandQueue)
    void pushCommand(Command cmd);

    // P5: Reconnect support (thread-safe: enqueues operation)
    void markDisconnected(PlayerId player_id, RoomId room_id);
    std::string getSnapshot(RoomId room_id) const;

    // Send callback for broadcasting to connections
    using SendCallback = std::function<void(PlayerId, uint32_t, const std::string&)>;
    void setSendCallback(SendCallback cb);
    [[nodiscard]] int id() const noexcept { return id_; }
    [[nodiscard]] size_t roomCount() const noexcept { return rooms_.size(); }

    // Drain pending room operations (public for BattleManager thread start)
    void drainPendingOps();
    void reportTickStats();

private:
    void tick();

    int id_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    std::vector<int64_t> tick_durations_;
    int64_t tick_count_ = 0;
    std::queue<PendingOp> pending_ops_;
    mutable std::mutex pending_mtx_;
    std::condition_variable pending_cv_;
    mutable std::shared_mutex rooms_mtx_;

    CommandQueue cmd_queue_;
    TickEngine tick_engine_;
    AoiGrid aoi_;
    Broadcaster bc_;
    struct RoomState {
        Room* room = nullptr;
        std::unordered_map<PlayerId, BattlePlayer> players;
        std::unordered_map<PlayerId, std::chrono::steady_clock::time_point> disconnected;
    };
    std::unordered_map<RoomId, RoomState> rooms_;
};

}  // namespace miniarena
