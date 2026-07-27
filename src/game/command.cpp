#include "command.h"

namespace miniarena {

void CommandQueue::push(Command cmd) {
    std::lock_guard lock(mtx_);
    queue_.push_back(std::move(cmd));
}

std::vector<Command> CommandQueue::drain() {
    std::lock_guard lock(mtx_);
    std::vector<Command> result;
    result.reserve(queue_.size());
    while (!queue_.empty()) {
        result.push_back(std::move(queue_.front()));
        queue_.pop_front();
    }
    return result;
}

size_t CommandQueue::size() const {
    std::lock_guard lock(mtx_);
    return queue_.size();
}

}  // namespace miniarena
