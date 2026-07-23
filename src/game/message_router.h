#pragma once

#include <functional>
#include <unordered_map>
#include <cstdint>
#include "network/frame_codec.h"

namespace miniarena {

using MessageHandler = std::function<void(ConnectionId conn_id, const Frame& frame)>;

// Routes incoming frames to registered handlers by message_id.
class MessageRouter {
public:
    void registerHandler(uint32_t msg_id, MessageHandler handler);
    void dispatch(ConnectionId conn_id, const Frame& frame);

private:
    std::unordered_map<uint32_t, MessageHandler> handlers_;
};

}  // namespace miniarena
