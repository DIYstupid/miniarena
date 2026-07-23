#include "room_manager.h"

namespace miniarena {

RoomId RoomManager::createRoom(int max_players) {
    RoomId id = next_room_id_.fetch_add(1);
    rooms_[id] = std::make_unique<Room>(id, max_players);
    return id;
}

Room* RoomManager::getRoom(RoomId room_id) {
    auto it = rooms_.find(room_id);
    return it != rooms_.end() ? it->second.get() : nullptr;
}

void RoomManager::destroyRoom(RoomId room_id) {
    rooms_.erase(room_id);
}

}  // namespace miniarena
