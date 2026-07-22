#include "frame_codec.h"
#include "buffer.h"

#include <cstring>
#include <stdexcept>
#include <arpa/inet.h>

namespace miniarena {

void FrameCodec::writeHeader(char* dst, uint32_t total_length,
                              uint32_t msg_id, uint16_t flags, uint64_t seq) {
    uint32_t be_len = htonl(total_length);
    uint32_t be_id  = htonl(msg_id);
    uint16_t be_fl  = htons(flags);
    uint32_t seq_hi = htonl(static_cast<uint32_t>(seq >> 32));
    uint32_t seq_lo = htonl(static_cast<uint32_t>(seq & 0xFFFFFFFF));

    std::memcpy(dst,      &be_len, 4);
    std::memcpy(dst + 4,  &be_id,  4);
    std::memcpy(dst + 8,  &be_fl,  2);
    std::memcpy(dst + 10, &seq_hi, 4);
    std::memcpy(dst + 14, &seq_lo, 4);
}

void FrameCodec::parseHeader(const char* src, uint32_t& total_length,
                              uint32_t& msg_id, uint16_t& flags, uint64_t& seq) {
    uint32_t be_len, be_id, seq_hi, seq_lo;
    uint16_t be_fl;

    std::memcpy(&be_len, src,      4);
    std::memcpy(&be_id,  src + 4,  4);
    std::memcpy(&be_fl,  src + 8,  2);
    std::memcpy(&seq_hi, src + 10, 4);
    std::memcpy(&seq_lo, src + 14, 4);

    total_length = ntohl(be_len);
    msg_id       = ntohl(be_id);
    flags        = ntohs(be_fl);
    seq          = (static_cast<uint64_t>(ntohl(seq_hi)) << 32) | ntohl(seq_lo);
}

std::string FrameCodec::encode(const Frame& frame) {
    return encode(frame.message_id, frame.flags, frame.sequence, frame.payload);
}

std::string FrameCodec::encode(uint32_t msg_id, uint16_t flags,
                                uint64_t seq, std::string_view payload) {
    if (payload.size() > kMaxPayload) {
        throw std::runtime_error("FrameCodec::encode: payload exceeds max size");
    }

    uint32_t total = static_cast<uint32_t>(kHeaderSize + payload.size());
    std::string result(total, '\0');

    writeHeader(result.data(), total, msg_id, flags, seq);
    if (!payload.empty()) {
        std::memcpy(result.data() + kHeaderSize, payload.data(), payload.size());
    }

    return result;
}

std::optional<Frame> FrameCodec::decode(Buffer& buf) {
    // 1. Need at least a header
    if (buf.readable() < kHeaderSize) {
        return std::nullopt;
    }

    // 2. Peek header
    uint32_t total_length, msg_id;
    uint16_t flags;
    uint64_t seq;
    parseHeader(buf.readPtr(), total_length, msg_id, flags, seq);

    // 3. Validate total_length
    if (total_length < kHeaderSize) {
        throw std::runtime_error("FrameCodec::decode: total_length < header size");
    }
    if (total_length > kHeaderSize + kMaxPayload) {
        throw std::runtime_error("FrameCodec::decode: total_length exceeds max packet size");
    }

    // 4. Wait for complete frame
    if (buf.readable() < total_length) {
        return std::nullopt;
    }

    // 5. Read full frame
    Frame frame;
    frame.total_length = total_length;
    frame.message_id   = msg_id;
    frame.flags        = flags;
    frame.sequence     = seq;

    // Skip the header we already peeked, then read payload
    buf.skip(kHeaderSize);

    size_t payload_len = total_length - kHeaderSize;
    if (payload_len > 0) {
        frame.payload.resize(payload_len);
        buf.read(frame.payload.data(), payload_len);
    }

    return frame;
}

}  // namespace miniarena
