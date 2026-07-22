#include <gtest/gtest.h>
#include <unistd.h>
#include <sys/socket.h>
#include <thread>
#include "network/connection.h"
#include "network/socket.h"

using namespace miniarena;

class ConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a socket pair for testing (no real network needed)
        int sv[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv), 0);
        server_fd = sv[0];
        client_fd = sv[1];
    }

    void TearDown() override {
        if (server_fd >= 0) close(server_fd);
        if (client_fd >= 0) close(client_fd);
    }

    Connection makeServerConn() {
        return Connection(1, Socket(server_fd));
    }

    void clientSend(const std::string& data) {
        ASSERT_EQ(write(client_fd, data.data(), data.size()),
                  static_cast<ssize_t>(data.size()));
    }

    int server_fd = -1;
    int client_fd = -1;
};

TEST_F(ConnectionTest, InitialStateIsConnected) {
    auto conn = makeServerConn();
    EXPECT_EQ(conn.state(), ConnectionState::CONNECTED);
    EXPECT_EQ(conn.id(), 1);
    EXPECT_TRUE(conn.fd() >= 0);
}

TEST_F(ConnectionTest, OnReadableReceivesFrame) {
    auto conn = makeServerConn();

    // Send a valid frame from client side
    auto data = FrameCodec::encode(4001, 0, 1, "move");
    clientSend(data);

    auto frames = conn.onReadable();
    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].message_id, 4001);
    EXPECT_EQ(frames[0].payload, "move");
    EXPECT_EQ(conn.state(), ConnectionState::ACTIVE);
}

TEST_F(ConnectionTest, OnReadableHalfPacket) {
    auto conn = makeServerConn();
    auto data = FrameCodec::encode(4001, 0, 1, std::string(100, 'X'));

    // Send only part of the frame
    clientSend(data.substr(0, 20));

    auto frames = conn.onReadable();
    EXPECT_TRUE(frames.empty());  // half packet, wait for more

    // Send rest
    clientSend(data.substr(20));

    frames = conn.onReadable();
    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].payload.size(), 100);
}

TEST_F(ConnectionTest, OnReadableStickyPacket) {
    auto conn = makeServerConn();

    // Send two frames concatenated
    auto d1 = FrameCodec::encode(1001, 0, 1, "first");
    auto d2 = FrameCodec::encode(2001, 0, 2, "second");
    clientSend(d1 + d2);

    auto frames = conn.onReadable();
    ASSERT_EQ(frames.size(), 2);
    EXPECT_EQ(frames[0].payload, "first");
    EXPECT_EQ(frames[1].payload, "second");
}

TEST_F(ConnectionTest, PeerCloseTransitionsState) {
    auto conn = makeServerConn();
    close(client_fd);
    client_fd = -1;

    auto frames = conn.onReadable();
    EXPECT_TRUE(frames.empty());
    EXPECT_EQ(conn.state(), ConnectionState::CLOSING);
}

TEST_F(ConnectionTest, SendFrameQueuesData) {
    auto conn = makeServerConn();

    conn.sendFrame(1002, 0, 1, "response");
    EXPECT_TRUE(conn.hasPendingWrites());

    // Flush send buffer to socket
    conn.onWritable();

    // Read from client side
    char buf[256];
    ssize_t n = read(client_fd, buf, sizeof(buf));
    ASSERT_GT(n, 0);

    // Verify it's a valid frame
    Buffer temp;
    temp.write(buf, n);
    auto frame = FrameCodec::decode(temp);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->message_id, 1002);
    EXPECT_EQ(frame->payload, "response");
}

TEST_F(ConnectionTest, SendQueueLimitClosesConnection) {
    auto conn = makeServerConn();

    // Fill send queue past limit
    std::string big(FrameCodec::kMaxPayload, 'Z');
    for (int i = 0; i < 100; ++i) {
        conn.sendFrame(4001, 0, i, big);
        if (conn.state() == ConnectionState::CLOSING) break;
    }
    EXPECT_EQ(conn.state(), ConnectionState::CLOSING);
}

TEST_F(ConnectionTest, IllegalMessagesCauseBan) {
    auto conn = makeServerConn();

    // Craft a frame with invalid total_length
    char bad[18] = {};
    *reinterpret_cast<uint32_t*>(bad) = htonl(4);  // total_length < 18

    // Send multiple illegal frames
    for (uint32_t i = 0; i < Connection::kMaxIllegalCount; ++i) {
        clientSend(std::string(bad, sizeof(bad)));
        conn.onReadable();
    }

    EXPECT_TRUE(conn.isBanned());
    EXPECT_EQ(conn.state(), ConnectionState::CLOSING);
}

TEST_F(ConnectionTest, RateLimitExceeded) {
    auto conn = makeServerConn();

    // Rapidly consume the rate limit
    bool limited = false;
    for (int i = 0; i < 200; ++i) {
        if (conn.rateLimitExceeded(100)) {
            limited = true;
            break;
        }
    }
    EXPECT_TRUE(limited);
}

TEST_F(ConnectionTest, UpdateActiveResetsTimeout) {
    auto conn = makeServerConn();
    auto data = FrameCodec::encode(1101, 0, 0, "");
    clientSend(data);

    auto before_ms = std::chrono::duration_cast<Milliseconds>(
        Clock::now().time_since_epoch()).count();

    conn.onReadable();

    auto after_ms = std::chrono::duration_cast<Milliseconds>(
        Clock::now().time_since_epoch()).count();

    EXPECT_FALSE(conn.isTimeout(after_ms, 1000));
}

TEST_F(ConnectionTest, CloseWithoutPendingWritesGoesToClosing) {
    auto conn = makeServerConn();
    conn.close();
    EXPECT_EQ(conn.state(), ConnectionState::CLOSING);
    EXPECT_TRUE(conn.shouldCleanup());  // no pending writes, ready to go
}

TEST_F(ConnectionTest, CloseWithPendingWritesStaysInClosing) {
    auto conn = makeServerConn();
    conn.sendFrame(1002, 0, 0, "data");
    conn.close();
    EXPECT_EQ(conn.state(), ConnectionState::CLOSING);
    EXPECT_TRUE(conn.hasPendingWrites());
    EXPECT_FALSE(conn.shouldCleanup());  // pending writes, not ready
}
