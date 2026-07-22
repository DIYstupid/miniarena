#include "connection.h"

#include <spdlog/spdlog.h>

namespace miniarena {

Connection::Connection(ConnectionId id, Socket sock)
    : id_(id)
    , sock_(std::move(sock))
    , recv_buf_(4096)
    , send_buf_(4096) {
    last_active_ms_ = std::chrono::duration_cast<Milliseconds>(
        Clock::now().time_since_epoch()).count();
}

std::vector<Frame> Connection::onReadable() {
    std::vector<Frame> frames;

    ssize_t n = recv_buf_.readFromFd(sock_.fd());
    if (n == 0) {
        // Peer closed connection
        spdlog::debug("conn {}: peer closed", id_);
        close();
        return frames;
    }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return frames;
        }
        spdlog::debug("conn {}: read error: {}", id_, strerror(errno));
        close();
        return frames;
    }

    updateActive();

    // Decode all complete frames in buffer
    while (recv_buf_.readable() >= FrameCodec::kHeaderSize) {
        try {
            auto frame = FrameCodec::decode(recv_buf_);
            if (!frame.has_value()) {
                break;  // half-packet, wait for more data
            }
            frames.push_back(std::move(*frame));
        } catch (const std::runtime_error& e) {
            spdlog::warn("conn {}: decode error: {}", id_, e.what());
            recordIllegal();
            if (isBanned()) {
                close();
                break;
            }
        }
    }

    return frames;
}

void Connection::onWritable() {
    if (send_buf_.readable() == 0) return;
    ssize_t n = send_buf_.writeToFd(sock_.fd());
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        spdlog::debug("conn {}: write error: {}", id_, strerror(errno));
        close();
    }
}

void Connection::sendFrame(uint32_t msg_id, uint16_t flags,
                            uint64_t seq, std::string_view payload) {
    if (state_ != ConnectionState::CONNECTED &&
        state_ != ConnectionState::ACTIVE) {
        return;
    }

    try {
        auto data = FrameCodec::encode(msg_id, flags, seq, payload);
        if (send_buf_.readable() + data.size() > kSendQueueMax) {
            spdlog::warn("conn {}: send queue full, closing", id_);
            close();
            return;
        }
        send_buf_.write(data.data(), data.size());
    } catch (const std::runtime_error& e) {
        spdlog::warn("conn {}: encode error: {}", id_, e.what());
    }
}

bool Connection::sendQueueFull() const noexcept {
    return send_buf_.readable() >= kSendQueueMax;
}

bool Connection::hasPendingWrites() const noexcept {
    return send_buf_.readable() > 0;
}

void Connection::updateActive() {
    last_active_ms_ = std::chrono::duration_cast<Milliseconds>(
        Clock::now().time_since_epoch()).count();
    state_ = ConnectionState::ACTIVE;
}

bool Connection::isTimeout(uint64_t now_ms, uint64_t timeout_ms) const {
    return now_ms - last_active_ms_ > timeout_ms;
}

void Connection::recordIllegal() {
    ++illegal_count_;
}

bool Connection::isBanned() const noexcept {
    return illegal_count_ >= kMaxIllegalCount;
}

bool Connection::rateLimitExceeded(uint32_t max_per_sec) {
    uint64_t now_ms = std::chrono::duration_cast<Milliseconds>(
        Clock::now().time_since_epoch()).count();

    if (now_ms - rate_window_start_ms_ >= 1000) {
        // New window
        rate_window_start_ms_ = now_ms;
        msg_count_ = 1;
        return false;
    }

    ++msg_count_;
    return msg_count_ > max_per_sec;
}
void Connection::close() {
    if (state_ == ConnectionState::CLOSED || state_ == ConnectionState::CLOSING) {
        return;
    }
    state_ = ConnectionState::CLOSING;
}

bool Connection::shouldCleanup() const noexcept {
    // CLOSING + send queue drained → ready for cleanup
    return state_ == ConnectionState::CLOSING && send_buf_.readable() == 0;
}

}  // namespace miniarena
