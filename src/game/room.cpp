#include "room.h"

namespace miniarena {

Room::Room(RoomId id, int max_players)
    : room_id_(id), max_players_(max_players) {}

int Room::addPlayer(PlayerId player_id, const std::string& username) {
    if (state_ != RoomState::CREATED && state_ != RoomState::WAITING_PLAYERS) {
        return 40003;  // wrong state
    }
    if (isFull()) {
        return 40002;  // room full
    }

    players_[player_id] = {player_id, username, false};

    if (state_ == RoomState::CREATED) {
        state_ = RoomState::WAITING_PLAYERS;
    }
    return 0;
}

void Room::removePlayer(PlayerId player_id) {
    auto it = players_.find(player_id);
    if (it == players_.end()) return;
    if (it->second.ready) --ready_count_;
    players_.erase(it);
}

void Room::playerReady(PlayerId player_id) {
    auto it = players_.find(player_id);
    if (it == players_.end()) return;
    if (!it->second.ready) {
        it->second.ready = true;
        ++ready_count_;
    }
}

bool Room::allReady() const {
    return ready_count_ == static_cast<int>(players_.size()) &&
           !players_.empty();
}

void Room::startLoading() {
    if (state_ == RoomState::WAITING_PLAYERS && isFull()) {
        state_ = RoomState::LOADING;
    }
}

void Room::startBattle() {
    if (state_ == RoomState::LOADING && allReady()) {
        state_ = RoomState::RUNNING;
    }
}

void Room::endBattle() {
    if (state_ == RoomState::RUNNING) {
        state_ = RoomState::SETTLING;
    }
}

void Room::destroy() {
    state_ = RoomState::DESTROYED;
    players_.clear();
}

bool Room::hasPlayer(PlayerId pid) const {
    return players_.find(pid) != players_.end();
}

}  // namespace miniarena
