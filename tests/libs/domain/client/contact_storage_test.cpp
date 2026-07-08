#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <client/contact_storage.h>

namespace bc::domain::client::test {

class ContactStorageTest : public ::testing::Test
{
protected:
    std::string testDbPath = "test_contacts_db.json";

    void TearDown() override
    {
        if (std::filesystem::exists(testDbPath)) {
            std::filesystem::remove(testDbPath);
        }
    }

    auto WriteDb(const std::string& content) -> void
    {
        std::ofstream out(testDbPath, std::ios::trunc);
        out << content;
    }
};

TEST_F(ContactStorageTest, SafelyParsesValidContacts)
{
    WriteDb(R"({
        "contacts": [
            {
                "alias": "alice",
                "publicKey": "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
                "note": "trusted"
            }
        ]
    })");

    auto contacts = ParseContacts(testDbPath);
    ASSERT_EQ(contacts.size(), 1);
    EXPECT_EQ(contacts[0].alias, "alice");
    EXPECT_EQ(contacts[0].publicKey[0], 0x01);
    EXPECT_EQ(contacts[0].publicKey[31], 0x20);
    ASSERT_TRUE(contacts[0].note.has_value());
    EXPECT_EQ(contacts[0].note.value(), "trusted");
}

TEST_F(ContactStorageTest, RejectsMissingAliasOrPublicKey)
{
    WriteDb(R"({
        "contacts": [
            { "publicKey": "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20" },
            { "alias": "bob" }
        ]
    })");

    auto contacts = ParseContacts(testDbPath);
    EXPECT_TRUE(contacts.empty());
}

TEST_F(ContactStorageTest, RejectsMalformedHexInPublicKey)
{
    WriteDb(R"({
        "contacts": [
            {
                "alias": "charlie",
                "publicKey": "invalid_hex_string_that_is_definitely_not_a_key"
            },
            {
                "alias": "dave",
                "publicKey": "010203"
            }
        ]
    })");

    auto contacts = ParseContacts(testDbPath);
    EXPECT_TRUE(contacts.empty());
}

TEST_F(ContactStorageTest, SafelyEscapesMaliciousInputDuringSave)
{
    PublicKeyType mockKey{};
    mockKey.fill(0xAA);

    std::string maliciousAlias = "eve\", \"admin\": true, \"dummy\": \"";
    std::string maliciousNote = "newline\nand\ttab\\slash";

    SaveContact(testDbPath, maliciousAlias, mockKey, maliciousNote);

    auto contacts = ParseContacts(testDbPath);
    ASSERT_EQ(contacts.size(), 1);
    EXPECT_EQ(contacts[0].alias, maliciousAlias);
    ASSERT_TRUE(contacts[0].note.has_value());
    EXPECT_EQ(contacts[0].note.value(), maliciousNote);
}

TEST_F(ContactStorageTest, HandlesMissingFileGracefully)
{
    auto contacts = ParseContacts("definitely_does_not_exist.json");
    EXPECT_TRUE(contacts.empty());
}

} // namespace bc::domain::client::test
