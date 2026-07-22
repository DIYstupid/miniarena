#include "socket.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <cstring>

#include <spdlog/spdlog.h>

namespace miniarena {

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept
    : fd_(other.fd_) {
    other.fd_ = -1;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

Socket Socket::createTcp() {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        throw std::runtime_error(std::string("socket() failed: ") + strerror(errno));
    }
    return Socket(fd);
}

void Socket::setReuseAddr() {
    int opt = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        spdlog::warn("setsockopt SO_REUSEADDR failed: {}", strerror(errno));
    }
}

void Socket::setNoDelay() {
    int opt = 1;
    if (::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        spdlog::warn("setsockopt TCP_NODELAY failed: {}", strerror(errno));
    }
}

void Socket::bind(uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error(std::string("bind() failed: ") + strerror(errno));
    }
}

void Socket::listen(int backlog) {
    if (::listen(fd_, backlog) < 0) {
        throw std::runtime_error(std::string("listen() failed: ") + strerror(errno));
    }
}

Socket Socket::accept(sockaddr_in* addr) {
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = ::accept4(fd_, reinterpret_cast<sockaddr*>(&client_addr),
                              &addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return Socket();  // no pending connections
        }
        throw std::runtime_error(std::string("accept4() failed: ") + strerror(errno));
    }

    if (addr) {
        *addr = client_addr;
    }

    return Socket(client_fd);
}

void Socket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

}  // namespace miniarena
