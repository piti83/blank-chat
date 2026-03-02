#include "protocol/mailbox_id.h"
#include "utils/uuid_constants.h"

#include <gtest/gtest.h>
#include <regex>

using namespace bc::protocol;

TEST(MailboxIdTest, CreateReturnsValidId)
{
    auto mailboxId = MailboxId::Create();
    auto raw = mailboxId.GetRaw();

    EXPECT_EQ(raw.size(), bc::utils::kUuidSize);
}

TEST(MailboxIdTest, GetAsStringFormatsCorrectly)
{
    auto mailboxId = MailboxId::Create();
    auto str = mailboxId.GetAsString();

    EXPECT_EQ(str.length(), 36);

    std::regex uuidRegex("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    EXPECT_TRUE(std::regex_match(str, uuidRegex));
}

TEST(MailboxIdTest, IdentifiersAreComparable)
{
    auto id1 = MailboxId::Create();
    auto id2 = MailboxId::Create();

    EXPECT_NE(id1, id2);

    auto id1Copy = id1;
    EXPECT_EQ(id1, id1Copy);
}
