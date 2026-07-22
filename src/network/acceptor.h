#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <thread>

#include "socket.h"
#include "network_types.h"

namespace miniarena {

class EventLoop;

// Main Reactor: listens on a port, accepts connections,
// and distributes them round-robin to IO EventLoops.
// Runs on its own thread.
class Acceptor {
public:
    Acceptor(uint16_t port, std::vector<EventLoop*> io_loops);
    ~Acceptor();

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    // Start accepting in a background thread.
    void start();
    [[nodiscard]] uint16_t port() const noexcept { return port_.load(); }
    // Stop accepting.
    void stop();

    // Block the current thread running the accept loop.
    void run();


private:
    std::atomic<uint16_t> port_;
    std::vector<EventLoop*> io_loops_;
    Socket listen_sock_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::atomic<ConnectionId> next_conn_id_{1};
};

}  // namespace miniarena
