#include "broadcaster.h"
#include "session.h"

namespace miniarena {

void Broadcaster::broadcastToRoom(uint32_t msg_id, const std::string& payload) {
    // Critical messages are sent to all players.
    // The caller (room) tracks which players are in the room and calls this.
    // Actual send happens via the callback set by the room/logic worker.
    if (send_cb_) {
        send_cb_(0, msg_id, payload);  // player_id=0 means "broadcast to all"
    }
}

void Broadcaster::broadcastToAoi(const std::vector<PlayerId>& receivers,
                                  uint32_t msg_id, const std::string& payload) {
    if (!send_cb_) return;
    for (PlayerId pid : receivers) {
        send_cb_(pid, msg_id, payload);
    }
}

void Broadcaster::queueOverride(PlayerId player_id, uint32_t msg_id,
                                 const std::string& payload) {
    overrides_[player_id] = {msg_id, payload};
}

void Broadcaster::flushOverrides() {
    for (auto& [pid, msg] : overrides_) {
        if (send_cb_) {
            send_cb_(pid, msg.first, msg.second);
        }
    }
    overrides_.clear();
}

}  // namespace miniarena
