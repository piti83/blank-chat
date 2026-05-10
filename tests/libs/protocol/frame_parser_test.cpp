#include <vector>

#include <gtest/gtest.h>

#include <protocol/frame.h>
#include <protocol/frame_parser.h>

namespace bc::protocol {

class FrameParserTest : public ::testing::Test
{
protected:
    MailboxID testId;

    void SetUp() override
    {
        testId.Fill(0xBB);
    }
};

TEST_F(FrameParserTest, ParsesCompletePushFrameInOneGo)
{
    Payload data = {0x01, 0x02, 0x03, 0x04};
    auto originalFrame = Frame::CreatePush(testId, std::move(data));
    auto serialized = originalFrame.Serialize();

    FrameParser parser;
    std::size_t consumed = parser.FeedBytes(serialized);

    EXPECT_EQ(consumed, serialized.size());
    EXPECT_FALSE(parser.HasError());

    auto extracted = parser.TryExtractFrame();
    ASSERT_TRUE(extracted.has_value());

    EXPECT_EQ(extracted->GetActionType(), ActionType::PUSH);
    EXPECT_EQ(extracted->GetMailboxID(), testId);
    EXPECT_EQ(extracted->GetPayloadLength(), 4);
    EXPECT_EQ(extracted->GetPayload(), (Payload{0x01, 0x02, 0x03, 0x04}));
}

TEST_F(FrameParserTest, ParsesCompletePollFrameInOneGo)
{
    auto originalFrame = Frame::CreatePoll(testId);
    auto serialized = originalFrame.Serialize();

    FrameParser parser;
    std::size_t consumed = parser.FeedBytes(serialized);

    EXPECT_EQ(consumed, serialized.size());
    EXPECT_FALSE(parser.HasError());

    auto extracted = parser.TryExtractFrame();
    ASSERT_TRUE(extracted.has_value());

    EXPECT_EQ(extracted->GetActionType(), ActionType::POLL);
    EXPECT_EQ(extracted->GetMailboxID(), testId);
    EXPECT_EQ(extracted->GetPayloadLength(), 0);
    EXPECT_TRUE(extracted->GetPayload().empty());
}

TEST_F(FrameParserTest, ParsesFrameByteByByteSimulatingTcpFragmentation)
{
    Payload data = {0xDE, 0xAD, 0xBE, 0xEF};
    auto serialized = Frame::CreatePush(testId, std::move(data)).Serialize();

    FrameParser parser;

    for (std::size_t i = 0; i < serialized.size(); ++i) {
        std::span<const std::uint8_t> chunk(&serialized[i], 1);
        std::size_t consumed = parser.FeedBytes(chunk);

        EXPECT_EQ(consumed, 1);
        EXPECT_FALSE(parser.HasError());

        if (i < serialized.size() - 1) {
            EXPECT_FALSE(parser.TryExtractFrame().has_value());
        }
    }

    auto extracted = parser.TryExtractFrame();
    ASSERT_TRUE(extracted.has_value());
    EXPECT_EQ(extracted->GetPayloadLength(), 4);
}

TEST_F(FrameParserTest, EntersErrorStateOnVolumetricAttack)
{
    FrameParser parser;

    std::vector<std::uint8_t> maliciousHeader;
    maliciousHeader.push_back(0x01);

    for (int i = 0; i < 16; ++i)
        maliciousHeader.push_back(0xAA);

    maliciousHeader.push_back(0xFF);
    maliciousHeader.push_back(0xFF);
    maliciousHeader.push_back(0xFF);
    maliciousHeader.push_back(0xFF);

    std::vector<std::uint8_t> networkStream = maliciousHeader;
    networkStream.push_back(0x99);
    networkStream.push_back(0x88);

    std::size_t consumed = parser.FeedBytes(networkStream);

    EXPECT_EQ(consumed, 21);
    EXPECT_TRUE(parser.HasError());
    EXPECT_FALSE(parser.TryExtractFrame().has_value());

    std::vector<std::uint8_t> moreBytes = {0x00, 0x11};
    std::size_t consumedLater = parser.FeedBytes(moreBytes);

    EXPECT_EQ(consumedLater, 0);
    EXPECT_TRUE(parser.HasError());
}

TEST_F(FrameParserTest, HandlesMultipleFramesInSingleBuffer)
{
    auto pollFrame = Frame::CreatePoll(testId).Serialize();
    auto pushFrame = Frame::CreatePush(testId, {0xFF}).Serialize();

    std::vector<std::uint8_t> stream;
    stream.insert(stream.end(), pollFrame.begin(), pollFrame.end());
    stream.insert(stream.end(), pushFrame.begin(), pushFrame.end());

    FrameParser parser;

    std::size_t consumed1 = parser.FeedBytes(stream);
    EXPECT_EQ(consumed1, pollFrame.size());
    EXPECT_FALSE(parser.HasError());

    auto firstExtracted = parser.TryExtractFrame();
    ASSERT_TRUE(firstExtracted.has_value());
    EXPECT_EQ(firstExtracted->GetActionType(), ActionType::POLL);

    std::span<const std::uint8_t> remaining(stream.data() + consumed1, stream.size() - consumed1);
    std::size_t consumed2 = parser.FeedBytes(remaining);

    EXPECT_EQ(consumed2, pushFrame.size());

    auto secondExtracted = parser.TryExtractFrame();
    ASSERT_TRUE(secondExtracted.has_value());
    EXPECT_EQ(secondExtracted->GetActionType(), ActionType::PUSH);
}

TEST_F(FrameParserTest, TryExtractReturnsNulloptWhenFrameNotReady)
{
    Payload data = {0xAA};
    auto serialized = Frame::CreatePush(testId, std::move(data)).Serialize();

    FrameParser parser;

    std::span<const std::uint8_t> halfHeader(serialized.data(), 10);
    parser.FeedBytes(halfHeader);

    EXPECT_FALSE(parser.TryExtractFrame().has_value());
    EXPECT_FALSE(parser.HasError());
}

TEST_F(FrameParserTest, IgnoresUnknownActionTypesAndResets)
{
    std::vector<std::uint8_t> badHeader;
    badHeader.push_back(0x99);

    for (int i = 0; i < 16; ++i)
        badHeader.push_back(0xBB);

    badHeader.push_back(0x00);
    badHeader.push_back(0x00);
    badHeader.push_back(0x00);
    badHeader.push_back(0x00);

    FrameParser parser;
    parser.FeedBytes(badHeader);

    auto extracted = parser.TryExtractFrame();
    EXPECT_FALSE(extracted.has_value());

    EXPECT_FALSE(parser.TryExtractFrame().has_value());
}

} // namespace bc::protocol
