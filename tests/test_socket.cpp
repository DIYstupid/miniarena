#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include "network/socket.h"

using namespace miniarena;

TEST(SocketTest, CreateTcpIsValid) {
    auto sock = Socket::createTcp();
    EXPECT_TRUE(sock.valid());
    EXPECT_GE(sock.fd(), 0);
}

TEST(SocketTest, DefaultIsInvalid) {
    Socket s;
    EXPECT_FALSE(s.valid());
    EXPECT_EQ(s.fd(), -1);
}

TEST(SocketTest, MoveConstruct) {
    auto s1 = Socket::createTcp();
    int fd = s1.fd();
    Socket s2(std::move(s1));
    EXPECT_FALSE(s1.valid());
    EXPECT_EQ(s1.fd(), -1);
    EXPECT_TRUE(s2.valid());
    EXPECT_EQ(s2.fd(), fd);
}

TEST(SocketTest, MoveAssign) {
    auto s1 = Socket::createTcp();
    int fd = s1.fd();
    Socket s2;
    s2 = std::move(s1);
    EXPECT_FALSE(s1.valid());
    EXPECT_TRUE(s2.valid());
    EXPECT_EQ(s2.fd(), fd);
}

TEST(SocketTest, Close) {
    auto sock = Socket::createTcp();
    EXPECT_TRUE(sock.valid());
    sock.close();
    EXPECT_FALSE(sock.valid());
    // double close should be safe
    sock.close();
    EXPECT_FALSE(sock.valid());
}

TEST(SocketTest, BindAndListen) {
    auto sock = Socket::createTcp();
    sock.setReuseAddr();
    sock.bind(0);  // OS picks port
    sock.listen();
    EXPECT_TRUE(sock.valid());
}

TEST(SocketTest, AcceptReturnsInvalidOnEmpty) {
    auto sock = Socket::createTcp();
    sock.setReuseAddr();
    sock.bind(0);
    sock.listen();
    auto client = sock.accept(nullptr);
    EXPECT_FALSE(client.valid());
}
