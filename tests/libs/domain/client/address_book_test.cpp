#include <filesystem>

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

} // namespace bc::domain::client::test
