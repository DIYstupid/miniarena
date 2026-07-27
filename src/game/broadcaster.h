#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <functional>
#include "player.h"

namespace miniarena {

// Manages battle state broadcasting.
// Distinguishes critical messages (immediate) from overridable ones (merged).
class Broadcaster {
public:
    // Callback for sending a frame to a specific connection.
    using SendCallback = std::function<void(PlayerId player_id, uint32_t msg_id,
                                            const std::string& payload)>;

    void setSendCallback(SendCallback cb) { send_cb_ = std::move(cb); }

    // Critical: broadcast to all players in the room immediately.
    void broadcastToRoom(uint32_t msg_id, const std::string& payload);

    // AOI-aware broadcast: send only to specified receivers.
    void broadcastToAoi(const std::vector<PlayerId>& receivers,
                        uint32_t msg_id, const std::string& payload);

    // Overridable: queue a message for a player; later calls with same
    // player_id + msg_id overwrite the previous one.
    void queueOverride(PlayerId player_id, uint32_t msg_id,
                       const std::string& payload);

    // Flush all queued override messages for all players.
    void flushOverrides();

private:
    SendCallback send_cb_;
    std::unordered_map<PlayerId, std::pair<uint32_t, std::string>> overrides_;
};

}  // namespace miniarena
