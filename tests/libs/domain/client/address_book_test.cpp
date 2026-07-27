#include <algorithm>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <client/address_book.h>
#include <crypto/identity_key.h>

namespace bc::domain::client::test {

class AddressBookTest : public ::testing::Test
{
protected:
    std::string testDbPath = "test_address_book.json";

    void TearDown() override
    {
        if (std::filesystem::exists(testDbPath)) {
            std::filesystem::remove(testDbPath);
        }
    }
};

TEST_F(AddressBookTest, AddContactFailsWhenUninitialized)
{
    AddressBook book;
    auto peer = bc::crypto::IdentityKey::Generate();
    EXPECT_FALSE(book.AddContact("bob", peer.GetPublicKey(), std::nullopt));
}

TEST_F(AddressBookTest, InitializeAndAddValidContact)
{
    auto myIdentity = bc::crypto::IdentityKey::Generate();
    AddressBook book;
    book.Initialize(testDbPath, myIdentity);

    auto peer = bc::crypto::IdentityKey::Generate();
    EXPECT_TRUE(book.AddContact("bob", peer.GetPublicKey(), "trusted"));

    const auto* contact = book.GetContact("bob");
    ASSERT_NE(contact, nullptr);
    EXPECT_EQ(contact->alias, "bob");
    EXPECT_EQ(contact->publicKey, peer.GetPublicKey());
    ASSERT_TRUE(contact->note.has_value());
    EXPECT_EQ(contact->note.value(), "trusted");
}

TEST_F(AddressBookTest, AddContactFailsWithMathematicallyInvalidKey)
{
    auto myIdentity = bc::crypto::IdentityKey::Generate();
    AddressBook book;
    book.Initialize(testDbPath, myIdentity);

    bc::crypto::PublicKeyType invalidKey{};
    invalidKey.fill(0xFF);
    EXPECT_FALSE(book.AddContact("hacker", invalidKey, std::nullopt));
}

TEST_F(AddressBookTest, GetContactReturnsNullptrForUnknownAlias)
{
    auto myIdentity = bc::crypto::IdentityKey::Generate();
    AddressBook book;
    book.Initialize(testDbPath, myIdentity);

    EXPECT_EQ(book.GetContact("ghost_user"), nullptr);
}

TEST_F(AddressBookTest, InitializeRestoresExistingKeysAndHandlesDerivationFailure)
{
    std::ofstream out(testDbPath);
    out << R"({
        "contacts": [
            {
                "alias": "alice_restored",
                "publicKey": "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
                "note": "restored",
                "rxMailboxId": "0102030405060708090a0b0c0d0e0f10",
                "txMailboxId": "0102030405060708090a0b0c0d0e0f10",
                "rxKey": "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
                "txKey": "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"
            },
            {
                "alias": "bob_derive_fail",
                "publicKey": "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            }
        ]
    })";
    out.close();

    auto myIdentity = bc::crypto::IdentityKey::Generate();
    AddressBook book;
    book.Initialize(testDbPath, myIdentity);

    auto* alice = book.GetContact("alice_restored");
    ASSERT_NE(alice, nullptr);
    EXPECT_EQ(alice->rxMailboxId.AsSpan()[0], 0x01);

    auto* bob = book.GetContact("bob_derive_fail");
    EXPECT_EQ(bob, nullptr);
}

TEST_F(AddressBookTest, GetAllAliasesReturnsCorrectList)
{
    auto myIdentity = bc::crypto::IdentityKey::Generate();
    AddressBook book;
    book.Initialize(testDbPath, myIdentity);

    auto peer1 = bc::crypto::IdentityKey::Generate();
    auto peer2 = bc::crypto::IdentityKey::Generate();

    book.AddContact("alice", peer1.GetPublicKey(), std::nullopt);
    book.AddContact("bob", peer2.GetPublicKey(), "friend");

    auto aliases = book.GetAllAliases();
    EXPECT_EQ(aliases.size(), 2);
    EXPECT_TRUE(std::find(aliases.begin(), aliases.end(), "alice") != aliases.end());
    EXPECT_TRUE(std::find(aliases.begin(), aliases.end(), "bob") != aliases.end());
}

TEST_F(AddressBookTest, GetAliasByRxMailboxIdWorksForCurrentAndOldId)
{
    auto myIdentity = bc::crypto::IdentityKey::Generate();
    AddressBook book;
    book.Initialize(testDbPath, myIdentity);

    auto peer = bc::crypto::IdentityKey::Generate();
    book.AddContact("charlie", peer.GetPublicKey(), std::nullopt);

    auto* contact = book.GetMutableContact("charlie");
    ASSERT_NE(contact, nullptr);

    EXPECT_EQ(book.GetAliasByRxMailboxId(contact->rxMailboxId), "charlie");

    bc::protocol::MailboxID oldId;
    oldId.Fill(0x99);
    contact->oldRxMailboxId = oldId;

    EXPECT_EQ(book.GetAliasByRxMailboxId(oldId), "charlie");

    bc::protocol::MailboxID unknownId;
    unknownId.Fill(0x00);
    EXPECT_EQ(book.GetAliasByRxMailboxId(unknownId), "");
}

TEST_F(AddressBookTest, GetMutableContactReturnsNullptrForUnknown)
{
    auto myIdentity = bc::crypto::IdentityKey::Generate();
    AddressBook book;
    book.Initialize(testDbPath, myIdentity);

    EXPECT_EQ(book.GetMutableContact("ghost"), nullptr);
}

TEST_F(AddressBookTest, SaveToDiskSucceeds)
{
    auto myIdentity = bc::crypto::IdentityKey::Generate();
    AddressBook book;
    book.Initialize(testDbPath, myIdentity);

    auto peer = bc::crypto::IdentityKey::Generate();
    book.AddContact("dave", peer.GetPublicKey(), std::nullopt);

    EXPECT_TRUE(book.SaveToDisk());
    EXPECT_TRUE(std::filesystem::exists(testDbPath));
}

} // namespace bc::domain::client::test
