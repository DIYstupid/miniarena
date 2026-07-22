#include <gtest/gtest.h>
#include "network/buffer.h"

using namespace miniarena;

TEST(BufferTest, EmptyOnCreation) {
    Buffer buf;
    EXPECT_EQ(buf.readable(), 0);
    EXPECT_GT(buf.writable(), 0);
    EXPECT_GT(buf.capacity(), 0);
}

TEST(BufferTest, WriteAndRead) {
    Buffer buf;
    const char* data = "hello";
    size_t n = buf.write(data, 5);
    EXPECT_EQ(n, 5);
    EXPECT_EQ(buf.readable(), 5);

    char out[10] = {};
    n = buf.read(out, sizeof(out));
    EXPECT_EQ(n, 5);
    EXPECT_STREQ(out, "hello");
    EXPECT_EQ(buf.readable(), 0);
}

TEST(BufferTest, PartialRead) {
    Buffer buf;
    buf.write("abcdef", 6);
    EXPECT_EQ(buf.readable(), 6);

    char out[4] = {};
    size_t n = buf.read(out, 3);
    EXPECT_EQ(n, 3);
    EXPECT_EQ(buf.readable(), 3);
    EXPECT_EQ(std::string(out, 3), "abc");

    n = buf.read(out, sizeof(out));
    EXPECT_EQ(n, 3);
    EXPECT_EQ(std::string(out, 3), "def");
    EXPECT_EQ(buf.readable(), 0);
}

TEST(BufferTest, CompactMovesData) {
    Buffer buf(16);
    buf.write("1234567890", 10);
    EXPECT_EQ(buf.readable(), 10);

    char out[6] = {};
    buf.read(out, 5);
    EXPECT_EQ(std::string(out, 5), "12345");
    EXPECT_EQ(buf.readable(), 5);

    // After compact, data should be at front
    buf.compact();
    EXPECT_EQ(buf.readable(), 5);
    EXPECT_EQ(std::string(buf.readPtr(), 5), "67890");
}

TEST(BufferTest, EnsureWriteGrows) {
    Buffer buf(64);
    EXPECT_EQ(buf.capacity(), 64);
    buf.ensureWrite(1024);
    EXPECT_GE(buf.writable(), 1024);
}

TEST(BufferTest, ResetClearsAll) {
    Buffer buf;
    buf.write("data", 4);
    EXPECT_EQ(buf.readable(), 4);
    buf.reset();
    EXPECT_EQ(buf.readable(), 0);
}

TEST(BufferTest, SkipAdvancesRead) {
    Buffer buf;
    buf.write("abcdef", 6);
    buf.skip(2);
    EXPECT_EQ(buf.readable(), 4);
    EXPECT_EQ(buf.readPtr()[0], 'c');
}

TEST(BufferTest, WriteLargeData) {
    Buffer buf;
    std::string data(10000, 'X');
    size_t n = buf.write(data.data(), data.size());
    EXPECT_EQ(n, data.size());
    EXPECT_EQ(buf.readable(), data.size());
}

TEST(BufferTest, ReadToFdAndReadFromFd) {
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);

    Buffer send_buf;
    send_buf.write("hello pipe", 10);

    ssize_t written = send_buf.writeToFd(pipefd[1]);
    EXPECT_EQ(written, 10);

    close(pipefd[1]);

    Buffer recv_buf;
    ssize_t nread = recv_buf.readFromFd(pipefd[0]);
    EXPECT_EQ(nread, 10);
    EXPECT_EQ(recv_buf.readable(), 10);
    EXPECT_EQ(std::string(recv_buf.readPtr(), 10), "hello pipe");

    close(pipefd[0]);
}
