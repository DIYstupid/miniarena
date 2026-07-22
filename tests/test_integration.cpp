#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <cstring>
#include "network/acceptor.h"
#include "network/event_loop.h"
#include "network/connection.h"

using namespace miniarena;

// Integration test: full network stack
// Acceptor → EventLoop → Connection → FrameCodec

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create IO event loops
        for (int i = 0; i < 2; ++i) {
            io_loops_.push_back(new EventLoop());
        }
    }

    void TearDown() override {
        for (auto* loop : io_loops_) {
            loop->stop();
            delete loop;
        }
    }

    // Connect a TCP client to localhost:port
    int connectClient(uint16_t port) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(fd, 0);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        int ret = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        EXPECT_EQ(ret, 0) << "connect failed: " << strerror(errno);
        return fd;
    }

    std::vector<EventLoop*> io_loops_;
};

TEST_F(IntegrationTest, AcceptAndReceiveData) {
    // Start IO event loops in background threads
    std::vector<std::thread> io_threads;
    for (auto* loop : io_loops_) {
        io_threads.emplace_back([loop]() { loop->run(); });
    }

    // Set frame callback on first IO loop
    std::vector<Frame> received;
    io_loops_[0]->setFrameCallback([&](ConnectionId, std::vector<Frame> frames) {
        for (auto& f : frames) received.push_back(std::move(f));
    });

    // Start acceptor on port 0 (OS picks)
    Acceptor acceptor(0, io_loops_);
    acceptor.start();

    // Give acceptor time to bind
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Connect client
    int client_fd = connectClient(acceptor.port());

    // Give time for accept + registration
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send a frame
    auto data = FrameCodec::encode(4001, 0, 42, "integration_test");
    ssize_t sent = write(client_fd, data.data(), data.size());
    EXPECT_EQ(sent, static_cast<ssize_t>(data.size()));

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0].message_id, 4001);
    EXPECT_EQ(received[0].sequence, 42);
    EXPECT_EQ(received[0].payload, "integration_test");

    // Cleanup
    close(client_fd);
    acceptor.stop();
    for (auto* loop : io_loops_) loop->stop();
    for (auto& t : io_threads) t.join();
}

TEST_F(IntegrationTest, MultipleClients) {
    std::vector<std::thread> io_threads;
    for (auto* loop : io_loops_) {
        io_threads.emplace_back([loop]() { loop->run(); });
    }

    std::vector<Frame> received;
    std::mutex mtx;
    io_loops_[0]->setFrameCallback([&](ConnectionId, std::vector<Frame> frames) {
        std::lock_guard lock(mtx);
        for (auto& f : frames) received.push_back(std::move(f));
    });
    io_loops_[1]->setFrameCallback([&](ConnectionId, std::vector<Frame> frames) {
        std::lock_guard lock(mtx);
        for (auto& f : frames) received.push_back(std::move(f));
    });

    Acceptor acceptor(0, io_loops_);
    acceptor.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Connect 10 clients
    std::vector<int> clients;
    for (int i = 0; i < 10; ++i) {
        clients.push_back(connectClient(acceptor.port()));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Each client sends a unique message
    for (int i = 0; i < 10; ++i) {
        auto data = FrameCodec::encode(4001, 0, i, "client_" + std::to_string(i));
        write(clients[i], data.data(), data.size());
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_EQ(received.size(), 10);
    for (const auto& f : received) {
        EXPECT_EQ(f.message_id, 4001);
    }

    for (int fd : clients) close(fd);
    acceptor.stop();
    for (auto* loop : io_loops_) loop->stop();
    for (auto& t : io_threads) t.join();
}

TEST_F(IntegrationTest, ClientCloseIsHandled) {
    std::vector<std::thread> io_threads;
    for (auto* loop : io_loops_) {
        io_threads.emplace_back([loop]() { loop->run(); });
    }

    Acceptor acceptor(0, io_loops_);
    acceptor.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    int client_fd = connectClient(acceptor.port());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    size_t initial_count = io_loops_[0]->connectionCount() + io_loops_[1]->connectionCount();
    EXPECT_EQ(initial_count, 1);

    // Close client
    close(client_fd);

    // Wait for server to detect close and cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Connection should be removed after cleanup
    size_t final_count = io_loops_[0]->connectionCount() + io_loops_[1]->connectionCount();
    EXPECT_EQ(final_count, 0);

    acceptor.stop();
    for (auto* loop : io_loops_) loop->stop();
    for (auto& t : io_threads) t.join();
}
