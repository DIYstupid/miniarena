#pragma once

#include <atomic>
#include <unordered_map>
#include <thread>
#include "player.h"
#include "room.h"
#include "battle_player.h"
#include "command.h"
#include "tick_engine.h"
#include "aoi_grid.h"
#include "broadcaster.h"

namespace miniarena {

class Room;

// A single logic thread that owns a set of rooms.
// Each room is bound to exactly one LogicWorker for its lifetime.
// Runs a 20 Hz tick loop.
class LogicWorker {
public:
    explicit LogicWorker(int id);

    LogicWorker(const LogicWorker&) = delete;
    LogicWorker& operator=(const LogicWorker&) = delete;

    void run();
    void stop();

    // Room management
    void addRoom(Room* room,
                 std::unordered_map<PlayerId, BattlePlayer> init_players);
    void removeRoom(RoomId room_id);

    // IO thread pushes commands here
    void pushCommand(Command cmd);

    // P5: Reconnect support
    std::string getSnapshot(RoomId room_id) const;
    void markDisconnected(PlayerId player_id, RoomId room_id);

    // Send callback for broadcasting to connections
    using SendCallback = std::function<void(PlayerId, uint32_t, const std::string&)>;
    void setSendCallback(SendCallback cb);
    [[nodiscard]] int id() const noexcept { return id_; }
    [[nodiscard]] size_t roomCount() const noexcept { return rooms_.size(); }

private:
    void tick();

    int id_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    CommandQueue cmd_queue_;
    TickEngine tick_engine_;
    AoiGrid aoi_;
    Broadcaster bc_;
    struct RoomState {
        Room* room = nullptr;
        std::unordered_map<PlayerId, BattlePlayer> players;
        // P5: player_id → disconnect time
        std::unordered_map<PlayerId, std::chrono::steady_clock::time_point> disconnected;
    };
    std::unordered_map<RoomId, RoomState> rooms_;
};

}  // namespace miniarena
