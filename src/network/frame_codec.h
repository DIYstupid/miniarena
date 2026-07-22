#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "network_types.h"

namespace miniarena {

struct Frame {
    uint32_t    total_length = 0;  // includes header (18 bytes)
    uint32_t    message_id   = 0;
    uint16_t    flags        = 0;
    uint64_t    sequence     = 0;
    std::string payload;
};

class Buffer;  // forward decl

// Encodes/decodes the MiniArena wire frame format:
//   [total_length:4] [message_id:4] [flags:2] [sequence:8] [payload:N]
// All header fields are big-endian.
class FrameCodec {
public:
    static constexpr size_t kHeaderSize  = net::kHeaderSize;
    static constexpr size_t kMaxPayload  = net::kMaxPayload;

    // Encode a Frame into a byte string.
    static std::string encode(const Frame& frame);
    static std::string encode(uint32_t msg_id, uint16_t flags,
                              uint64_t seq, std::string_view payload);

    // Decode one frame from a Buffer.
    // Returns nullopt if a complete frame is not yet available (half-packet).
    // Throws std::runtime_error on an invalid frame (sticky-packet attack).
    static std::optional<Frame> decode(Buffer& buf);

    // Low-level: write big-endian header fields into a buffer
    static void writeHeader(char* dst, uint32_t total_length,
                            uint32_t msg_id, uint16_t flags, uint64_t seq);
    // Low-level: parse header from a raw 18-byte pointer
    static void parseHeader(const char* src, uint32_t& total_length,
                            uint32_t& msg_id, uint16_t& flags, uint64_t& seq);
};

}  // namespace miniarena
