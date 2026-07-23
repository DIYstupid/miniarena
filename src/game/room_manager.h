#pragma once

#include <unordered_map>
#include <memory>
#include <atomic>
#include "room.h"

namespace miniarena {

class RoomManager {
public:
    RoomManager() = default;

    [[nodiscard]] RoomId createRoom(int max_players);
    Room* getRoom(RoomId room_id);
    void destroyRoom(RoomId room_id);

    [[nodiscard]] size_t roomCount() const noexcept { return rooms_.size(); }

private:
    std::unordered_map<RoomId, std::unique_ptr<Room>> rooms_;
    std::atomic<RoomId> next_room_id_{1};
};

}  // namespace miniarena
