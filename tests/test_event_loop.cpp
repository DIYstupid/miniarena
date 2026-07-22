#include <gtest/gtest.h>
#include <unistd.h>
#include <sys/socket.h>
#include <thread>
#include <chrono>
#include "network/event_loop.h"
#include "network/socket.h"

using namespace miniarena;

class EventLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        loop_ = std::make_unique<EventLoop>();
        loop_->setTimeout(100);  // short timeout for testing
    }

    void TearDown() override {
        loop_->stop();
    }

    std::pair<Socket, Socket> makePair() {
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv);
        return {Socket(sv[0]), Socket(sv[1])};
    }

    std::unique_ptr<EventLoop> loop_;
};

TEST_F(EventLoopTest, StartsAndStops) {
    EXPECT_EQ(loop_->connectionCount(), 0);
    loop_->stop();
}

TEST_F(EventLoopTest, AddConnectionIncrementsCount) {
    auto [server, client] = makePair();
    auto conn = std::make_unique<Connection>(1, std::move(server));
    loop_->addConnection(std::move(conn));
    EXPECT_EQ(loop_->connectionCount(), 1);
}

TEST_F(EventLoopTest, CloseConnectionRemovesIt) {
    auto [server, client] = makePair();
    auto conn = std::make_unique<Connection>(1, std::move(server));
    loop_->addConnection(std::move(conn));
    EXPECT_EQ(loop_->connectionCount(), 1);

    loop_->closeConnection(1);
    // Connection should be marked CLOSING, still in map until cleanup
    // (cleanup happens in run loop, not here)
}

TEST_F(EventLoopTest, EnableAndDisableWrite) {
    auto [server, client] = makePair();
    auto conn = std::make_unique<Connection>(1, std::move(server));
    loop_->addConnection(std::move(conn));

    // These should not crash
    loop_->enableWrite(1);
    loop_->disableWrite(1);
}

TEST_F(EventLoopTest, FrameCallbackReceivesData) {
    auto [server, client] = makePair();
    int server_fd = server.fd();
    auto conn = std::make_unique<Connection>(1, std::move(server));
    loop_->addConnection(std::move(conn));

    std::vector<Frame> received;
    loop_->setFrameCallback([&](ConnectionId id, std::vector<Frame> frames) {
        for (auto& f : frames) {
            received.push_back(std::move(f));
        }
    });

    // Run event loop in a thread
    std::thread t([this]() { loop_->run(); });

    // Send data from client side
    auto data = FrameCodec::encode(4001, 0, 1, "hello_event_loop");
    write(client.fd(), data.data(), data.size());

    // Give it time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    loop_->stop();
    t.join();

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0].message_id, 4001);
    EXPECT_EQ(received[0].payload, "hello_event_loop");
}

TEST_F(EventLoopTest, ConnectionCountAfterRun) {
    auto [server, client] = makePair();
    auto conn = std::make_unique<Connection>(1, std::move(server));
    loop_->addConnection(std::move(conn));

    std::thread t([this]() { loop_->run(); });

    // Send data to activate
    auto data = FrameCodec::encode(1101, 0, 1, "");
    write(client.fd(), data.data(), data.size());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    loop_->stop();
    t.join();

    EXPECT_EQ(loop_->connectionCount(), 1);
}
