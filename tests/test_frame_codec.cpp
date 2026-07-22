#include <arpa/inet.h>
#include <gtest/gtest.h>
#include "network/frame_codec.h"
#include "network/buffer.h"

using namespace miniarena;

// --- Encode Tests ---

TEST(FrameCodecTest, EncodeEmptyPayload) {
    auto data = FrameCodec::encode(1001, 0, 42, "");
    EXPECT_EQ(data.size(), FrameCodec::kHeaderSize);
}

TEST(FrameCodecTest, EncodeWithPayload) {
    std::string payload = "move_data";
    auto data = FrameCodec::encode(4001, 0x0001, 0xDEADBEEFCAFEULL, payload);
    EXPECT_EQ(data.size(), FrameCodec::kHeaderSize + payload.size());
}

TEST(FrameCodecTest, EncodeFrameStruct) {
    Frame f;
    f.message_id = 2001;
    f.flags = 0;
    f.sequence = 100;
    f.payload = "test";
    auto data = FrameCodec::encode(f);
    EXPECT_EQ(data.size(), FrameCodec::kHeaderSize + 4);
}

TEST(FrameCodecTest, EncodeThrowsOnOversizePayload) {
    std::string big(FrameCodec::kMaxPayload + 1, 'X');
    EXPECT_THROW(FrameCodec::encode(1, 0, 0, big), std::runtime_error);
}

// --- Decode Tests ---

TEST(FrameCodecTest, DecodeRoundtrip) {
    auto data = FrameCodec::encode(4001, 0x0005, 0x123456789ABCDEF0ULL, "attack");
    Buffer buf;
    buf.write(data.data(), data.size());

    auto frame = FrameCodec::decode(buf);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->message_id, 4001);
    EXPECT_EQ(frame->flags, 0x0005);
    EXPECT_EQ(frame->sequence, 0x123456789ABCDEF0ULL);
    EXPECT_EQ(frame->payload, "attack");
    EXPECT_EQ(frame->total_length, FrameCodec::kHeaderSize + 6);
}

TEST(FrameCodecTest, DecodeEmptyPayload) {
    auto data = FrameCodec::encode(1101, 0, 1, "");
    Buffer buf;
    buf.write(data.data(), data.size());

    auto frame = FrameCodec::decode(buf);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->payload, "");
    EXPECT_EQ(frame->total_length, FrameCodec::kHeaderSize);
}

TEST(FrameCodecTest, DecodeIncompleteHeader) {
    Buffer buf;
    buf.write("short", 5);  // less than 18 bytes

    auto frame = FrameCodec::decode(buf);
    EXPECT_FALSE(frame.has_value());
    EXPECT_EQ(buf.readable(), 5);  // data preserved
}

TEST(FrameCodecTest, DecodeIncompleteFrame) {
    // Encode a frame, then feed only part of it
    auto data = FrameCodec::encode(4001, 0, 0, "long_payload_here");
    ASSERT_GT(data.size(), FrameCodec::kHeaderSize);

    // Write header + 3 bytes of payload (not the full payload)
    Buffer buf;
    buf.write(data.data(), FrameCodec::kHeaderSize + 3);

    auto frame = FrameCodec::decode(buf);
    EXPECT_FALSE(frame.has_value());
}

TEST(FrameCodecTest, DecodeInvalidTotalLengthTooSmall) {
    // Craft a frame with total_length < header size
    Buffer buf;
    buf.write("\x00\x00\x00\x04", 4);  // total_length = 4
    // Pad to 18 bytes
    std::string pad(14, '\0');
    buf.write(pad.data(), pad.size());

    EXPECT_THROW(FrameCodec::decode(buf), std::runtime_error);
}

TEST(FrameCodecTest, DecodeInvalidTotalLengthTooLarge) {
    // Craft a frame with huge total_length
    Buffer buf;
    // total_length = header + max_payload + 1 (too big)
    uint32_t bad_len = htonl(FrameCodec::kHeaderSize + FrameCodec::kMaxPayload + 1);
    buf.write(reinterpret_cast<const char*>(&bad_len), 4);
    std::string pad(14, '\0');
    buf.write(pad.data(), pad.size());

    EXPECT_THROW(FrameCodec::decode(buf), std::runtime_error);
}

TEST(FrameCodecTest, DecodeMaxPayload) {
    std::string big_payload(FrameCodec::kMaxPayload, 'Z');
    auto data = FrameCodec::encode(6001, 0, 99, big_payload);
    Buffer buf;
    buf.write(data.data(), data.size());

    auto frame = FrameCodec::decode(buf);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->payload.size(), FrameCodec::kMaxPayload);
    EXPECT_EQ(frame->message_id, 6001);
    EXPECT_EQ(frame->sequence, 99);
}

TEST(FrameCodecTest, DecodeMultipleFrames) {
    // Encode two frames back to back, feed to one buffer
    auto d1 = FrameCodec::encode(4001, 0, 1, "first");
    auto d2 = FrameCodec::encode(4002, 0, 2, "second");

    Buffer buf;
    buf.write(d1.data(), d1.size());
    buf.write(d2.data(), d2.size());

    auto f1 = FrameCodec::decode(buf);
    ASSERT_TRUE(f1.has_value());
    EXPECT_EQ(f1->payload, "first");
    EXPECT_EQ(f1->sequence, 1);

    auto f2 = FrameCodec::decode(buf);
    ASSERT_TRUE(f2.has_value());
    EXPECT_EQ(f2->payload, "second");
    EXPECT_EQ(f2->sequence, 2);

    EXPECT_EQ(buf.readable(), 0);
}
