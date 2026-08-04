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

    Contact c;
    c.alias = maliciousAlias;
    c.publicKey = mockKey;
    c.note = maliciousNote;
    c.rxMailboxId.Fill(0x01);
    c.txMailboxId.Fill(0x02);
    c.rxKey = bc::core::SecureBuffer(32);
    c.txKey = bc::core::SecureBuffer(32);

    std::vector<const Contact*> vec = {&c};
    SyncContactsToDisk(testDbPath, vec);

    auto contacts = ParseContacts(testDbPath);
    ASSERT_EQ(contacts.size(), 1);
    EXPECT_EQ(contacts[0].alias, maliciousAlias);
}

TEST_F(ContactStorageTest, HandlesMissingFileGracefully)
{
    auto contacts = ParseContacts("definitely_does_not_exist.json");
    EXPECT_TRUE(contacts.empty());
}

TEST_F(ContactStorageTest, FailsSecurelyOnInvalidJson)
{
    WriteDb(R"({ "contacts": [ {"alias": "alice" )");
    auto contacts = ParseContacts(testDbPath);
    EXPECT_TRUE(contacts.empty());
}

TEST_F(ContactStorageTest, HandlesMissingContactsArray)
{
    WriteDb(R"({ "some_other_field": [] })");
    auto contacts = ParseContacts(testDbPath);
    EXPECT_TRUE(contacts.empty());
}

TEST_F(ContactStorageTest, HandlesNonObjectInContactsArray)
{
    WriteDb(R"({ "contacts": [ "this_is_a_string_not_an_object", 42 ] })");
    auto contacts = ParseContacts(testDbPath);
    EXPECT_TRUE(contacts.empty());
}

TEST_F(ContactStorageTest, ParsesOptionalFieldsProperly)
{
    WriteDb(R"({
        "contacts": [
            {
                "alias": "full_contact",
                "publicKey": "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
                "note": "some note",
                "rxMailboxId": "aa02030405060708090a0b0c0d0e0f10",
                "txMailboxId": "bb02030405060708090a0b0c0d0e0f10",
                "rxKey": "cc02030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
                "txKey": "dd02030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"
            }
        ]
    })");
    auto contacts = ParseContacts(testDbPath);
    ASSERT_EQ(contacts.size(), 1);
    EXPECT_EQ(contacts[0].alias, "full_contact");

    ASSERT_TRUE(contacts[0].rxMailboxId.has_value());
    EXPECT_EQ(contacts[0].rxMailboxId.value()[0], 0xAA);

    ASSERT_TRUE(contacts[0].txMailboxId.has_value());
    EXPECT_EQ(contacts[0].txMailboxId.value()[0], 0xBB);

    ASSERT_TRUE(contacts[0].rxKey.has_value());
    EXPECT_EQ(contacts[0].rxKey.value()[0], 0xCC);

    ASSERT_TRUE(contacts[0].txKey.has_value());
    EXPECT_EQ(contacts[0].txKey.value()[0], 0xDD);
}

TEST_F(ContactStorageTest, IgnoresMalformedHexInOptionalFields)
{
    WriteDb(R"({
        "contacts": [
            {
                "alias": "partial_contact",
                "publicKey": "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
                "note": "",
                "rxMailboxId": "invalid_hex",
                "txMailboxId": "123",
                "rxKey": "not_hex",
                "txKey": "zzz"
            }
        ]
    })");
    auto contacts = ParseContacts(testDbPath);
    ASSERT_EQ(contacts.size(), 1);
    EXPECT_EQ(contacts[0].alias, "partial_contact");

    EXPECT_FALSE(contacts[0].rxMailboxId.has_value());
    EXPECT_FALSE(contacts[0].txMailboxId.has_value());
    EXPECT_FALSE(contacts[0].rxKey.has_value());
    EXPECT_FALSE(contacts[0].txKey.has_value());
}

TEST_F(ContactStorageTest, SyncContactsToDiskReturnsFalseOnUnwritableFile)
{
    std::string fakeDirName = "dummy_dir_as_file";
    std::filesystem::create_directory(fakeDirName);

    std::vector<const Contact*> activeContacts;
    EXPECT_FALSE(SyncContactsToDisk(fakeDirName, activeContacts));

    std::filesystem::remove(fakeDirName);
}

} // namespace bc::domain::client::test
