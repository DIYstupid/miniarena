#include "event_loop.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <cstring>
#include <spdlog/spdlog.h>

namespace miniarena {

EventLoop::EventLoop() {
    epfd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epfd_ < 0) {
        throw std::runtime_error(std::string("epoll_create1 failed: ") + strerror(errno));
    }

    wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd_ < 0) {
        close(epfd_);
        throw std::runtime_error(std::string("eventfd failed: ") + strerror(errno));
    }

    // Register wake_fd for reading
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = wake_fd_;
    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, wake_fd_, &ev) < 0) {
        close(wake_fd_);
        close(epfd_);
        throw std::runtime_error(std::string("epoll_ctl wake_fd failed: ") + strerror(errno));
    }
}

EventLoop::~EventLoop() {
    stop();
    conns_.clear();
    if (wake_fd_ >= 0) close(wake_fd_);
    if (epfd_ >= 0) close(epfd_);
}

void EventLoop::addConnection(std::unique_ptr<Connection> conn) {
    int fd = conn->fd();
    ConnectionId id = conn->id();

    // Register fd for EPOLLIN edge-triggered
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        spdlog::error("epoll_ctl ADD fd {} failed: {}", fd, strerror(errno));
        conn->close();
        return;
    }

    fd_to_id_[fd] = id;

    // Add heartbeat timer
    timer_wheel_.add(id, heartbeat_ms_, [this](ConnectionId cid) {
        // Heartbeat timeout: if still connected, close
        auto it = conns_.find(cid);
        if (it != conns_.end()) {
            spdlog::debug("conn {}: heartbeat timeout", cid);
            closeConnection(cid);
        }
    });

    spdlog::debug("conn {}: added to event loop (fd={})", id, fd);
    conns_[id] = std::move(conn);
}

void EventLoop::closeConnection(ConnectionId id) {
    auto it = conns_.find(id);
    if (it == conns_.end()) return;

    it->second->close();
    timer_wheel_.remove(id);
}

void EventLoop::enableRead(ConnectionId id) {
    auto it = conns_.find(id);
    if (it == conns_.end()) return;

    int fd = it->second->fd();
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
}

void EventLoop::enableWrite(ConnectionId id) {
    auto it = conns_.find(id);
    if (it == conns_.end()) return;

    int fd = it->second->fd();
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
}

void EventLoop::disableWrite(ConnectionId id) {
    auto it = conns_.find(id);
    if (it == conns_.end()) return;

    int fd = it->second->fd();
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fd;
    epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
}

void EventLoop::run() {
    running_ = true;
    spdlog::info("EventLoop: starting");

    epoll_event events[kMaxEvents];

    while (running_) {
        int n = epoll_wait(epfd_, events, kMaxEvents,
                           static_cast<int>(TimerWheel::kTickMs));
        if (n < 0) {
            if (errno == EINTR) continue;
            spdlog::error("epoll_wait error: {}", strerror(errno));
            break;
        }

        handleEvents(events, n);

        uint64_t now_ms = std::chrono::duration_cast<Milliseconds>(
            Clock::now().time_since_epoch()).count();
        checkTimeouts(now_ms);
        cleanupClosed();
    }

    spdlog::info("EventLoop: stopped");
}

void EventLoop::handleEvents(const epoll_event* events, int n) {
    for (int i = 0; i < n; ++i) {
        int fd = events[i].data.fd;

        // Wake event
        if (fd == wake_fd_) {
            uint64_t val;
            while (read(wake_fd_, &val, sizeof(val)) > 0) {}
            continue;
        }

        auto id_it = fd_to_id_.find(fd);
        if (id_it == fd_to_id_.end()) continue;

        ConnectionId id = id_it->second;
        auto conn_it = conns_.find(id);
        if (conn_it == conns_.end()) continue;

        auto& conn = conn_it->second;

        // Readable
        if (events[i].events & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
            auto frames = conn->onReadable();

            if (conn->state() == ConnectionState::CLOSING) {
                closeConnection(id);
            } else if (!frames.empty() && frame_cb_) {
                frame_cb_(id, std::move(frames));
            }

            // Refresh heartbeat on successful read
            timer_wheel_.refresh(id, heartbeat_ms_);
        }

        // Writable
        if (events[i].events & EPOLLOUT) {
            conn->onWritable();

            // If in CLOSING and all data flushed
            if (conn->shouldCleanup()) {
                disableWrite(id);
            }

            // Disable EPOLLOUT if no more pending writes
            if (!conn->hasPendingWrites()) {
                disableWrite(id);
            }
        }
    }
}

void EventLoop::checkTimeouts(uint64_t now_ms) {
    // Check if any active connection has exceeded the timeout
    for (auto it = conns_.begin(); it != conns_.end(); ) {
        auto& conn = it->second;
        if (conn->state() == ConnectionState::ACTIVE ||
            conn->state() == ConnectionState::CONNECTED) {
            if (conn->isTimeout(now_ms, timeout_ms_)) {
                spdlog::debug("conn {}: idle timeout", conn->id());
                closeConnection(conn->id());
            }
        }
        ++it;
    }
}

void EventLoop::cleanupClosed() {
    // timer_wheel_.tick() fires expired callbacks and returns expired IDs
    timer_wheel_.tick();

    // Remove connections in CLOSED state with no pending writes
    for (auto it = conns_.begin(); it != conns_.end(); ) {
        auto& conn = it->second;
        if (conn->shouldCleanup()) {
            int fd = conn->fd();
            spdlog::debug("conn {}: cleanup", conn->id());
            epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
            fd_to_id_.erase(fd);
            it = conns_.erase(it);
        } else {
            ++it;
        }
    }
}

void EventLoop::stop() {
    running_ = false;
    wake();
}

void EventLoop::wake() {
    uint64_t val = 1;
    ssize_t n = write(wake_fd_, &val, sizeof(val));
    (void)n;  // ignore short write
}

size_t EventLoop::connectionCount() const noexcept {
    return conns_.size();
}

}  // namespace miniarena
