#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace miniarena {

// A simple ring-buffer (actually a gap buffer) for network I/O.
// Read and write positions advance; compact() reclaims space.
class Buffer {
public:
    explicit Buffer(size_t initial = 4096);

    // --- Write ---
    size_t write(const char* data, size_t len);
    ssize_t readFromFd(int fd);  // read() from fd into writable space

    // --- Read ---
    size_t read(char* data, size_t len);
    ssize_t writeToFd(int fd);   // write() readable data to fd

    // --- Query ---
    [[nodiscard]] const char* readPtr()  const noexcept;
    [[nodiscard]] char*       writePtr() noexcept;
    [[nodiscard]] size_t      readable() const noexcept;
    [[nodiscard]] size_t      writable() const noexcept;
    [[nodiscard]] size_t      capacity() const noexcept;

    // --- Management ---
    void compact();                   // move unread data to front
    void ensureWrite(size_t len);     // guarantee writable >= len
    void skip(size_t len);            // discard len bytes from read position
    void reset();                     // reset read/write to zero

private:
    std::vector<char> buf_;
    size_t read_idx_  = 0;
    size_t write_idx_ = 0;

    void grow(size_t min_capacity);
};

}  // namespace miniarena
