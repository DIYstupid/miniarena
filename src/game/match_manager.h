#pragma once

#include <cstdint>
#include <functional>
#include "player.h"
#include "room.h"

namespace miniarena {

class RoomManager;
class SessionManager;
class RedisClient;

// Simple matchmaker: players join a queue, when enough gather,
// a room is created and all matched players are notified.
class MatchManager {
public:
    MatchManager(RoomManager* rooms, SessionManager* sessions,
                 RedisClient* redis, int default_room_size = 10);

    // Join/leave the match queue for a given mode.
    // Returns 0 on success, or error code.
    int joinQueue(PlayerId player_id, int mode);
    int leaveQueue(PlayerId player_id);

    // Called periodically to check if a match can be formed.
    // Returns the room_id if a match was created, 0 otherwise.
    RoomId tryMatch(int mode);

    // Set callback for notifying players via their connections.
    using NotifyCallback = std::function<void(PlayerId, uint32_t msg_id,
                                              const std::string& payload)>;
    void setNotifyCallback(NotifyCallback cb) { notify_cb_ = std::move(cb); }

private:
    RoomManager* rooms_;
    SessionManager* sessions_;
    RedisClient* redis_;
    int room_size_;
    NotifyCallback notify_cb_;
};

}  // namespace miniarena
