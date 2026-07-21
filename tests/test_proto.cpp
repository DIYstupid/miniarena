#include <gtest/gtest.h>
#include "messages.pb.h"

TEST(ProtoTest, MessageIdsAreDefined) {
    // Verify the proto library links and messages can be constructed
    miniarena::LoginRequest req;
    req.set_username("test_user");
    req.set_password("test_pass");

    EXPECT_EQ(req.username(), "test_user");
    EXPECT_EQ(req.password(), "test_pass");
}

TEST(ProtoTest, MessageHeaderSerialization) {
    miniarena::MessageHeader header;
    header.set_message_id(1001);
    header.set_sequence(42);
    header.set_session_id(0xDEADBEEF);
    header.set_client_timestamp_ms(1234567890);

    std::string serialized;
    ASSERT_TRUE(header.SerializeToString(&serialized));
    EXPECT_FALSE(serialized.empty());

    miniarena::MessageHeader parsed;
    ASSERT_TRUE(parsed.ParseFromString(serialized));
    EXPECT_EQ(parsed.message_id(), 1001);
    EXPECT_EQ(parsed.sequence(), 42);
    EXPECT_EQ(parsed.session_id(), 0xDEADBEEF);
    EXPECT_EQ(parsed.client_timestamp_ms(), 1234567890);
}

TEST(ProtoTest, PlayerStateDefaults) {
    miniarena::PlayerState state;
    EXPECT_EQ(state.player_id(), 0);
    EXPECT_FLOAT_EQ(state.position_x(), 0.0f);
    EXPECT_FLOAT_EQ(state.position_y(), 0.0f);
    EXPECT_EQ(state.hp(), 0);
    EXPECT_EQ(state.max_hp(), 0);
    EXPECT_FALSE(state.alive());
}
