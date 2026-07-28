#pragma once
#include <mutex>
#include <queue>
#include <sys/epoll.h>
#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "network_types.h"
#include "connection.h"
#include "timer_wheel.h"

namespace miniarena {

// Callback invoked when a connection receives decoded frames.
using FrameCallback = std::function<void(ConnectionId conn_id, std::vector<Frame> frames)>;

// Callback invoked when a connection is removed (disconnect/timeout/error).
using DisconnectCallback = std::function<void(ConnectionId conn_id)>;
// Single-threaded epoll event loop.
// Each instance runs on its own IO thread.
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // --- Connection management ---
    // Add an already-accepted connection. Takes ownership.
    void addConnection(std::unique_ptr<Connection> conn);

    // Mark a connection for closing.
    void closeConnection(ConnectionId conn_id);

    // Queue data for sending and enable EPOLLOUT (thread-safe).
    void sendToConnection(ConnectionId conn_id, const std::string& data);

    // --- Event registration ---
    void enableRead(ConnectionId conn_id);
    void enableWrite(ConnectionId conn_id);
    void disableWrite(ConnectionId conn_id);
    void setFrameCallback(FrameCallback cb) { frame_cb_ = std::move(cb); }
    void setDisconnectCallback(DisconnectCallback cb) { disconnect_cb_ = std::move(cb); }
    // --- Main loop ---
    void run();
    void stop();

    // Wake the event loop from epoll_wait (thread-safe).
    void wake();

    // --- Stats ---
    [[nodiscard]] size_t connectionCount() const noexcept;

    // --- Config ---
    void setHeartbeatInterval(uint64_t ms) { heartbeat_ms_ = ms; }
    void setTimeout(uint64_t ms)          { timeout_ms_ = ms; }

    static constexpr int kMaxEvents = 1024;

    void addConnectionImpl(std::unique_ptr<Connection> conn);
    void drainPending();
    void handleEvents(const epoll_event* events, int n);
    void checkTimeouts(uint64_t now_ms);
    void cleanupClosed();
    int epfd_;
    int wake_fd_;

    // Thread-safe pending connection queue (filled by Acceptor thread)
    std::queue<std::unique_ptr<Connection>> pending_conns_;
    mutable std::mutex pending_mtx_;

    std::unordered_map<ConnectionId, std::unique_ptr<Connection>> conns_;
    std::unordered_map<int, ConnectionId> fd_to_id_;
    TimerWheel timer_wheel_;
    FrameCallback frame_cb_;
    DisconnectCallback disconnect_cb_;
    uint64_t heartbeat_ms_ = net::kHeartbeatIntervalMs;
    uint64_t timeout_ms_   = net::kConnectionTimeoutMs;

    std::atomic<bool> running_{false};
};

}  // namespace miniarena
