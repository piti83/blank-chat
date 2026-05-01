#include <gtest/gtest.h>
#include <message_broker.h>

namespace bc::domain::server {

class MessageBrokerTest : public ::testing::Test
{
protected:
    bc::protocol::frame::MailboxID aliceId;
    bc::protocol::frame::MailboxID bobId;

    void SetUp() override
    {
        aliceId.Fill(0xAA);
        bobId.Fill(0xBB);
    }
};

TEST_F(MessageBrokerTest, ProcessPollOnEmptyBrokerReturnsNullopt)
{
    MessageBroker broker;
    auto result = broker.ProcessPoll(aliceId);
    EXPECT_FALSE(result.has_value());
}

TEST_F(MessageBrokerTest, PushAndPollReconstructsFrameProperly)
{
    MessageBroker broker;
    bc::protocol::frame::Payload data = {0x01, 0x02, 0x03};
    auto frameIn = bc::protocol::frame::Frame::CreatePush(aliceId, std::move(data));

    broker.ProcessPush(std::move(frameIn));

    auto result = broker.ProcessPoll(aliceId);

    ASSERT_TRUE(result.has_value());

    auto frameOut = std::move(result.value());

    EXPECT_EQ(frameOut.GetActionType(), bc::protocol::frame::ActionType::PUSH);
    EXPECT_EQ(frameOut.GetMailboxID(), aliceId);
    EXPECT_EQ(frameOut.GetPayloadLength(), 3);
    EXPECT_EQ(frameOut.GetPayload(), (bc::protocol::frame::Payload{0x01, 0x02, 0x03}));

    auto emptyResult = broker.ProcessPoll(aliceId);
    EXPECT_FALSE(emptyResult.has_value());
}

TEST_F(MessageBrokerTest, MessagesAreProcessedInStrictFIFOOrder)
{
    MessageBroker broker;

    broker.ProcessPush(bc::protocol::frame::Frame::CreatePush(aliceId, {0x11}));
    broker.ProcessPush(bc::protocol::frame::Frame::CreatePush(aliceId, {0x22}));
    broker.ProcessPush(bc::protocol::frame::Frame::CreatePush(aliceId, {0x33}));

    auto res1 = broker.ProcessPoll(aliceId);
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(res1.value().GetPayload(), (bc::protocol::frame::Payload{0x11}));

    auto res2 = broker.ProcessPoll(aliceId);
    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(res2.value().GetPayload(), (bc::protocol::frame::Payload{0x22}));

    auto res3 = broker.ProcessPoll(aliceId);
    ASSERT_TRUE(res3.has_value());
    EXPECT_EQ(res3.value().GetPayload(), (bc::protocol::frame::Payload{0x33}));

    EXPECT_FALSE(broker.ProcessPoll(aliceId).has_value());
}

TEST_F(MessageBrokerTest, MailboxesAreStrictlyIsolated)
{
    MessageBroker broker;

    broker.ProcessPush(bc::protocol::frame::Frame::CreatePush(bobId, {0xBE, 0xEF}));

    auto aliceResult = broker.ProcessPoll(aliceId);
    EXPECT_FALSE(aliceResult.has_value());

    auto bobResult = broker.ProcessPoll(bobId);
    ASSERT_TRUE(bobResult.has_value());
    EXPECT_EQ(bobResult.value().GetPayload(), (bc::protocol::frame::Payload{0xBE, 0xEF}));
}

} // namespace bc::domain::server
