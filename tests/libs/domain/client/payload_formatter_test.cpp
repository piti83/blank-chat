#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include <client/client_types.h>
#include <client/payload_formatter.h>

namespace bc::domain::client::test {

class PayloadFormatterTest : public ::testing::Test
{
protected:
    bc::crypto::PublicKeyType dummyKey{};
    std::array<std::uint8_t, cryptoSignBytes> dummySig{};

    void SetUp() override
    {
        dummyKey.fill(0xAA);
        dummySig.fill(0xBB);
    }
};

TEST_F(PayloadFormatterTest, BuildAndParseTextMessage)
{
    std::vector<std::uint8_t> text = {'H', 'e', 'l', 'l', 'o'};
    auto payload = PayloadFormatter::BuildTextMessage(text);

    ASSERT_FALSE(payload.empty());
    EXPECT_EQ(payload[0], static_cast<std::uint8_t>(PayloadOpcode::TEXT_MESSAGE));
    EXPECT_EQ(payload.size(), 1 + text.size());

    auto parsedOpt = PayloadFormatter::ParseTextMessage(payload);
    ASSERT_TRUE(parsedOpt.has_value());
    EXPECT_TRUE(std::ranges::equal(*parsedOpt, text));
}

TEST_F(PayloadFormatterTest, BuildAndParsePfsRotateRequest)
{
    auto payload = PayloadFormatter::BuildPfsRotateRequest(dummyKey, dummySig);

    ASSERT_FALSE(payload.empty());
    EXPECT_EQ(payload[0], static_cast<std::uint8_t>(PayloadOpcode::PFS_ROTATE_REQUEST));
    EXPECT_EQ(payload.size(), 1 + dummyKey.size() + dummySig.size());

    auto parsedOpt = PayloadFormatter::ParsePfsRotateRequest(payload);
    ASSERT_TRUE(parsedOpt.has_value());
    EXPECT_TRUE(std::ranges::equal(parsedOpt->ephemeralPublicKey, dummyKey));
    EXPECT_TRUE(std::ranges::equal(parsedOpt->signature, dummySig));
}

TEST_F(PayloadFormatterTest, BuildAndParsePfsRotateAck)
{
    auto payload = PayloadFormatter::BuildPfsRotateAck(dummyKey, dummySig);

    ASSERT_FALSE(payload.empty());
    EXPECT_EQ(payload[0], static_cast<std::uint8_t>(PayloadOpcode::PFS_ROTATE_ACK));
    EXPECT_EQ(payload.size(), 1 + dummyKey.size() + dummySig.size());

    auto parsedOpt = PayloadFormatter::ParsePfsRotateAck(payload);
    ASSERT_TRUE(parsedOpt.has_value());
    EXPECT_TRUE(std::ranges::equal(parsedOpt->ephemeralPublicKey, dummyKey));
    EXPECT_TRUE(std::ranges::equal(parsedOpt->signature, dummySig));
}

TEST_F(PayloadFormatterTest, ExtractOpcodeReturnsNulloptOnEmptyPayload)
{
    std::vector<std::uint8_t> emptyPayload;
    EXPECT_FALSE(PayloadFormatter::ExtractOpcode(emptyPayload).has_value());
}

TEST_F(PayloadFormatterTest, ExtractOpcodeReturnsNulloptOnInvalidOpcode)
{
    std::vector<std::uint8_t> invalidPayload = {0x99, 0x01, 0x02};
    EXPECT_FALSE(PayloadFormatter::ExtractOpcode(invalidPayload).has_value());
}

TEST_F(PayloadFormatterTest, ParseTextMessageFailsOnWrongOpcode)
{
    std::vector<std::uint8_t> wrongPayload = {
        static_cast<std::uint8_t>(PayloadOpcode::PFS_ROTATE_REQUEST), 0x00};
    EXPECT_FALSE(PayloadFormatter::ParseTextMessage(wrongPayload).has_value());
}

TEST_F(PayloadFormatterTest, ParsePfsRotateRequestFailsOnWrongOpcodeAndSize)
{
    std::vector<std::uint8_t> wrongPayload = {
        static_cast<std::uint8_t>(PayloadOpcode::TEXT_MESSAGE), 0x00};
    EXPECT_FALSE(PayloadFormatter::ParsePfsRotateRequest(wrongPayload).has_value());

    std::vector<std::uint8_t> wrongSize = {
        static_cast<std::uint8_t>(PayloadOpcode::PFS_ROTATE_REQUEST), 0x00, 0x01};
    EXPECT_FALSE(PayloadFormatter::ParsePfsRotateRequest(wrongSize).has_value());
}

TEST_F(PayloadFormatterTest, ParsePfsRotateAckFailsOnWrongOpcodeAndSize)
{
    std::vector<std::uint8_t> wrongPayload = {
        static_cast<std::uint8_t>(PayloadOpcode::TEXT_MESSAGE), 0x00};
    EXPECT_FALSE(PayloadFormatter::ParsePfsRotateAck(wrongPayload).has_value());

    std::vector<std::uint8_t> wrongSize = {static_cast<std::uint8_t>(PayloadOpcode::PFS_ROTATE_ACK),
                                           0x00, 0x01};
    EXPECT_FALSE(PayloadFormatter::ParsePfsRotateAck(wrongSize).has_value());
}

} // namespace bc::domain::client::test
