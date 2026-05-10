#include <vector>

#include <gtest/gtest.h>

#include <protocol/mailbox_id.h>
#include <protocol/mailbox_id_hash.h>

namespace bc::protocol {

class MailboxIDTest : public ::testing::Test
{
protected:
    std::array<std::uint8_t, mailboxIdSize> testData1 = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                                         0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
                                                         0x0D, 0x0E, 0x0F, 0x10};

    std::array<std::uint8_t, mailboxIdSize> testData2 = {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA,
                                                         0x99, 0x88, 0x77, 0x66, 0x55, 0x44,
                                                         0x33, 0x22, 0x11, 0x00};
};

TEST_F(MailboxIDTest, DefaultConstructorZeroInitializesBytes)
{
    MailboxID id;
    for (const auto& byte : id) {
        EXPECT_EQ(byte, 0x00);
    }
}

TEST_F(MailboxIDTest, ArrayConstructorInitializesCorrectly)
{
    MailboxID id(testData1);
    EXPECT_TRUE(std::equal(id.begin(), id.end(), testData1.begin()));
}

TEST_F(MailboxIDTest, SpanConstructorReadsFromNetworkBufferCorrectly)
{
    std::vector<std::uint8_t> rxBuffer = {0xAA, 0xBB};
    rxBuffer.insert(rxBuffer.end(), testData2.begin(), testData2.end());
    rxBuffer.push_back(0xCC);
    std::span<const std::uint8_t, mailboxIdSize> bufferSpan(rxBuffer.data() + 2, mailboxIdSize);

    MailboxID id(bufferSpan);
    EXPECT_TRUE(std::equal(id.begin(), id.end(), testData2.begin()));
}

TEST_F(MailboxIDTest, FillModifiesAllBytes)
{
    MailboxID id;
    id.Fill(0x7F);
    for (const auto& byte : id) {
        EXPECT_EQ(byte, 0x7F);
    }
}

TEST_F(MailboxIDTest, ConstIteratorsAndDataPointersWorkProperly)
{
    MailboxID id(testData1);

    EXPECT_EQ(id.size(), 16);
    EXPECT_EQ(std::distance(id.begin(), id.end()), 16);
    EXPECT_NE(id.data(), nullptr);
    EXPECT_EQ(id.data()[0], 0x01);
    EXPECT_EQ(id.data()[15], 0x10);
}

TEST_F(MailboxIDTest, AsSpanReturnsCorrectConstView)
{
    MailboxID id(testData1);
    auto spanView = id.AsSpan();

    EXPECT_EQ(spanView.size(), mailboxIdSize);
    EXPECT_TRUE(std::equal(spanView.begin(), spanView.end(), testData1.begin()));
}

TEST_F(MailboxIDTest, EqualityOperatorsWorkCorrectly)
{
    MailboxID id1(testData1);
    MailboxID id1Copy(testData1);
    MailboxID id2(testData2);

    EXPECT_EQ(id1, id1Copy);
    EXPECT_NE(id1, id2);
}

TEST_F(MailboxIDTest, SpaceshipOperatorAllowsSorting)
{
    MailboxID idSmall;
    MailboxID idBig;
    idBig.Fill(0xFF);

    EXPECT_LT(idSmall, idBig);
    EXPECT_GT(idBig, idSmall);
    EXPECT_LE(idSmall, idSmall);
    EXPECT_GE(idBig, idBig);
}

TEST_F(MailboxIDTest, HashSpecializationGeneratesDeterministicHashes)
{
    MailboxID id1(testData1);
    MailboxID id1Copy(testData1);
    MailboxID id2(testData2);

    std::hash<MailboxID> hasher;

    EXPECT_EQ(hasher(id1), hasher(id1Copy));

    EXPECT_NE(hasher(id1), hasher(id2));
}

} // namespace bc::protocol
