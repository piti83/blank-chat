#include <frame.h>
#include <gtest/gtest.h>

namespace bc::protocol::frame {

class FrameTest : public ::testing::Test
{
protected:
    MailboxID defaultId;

    void SetUp() override
    {
        defaultId.Fill(0xAA);
    }
};

TEST_F(FrameTest, CreatePushInitializesProperly)
{
    Payload data = {0x01, 0x02, 0x03};

    auto frame = Frame::CreatePush(defaultId, std::move(data));

    EXPECT_EQ(frame.GetActionType(), ActionType::PUSH);
    EXPECT_EQ(frame.GetMailboxID(), defaultId);
    EXPECT_EQ(frame.GetPayloadLength(), 3);

    EXPECT_EQ(frame.GetPayload(), (Payload{0x01, 0x02, 0x03}));
}

TEST_F(FrameTest, CreatePollInitializesProperly)
{
    auto frame = Frame::CreatePoll(defaultId);

    EXPECT_EQ(frame.GetActionType(), ActionType::POLL);
    EXPECT_EQ(frame.GetMailboxID(), defaultId);
    EXPECT_EQ(frame.GetPayloadLength(), 0);
    EXPECT_TRUE(frame.GetPayload().empty());
}

TEST_F(FrameTest, MoveConstructorTransfersOwnershipWithoutLeaks)
{
    Payload data = {0xFF, 0xEE, 0xDD};
    auto original = Frame::CreatePush(defaultId, std::move(data));

    auto moved = Frame(std::move(original));

    EXPECT_EQ(moved.GetActionType(), ActionType::PUSH);
    EXPECT_EQ(moved.GetPayloadLength(), 3);
    EXPECT_EQ(moved.GetPayload(), (Payload{0xFF, 0xEE, 0xDD}));

    EXPECT_TRUE(original.GetPayload().empty());
}

TEST_F(FrameTest, SerializePushFrameProducesCorrectBuffer)
{
    MailboxID id(std::array<std::uint8_t, mailboxIdSize>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
                                                         14, 15, 16});
    Payload data = {0xAA, 0xBB, 0xCC};

    auto frame = Frame::CreatePush(id, std::move(data));
    auto buffer = frame.Serialize();

    RawFrame expected;
    expected.push_back(0x01);
    expected.insert(expected.end(), id.begin(), id.end());

    expected.push_back(0x03);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x00);

    expected.push_back(0xAA);
    expected.push_back(0xBB);
    expected.push_back(0xCC);

    EXPECT_EQ(buffer, expected);
}

TEST_F(FrameTest, SerializePollFrameProducesCorrectBuffer)
{
    auto frame = Frame::CreatePoll(defaultId);
    auto buffer = frame.Serialize();

    RawFrame expected;
    expected.push_back(0x02);
    expected.insert(expected.end(), defaultId.begin(), defaultId.end());

    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x00);
    expected.push_back(0x00);

    EXPECT_EQ(buffer, expected);
}

TEST_F(FrameTest, SerializeVerifiesLittleEndianEncoding)
{
    Payload data(300, 0x77);

    auto frame = Frame::CreatePush(defaultId, std::move(data));
    auto buffer = frame.Serialize();

    ASSERT_GE(buffer.size(), 1 + mailboxIdSize + 4);

    EXPECT_EQ(buffer[17], 0x2C);
    EXPECT_EQ(buffer[18], 0x01);
    EXPECT_EQ(buffer[19], 0x00);
    EXPECT_EQ(buffer[20], 0x00);
}

TEST_F(FrameTest, ExtractPayloadMovesDataWithoutCopyAndLeavesFrameEmpty)
{
    Payload originalData = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
    auto frame = Frame::CreatePush(defaultId, std::move(originalData));

    ASSERT_EQ(frame.GetPayloadLength(), 5);
    ASSERT_FALSE(frame.GetPayload().empty());

    Payload extractedData = std::move(frame).ExtractPayload();

    EXPECT_EQ(extractedData.size(), 5);
    EXPECT_EQ(extractedData, (Payload{0xDE, 0xAD, 0xBE, 0xEF, 0x42}));

    EXPECT_TRUE(frame.GetPayload().empty());
}

} // namespace bc::protocol::frame
