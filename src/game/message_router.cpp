#include "message_router.h"
#include <spdlog/spdlog.h>

namespace miniarena {

void MessageRouter::registerHandler(uint32_t msg_id, MessageHandler handler) {
    handlers_[msg_id] = std::move(handler);
}

void MessageRouter::dispatch(ConnectionId conn_id, const Frame& frame) {
    auto it = handlers_.find(frame.message_id);
    if (it != handlers_.end()) {
        it->second(conn_id, frame);
    } else {
        spdlog::warn("No handler for message_id {}", frame.message_id);
    }
}

}  // namespace miniarena
