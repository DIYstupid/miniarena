#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>
#include "player.h"

namespace miniarena {

enum class CommandType : uint8_t {
    MOVE        = 0,
    STOP_MOVE   = 1,
    ATTACK      = 2,
    SKILL       = 3,
    LEAVE       = 4,
};

struct Command {
    CommandType type;
    PlayerId    player_id;
    uint64_t    sequence    = 0;
    uint64_t    timestamp_ms = 0;

    // Params (union-like via separate fields — only relevant ones filled)
    float       move_dir_x = 0.0f;
    float       move_dir_y = 0.0f;
    PlayerId    target_id  = 0;
    int32_t     skill_id   = 0;
    float       skill_target_x = 0.0f;
    float       skill_target_y = 0.0f;
};

// Thread-safe command queue for IO → Logic worker communication.
class CommandQueue {
public:
    void push(Command cmd);
    std::vector<Command> drain();     // Take all, clear queue
    [[nodiscard]] size_t size() const;

private:
    std::deque<Command> queue_;
    mutable std::mutex mtx_;
};

}  // namespace miniarena
