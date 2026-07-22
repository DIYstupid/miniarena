#include <arpa/inet.h>
#include "acceptor.h"
#include "event_loop.h"

#include <spdlog/spdlog.h>
#include <sys/epoll.h>

namespace miniarena {

Acceptor::Acceptor(uint16_t port, std::vector<EventLoop*> io_loops)
    : port_(port)
    , io_loops_(std::move(io_loops)) {
    if (io_loops_.empty()) {
        throw std::runtime_error("Acceptor: at least one IO EventLoop required");
    }
}

Acceptor::~Acceptor() {
    stop();
}

void Acceptor::start() {
    running_ = true;
    thread_ = std::thread(&Acceptor::run, this);
}

void Acceptor::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Acceptor::run() {
    listen_sock_ = Socket::createTcp();
    listen_sock_.setReuseAddr();
    listen_sock_.bind(port_);
    listen_sock_.listen();

    // Resolve actual port (needed when binding to port 0)
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);
    if (getsockname(listen_sock_.fd(), reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0) {
        port_ = ntohs(addr.sin_port);
    }

    spdlog::info("Acceptor: listening on port {}", port_.load());

    // Use a simple epoll for the listen socket
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        spdlog::error("Acceptor: epoll_create1 failed: {}", strerror(errno));
        return;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_sock_.fd();
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sock_.fd(), &ev) < 0) {
        spdlog::error("Acceptor: epoll_ctl failed: {}", strerror(errno));
        close(epfd);
        return;
    }

    size_t next_loop = 0;
    epoll_event events[64];

    while (running_) {
        int n = epoll_wait(epfd, events, 64, 100);  // 100ms timeout for stop check
        if (n < 0) {
            if (errno == EINTR) continue;
            spdlog::error("Acceptor: epoll_wait error: {}", strerror(errno));
            break;
        }

        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd != listen_sock_.fd()) continue;

            // Accept all pending connections
            while (true) {
                sockaddr_in client_addr{};
                auto client_sock = listen_sock_.accept(&client_addr);
                if (!client_sock.valid()) break;

                client_sock.setNoDelay();

                ConnectionId conn_id = next_conn_id_.fetch_add(1);

                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                spdlog::info("conn {}: accepted from {}:{}",
                             conn_id, ip, ntohs(client_addr.sin_port));

                auto conn = std::make_unique<Connection>(conn_id, std::move(client_sock));

                // Round-robin distribute to IO loops
                auto* loop = io_loops_[next_loop];
                next_loop = (next_loop + 1) % io_loops_.size();

                loop->addConnection(std::move(conn));
                loop->wake();  // wake the IO loop to register the new fd
            }
        }
    }

    close(epfd);
    spdlog::info("Acceptor: stopped");
}

}  // namespace miniarena
