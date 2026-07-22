#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#include "network/acceptor.h"
#include "network/event_loop.h"
#include "network/connection.h"
#include "network/frame_codec.h"

using namespace miniarena;

int main() {
    constexpr int kNumClients   = 1000;
    constexpr int kIoThreads    = 4;
    constexpr int kDurationSec  = 10;

    std::cout << "=== MiniArena 1000-Connection Smoke Test ===" << std::endl;

    // Create IO event loops
    std::vector<EventLoop*> io_loops;
    for (int i = 0; i < kIoThreads; ++i) {
        io_loops.push_back(new EventLoop());
    }

    // Start IO threads
    std::vector<std::thread> io_threads;
    for (auto* loop : io_loops) {
        io_threads.emplace_back([loop]() { loop->run(); });
    }

    // Frame counter
    std::atomic<size_t> frame_count{0};
    for (auto* loop : io_loops) {
        loop->setFrameCallback([&](ConnectionId, std::vector<Frame> frames) {
            frame_count += frames.size();
        });
    }

    // Start acceptor
    Acceptor acceptor(0, io_loops);
    acceptor.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    uint16_t port = acceptor.port();
    std::cout << "Acceptor on port " << port << std::endl;

    // Connect 1000 clients
    std::vector<int> clients;
    std::cout << "Connecting " << kNumClients << " clients... " << std::flush;

    for (int i = 0; i < kNumClients; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            std::cerr << "socket() failed at client " << i << std::endl;
            continue;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "connect() failed at client " << i << ": " << strerror(errno) << std::endl;
            close(fd);
            continue;
        }
        clients.push_back(fd);
    }
    std::cout << clients.size() << " connected." << std::endl;

    // Wait for accepts to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Count server-side connections
    size_t total_conns = 0;
    for (auto* loop : io_loops) {
        total_conns += loop->connectionCount();
    }
    std::cout << "Server connections: " << total_conns << std::endl;

    // Send and receive for kDurationSec seconds
    std::cout << "Running for " << kDurationSec << " seconds... " << std::flush;
    auto start = std::chrono::steady_clock::now();
    size_t sent_total = 0;

    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(kDurationSec)) {
        for (size_t i = 0; i < clients.size(); i += 10) {
            auto data = FrameCodec::encode(1101, 0, sent_total++, "");
            write(clients[i], data.data(), data.size());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cout << "done. Sent: " << sent_total << ", Received: " << frame_count.load() << std::endl;

    // Cleanup: close all clients
    std::cout << "Closing clients... " << std::flush;
    for (int fd : clients) close(fd);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Stop server
    acceptor.stop();
    for (auto* loop : io_loops) loop->stop();
    for (auto& t : io_threads) t.join();

    // Check cleanup
    total_conns = 0;
    for (auto* loop : io_loops) {
        total_conns += loop->connectionCount();
        delete loop;
    }
    std::cout << "Remaining connections: " << total_conns << std::endl;

    if (total_conns == 0) {
        std::cout << "=== PASS: All connections cleaned up ===" << std::endl;
        return 0;
    } else {
        std::cerr << "=== FAIL: " << total_conns << " connections leaked ===" << std::endl;
        return 1;
    }
}
