#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "network_types.h"
#include "socket.h"
#include "buffer.h"
#include "frame_codec.h"

namespace miniarena {

enum class ConnectionState {
    CONNECTED,   // Just established
    ACTIVE,      // Normal communication
    CLOSING,     // Shutting down (flush send queue)
    CLOSED,      // Ready for cleanup
};

// Represents a single TCP client connection.
// Owned by the EventLoop that manages its fd.
class Connection {
public:
    Connection(ConnectionId id, Socket sock);

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // --- I/O callbacks (called by EventLoop) ---
    // Read from socket, decode frames. Returns decoded frames.
    std::vector<Frame> onReadable();

    // Flush send buffer to socket.
    void onWritable();

    // --- Send ---
    void sendFrame(uint32_t msg_id, uint16_t flags,
                   uint64_t seq, std::string_view payload);
    [[nodiscard]] bool sendQueueFull() const noexcept;
    [[nodiscard]] bool hasPendingWrites() const noexcept;

    // --- Heartbeat / Timeout ---
    void updateActive();
    [[nodiscard]] bool isTimeout(uint64_t now_ms, uint64_t timeout_ms) const;

    // --- Safety ---
    void recordIllegal();
    [[nodiscard]] bool isBanned() const noexcept;
    [[nodiscard]] bool rateLimitExceeded(uint32_t max_per_sec);

    // --- Lifecycle ---
    void close();
    [[nodiscard]] bool shouldCleanup() const noexcept;

    // --- Accessors ---
    [[nodiscard]] ConnectionId id()    const noexcept { return id_; }
    [[nodiscard]] int          fd()    const noexcept { return sock_.fd(); }
    [[nodiscard]] ConnectionState state() const noexcept { return state_; }

    void setSessionId(uint64_t sid) { session_id_ = sid; }
    [[nodiscard]] uint64_t sessionId() const noexcept { return session_id_; }

    static constexpr size_t   kSendQueueMax    = net::kSendQueueMax;
    static constexpr uint32_t kMaxIllegalCount = net::kMaxIllegalCount;
    static constexpr uint32_t kMaxMsgRate      = net::kMaxMsgRatePerSec;

private:
    ConnectionId    id_;
    Socket          sock_;
    ConnectionState state_ = ConnectionState::CONNECTED;

    Buffer recv_buf_;
    Buffer send_buf_;

    uint64_t last_active_ms_   = 0;
    uint32_t illegal_count_    = 0;
    uint64_t session_id_       = 0;  // P3

    // Rate limiting
    uint32_t msg_count_            = 0;
    uint64_t rate_window_start_ms_ = 0;
};

}  // namespace miniarena
