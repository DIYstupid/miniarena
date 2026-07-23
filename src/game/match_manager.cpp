#include "match_manager.h"
#include "room_manager.h"
#include "session_manager.h"
#include "storage/redis_client.h"
#include "messages.pb.h"

#include <spdlog/spdlog.h>

namespace miniarena {

MatchManager::MatchManager(RoomManager* rooms, SessionManager* sessions,
                           RedisClient* redis, int default_room_size)
    : rooms_(rooms), sessions_(sessions), redis_(redis),
      room_size_(default_room_size) {}

int MatchManager::joinQueue(PlayerId player_id, int mode) {
    redis_->pushMatchQueue(mode, player_id);
    sessions_->setState(sessions_->getByPlayer(player_id)->session_id,
                        SessionState::MATCHING);
    sessions_->setMatchMode(sessions_->getByPlayer(player_id)->session_id, mode);
    spdlog::debug("Player {} joined match queue mode {}", player_id, mode);
    return 0;
}

int MatchManager::leaveQueue(PlayerId player_id) {
    // We can't remove from a Redis list easily; the player will just
    // be filtered out when matched. For now, revert state.
    auto* s = sessions_->getByPlayer(player_id);
    if (s) {
        sessions_->setState(s->session_id, SessionState::IN_LOBBY);
        sessions_->setMatchMode(s->session_id, 0);
    }
    return 0;
}

RoomId MatchManager::tryMatch(int mode) {
    int count = redis_->matchQueueSize(mode);
    if (count < room_size_) return 0;

    // Create room
    RoomId room_id = rooms_->createRoom(room_size_);
    spdlog::info("Match formed: room {} with {} players", room_id, room_size_);

    // Pop players and add to room
    for (int i = 0; i < room_size_; ++i) {
        auto pid_opt = redis_->popMatchQueue(mode);
        if (!pid_opt) break;

        PlayerId pid = *pid_opt;
        auto* s = sessions_->getByPlayer(pid);
        if (!s) continue;

        rooms_->getRoom(room_id)->addPlayer(pid, s->username);
        sessions_->setState(s->session_id, SessionState::IN_ROOM);
        sessions_->setCurrentRoom(s->session_id, room_id);

        // Notify player
        miniarena::MatchSuccessNotify notify;
        notify.set_room_id(room_id);
        notify.set_server_addr("127.0.0.1:9000");
        std::string data;
        notify.SerializeToString(&data);

        if (notify_cb_) {
            notify_cb_(pid, 2004, data);
        }
    }

    return room_id;
}

}  // namespace miniarena
