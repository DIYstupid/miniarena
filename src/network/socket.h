#pragma once

#include <netinet/in.h>
#include <string>

namespace miniarena {

// RAII wrapper around a file descriptor for TCP sockets.
// Non-copyable, movable. Creates non-blocking sockets by default.
class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) noexcept : fd_(fd) {}
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    // Create a non-blocking TCP socket (SOCK_STREAM | SOCK_NONBLOCK)
    static Socket createTcp();

    void setReuseAddr();
    void setNoDelay();
    void bind(uint16_t port);
    void listen(int backlog = 128);
    Socket accept(sockaddr_in* addr = nullptr);

    [[nodiscard]] int  fd()    const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

    void close();

private:
    int fd_ = -1;
};

}  // namespace miniarena
