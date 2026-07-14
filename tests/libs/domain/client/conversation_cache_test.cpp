#include <filesystem>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <client/conversation_cache.h>

namespace bc::domain::client::test {

class ConversationCacheTest : public ::testing::Test
{
protected:
    std::filesystem::path testCacheDir = "test_conversation_cache_dir";
    std::string testAlias = "alice";

    void SetUp() override
    {
        std::error_code ec;
        if (std::filesystem::exists(testCacheDir)) {
            std::filesystem::remove_all(testCacheDir, ec);
        }
    }

    void TearDown() override
    {
        std::error_code ec;
        if (std::filesystem::exists(testCacheDir)) {
            std::filesystem::remove_all(testCacheDir, ec);
        }
    }
};

TEST_F(ConversationCacheTest, InitializeCreatesDirectory)
{
    ConversationCache cache;
    EXPECT_TRUE(cache.Initialize(testCacheDir));
    EXPECT_TRUE(std::filesystem::exists(testCacheDir));
    EXPECT_TRUE(std::filesystem::is_directory(testCacheDir));
}

TEST_F(ConversationCacheTest, AppendsAndLoadsHistoryCorrectly)
{
    ConversationCache cache;
    ASSERT_TRUE(cache.Initialize(testCacheDir));

    CacheEntry msg1{.id = "hash_123",
                    .timestamp = 1600000000,
                    .direction = MessageDirection::OUTBOUND,
                    .alias = testAlias,
                    .status = MessageStatus::PENDING_ACK,
                    .payload = bc::protocol::Payload{'H', 'e', 'y'}};

    CacheEntry msg2{.id = "hash_456",
                    .timestamp = 1600000010,
                    .direction = MessageDirection::INBOUND,
                    .alias = testAlias,
                    .status = MessageStatus::DELIVERED,
                    .payload = bc::protocol::Payload{'H', 'i'}};

    cache.AppendMessage(msg1);
    cache.AppendMessage(msg2);

    auto history = cache.LoadHistory(testAlias);
    ASSERT_EQ(history.size(), 2);

    EXPECT_EQ(history[0].id, "hash_123");
    EXPECT_EQ(history[0].direction, MessageDirection::OUTBOUND);
    EXPECT_EQ(history[0].status, MessageStatus::PENDING_ACK);
    EXPECT_EQ(history[0].payload, (bc::protocol::Payload{'H', 'e', 'y'}));

    EXPECT_EQ(history[1].id, "hash_456");
    EXPECT_EQ(history[1].direction, MessageDirection::INBOUND);
    EXPECT_EQ(history[1].status, MessageStatus::DELIVERED);
    EXPECT_EQ(history[1].payload, (bc::protocol::Payload{'H', 'i'}));
}

TEST_F(ConversationCacheTest, UpdateMessageStatusWorksAtomically)
{
    ConversationCache cache;
    ASSERT_TRUE(cache.Initialize(testCacheDir));

    CacheEntry msg1{.id = "target_hash",
                    .timestamp = 1600000000,
                    .direction = MessageDirection::OUTBOUND,
                    .alias = testAlias,
                    .status = MessageStatus::PENDING_ACK,
                    .payload = bc::protocol::Payload{'T', 'e', 's', 't'}};

    cache.AppendMessage(msg1);
    cache.UpdateMessageStatus(testAlias, "target_hash", MessageStatus::DELIVERED);

    auto history = cache.LoadHistory(testAlias);
    ASSERT_EQ(history.size(), 1);
    EXPECT_EQ(history[0].status, MessageStatus::DELIVERED)
        << "Status should be updated to DELIVERED.";
}

TEST_F(ConversationCacheTest, UpdateMessageStatusIgnoresUnknownMessage)
{
    ConversationCache cache;
    ASSERT_TRUE(cache.Initialize(testCacheDir));

    CacheEntry msg{.id = "valid_hash",
                   .timestamp = 100,
                   .direction = MessageDirection::OUTBOUND,
                   .alias = testAlias,
                   .status = MessageStatus::PENDING_ACK,
                   .payload = {}};
    cache.AppendMessage(msg);

    cache.UpdateMessageStatus(testAlias, "ghost_hash", MessageStatus::DELIVERED);

    auto history = cache.LoadHistory(testAlias);
    ASSERT_EQ(history.size(), 1);
    EXPECT_EQ(history[0].status, MessageStatus::PENDING_ACK);
}

TEST_F(ConversationCacheTest, DeleteHistoryRemovesFileSuccessfully)
{
    ConversationCache cache;
    ASSERT_TRUE(cache.Initialize(testCacheDir));

    CacheEntry msg{.id = "123",
                   .timestamp = 0,
                   .direction = MessageDirection::INBOUND,
                   .alias = testAlias,
                   .status = MessageStatus::DELIVERED,
                   .payload = {}};
    cache.AppendMessage(msg);

    EXPECT_TRUE(cache.DeleteHistory(testAlias));
    EXPECT_TRUE(cache.LoadHistory(testAlias).empty());
}

} // namespace bc::domain::client::test
