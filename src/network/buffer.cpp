#include "buffer.h"

#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace miniarena {

Buffer::Buffer(size_t initial) {
    buf_.resize(std::max(initial, size_t(64)));
}

size_t Buffer::write(const char* data, size_t len) {
    if (len == 0) return 0;
    ensureWrite(len);
    size_t n = std::min(len, writable());
    std::memcpy(writePtr(), data, n);
    write_idx_ += n;
    return n;
}

ssize_t Buffer::readFromFd(int fd) {
    ensureWrite(4096);
    size_t space = writable();
    if (space == 0) return 0;
    ssize_t n = ::read(fd, writePtr(), space);
    if (n > 0) {
        write_idx_ += static_cast<size_t>(n);
    }
    return n;
}

size_t Buffer::read(char* data, size_t len) {
    size_t n = std::min(len, readable());
    if (n > 0) {
        std::memcpy(data, readPtr(), n);
        read_idx_ += n;
    }
    if (read_idx_ == write_idx_) {
        reset();
    }
    return n;
}

ssize_t Buffer::writeToFd(int fd) {
    if (readable() == 0) return 0;
    ssize_t n = ::write(fd, readPtr(), readable());
    if (n > 0) {
        read_idx_ += static_cast<size_t>(n);
        if (read_idx_ == write_idx_) {
            reset();
        }
    }
    return n;
}

const char* Buffer::readPtr() const noexcept {
    return buf_.data() + read_idx_;
}

char* Buffer::writePtr() noexcept {
    return buf_.data() + write_idx_;
}

size_t Buffer::readable() const noexcept {
    return write_idx_ - read_idx_;
}

size_t Buffer::writable() const noexcept {
    return buf_.size() - write_idx_;
}

size_t Buffer::capacity() const noexcept {
    return buf_.size();
}

void Buffer::compact() {
    if (read_idx_ > 0 && readable() > 0) {
        size_t n = readable();
        std::memmove(buf_.data(), readPtr(), n);
        read_idx_ = 0;
        write_idx_ = n;
    } else if (read_idx_ > 0) {
        reset();
    }
}

void Buffer::ensureWrite(size_t len) {
    if (writable() >= len) return;
    compact();
    if (writable() >= len) return;
    grow(write_idx_ + len);
}

void Buffer::skip(size_t len) {
    size_t n = std::min(len, readable());
    read_idx_ += n;
    if (read_idx_ == write_idx_) {
        reset();
    }
}

void Buffer::reset() {
    read_idx_ = 0;
    write_idx_ = 0;
}

void Buffer::grow(size_t min_capacity) {
    size_t new_cap = buf_.size() * 2;
    while (new_cap < min_capacity) {
        new_cap *= 2;
    }
    buf_.resize(new_cap);
}

}  // namespace miniarena
