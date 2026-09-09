#include <algorithm>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <client/address_book.h>
#include <core/string_utils.h>
#include <crypto/identity_key.h>
#include <crypto/mailbox_derivation.h>

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

TEST_F(AddressBookTest, InitialPfsCompleteSurvivesRestart)
{
    auto myIdentity = bc::crypto::IdentityKey::Generate();
    auto peer = bc::crypto::IdentityKey::Generate();

    {
        AddressBook book;
        book.Initialize(testDbPath, myIdentity);

        ASSERT_TRUE(book.AddContact("alice", peer.GetPublicKey(), std::nullopt));

        auto* contact = book.GetMutableContact("alice");
        ASSERT_NE(contact, nullptr);

        EXPECT_FALSE(contact->initialPfsComplete);

        contact->initialPfsComplete = true;
        ASSERT_TRUE(book.SaveToDisk());
    }

    AddressBook restoredBook;
    restoredBook.Initialize(testDbPath, myIdentity);

    const auto* restoredContact = restoredBook.GetContact("alice");
    ASSERT_NE(restoredContact, nullptr);

    EXPECT_TRUE(restoredContact->initialPfsComplete);
}

TEST_F(AddressBookTest, EstablishedPfsSessionKeysSurviveRestart)
{
    auto myIdentity = bc::crypto::IdentityKey::Generate();
    auto peer = bc::crypto::IdentityKey::Generate();

    bc::protocol::MailboxID expectedRx;
    bc::protocol::MailboxID expectedTx;
    expectedRx.Fill(0xA1);
    expectedTx.Fill(0xB2);

    {
        AddressBook book;
        book.Initialize(testDbPath, myIdentity);

        ASSERT_TRUE(book.AddContact("alice", peer.GetPublicKey(), std::nullopt));

        auto* contact = book.GetMutableContact("alice");
        ASSERT_NE(contact, nullptr);

        contact->rxMailboxId = expectedRx;
        contact->txMailboxId = expectedTx;

        std::ranges::fill(contact->rxKey.AsMutableSpan(), 0xC3);
        std::ranges::fill(contact->txKey.AsMutableSpan(), 0xD4);

        contact->initialPfsComplete = true;

        ASSERT_TRUE(book.SaveToDisk());
    }

    AddressBook restoredBook;
    restoredBook.Initialize(testDbPath, myIdentity);

    const auto* restored = restoredBook.GetContact("alice");
    ASSERT_NE(restored, nullptr);

    EXPECT_TRUE(restored->initialPfsComplete);

    EXPECT_EQ(restored->rxMailboxId, expectedRx);
    EXPECT_EQ(restored->txMailboxId, expectedTx);

    EXPECT_TRUE(std::ranges::all_of(restored->rxKey.AsSpan(),
                                    [](std::uint8_t value) { return value == 0xC3; }));

    EXPECT_TRUE(std::ranges::all_of(restored->txKey.AsSpan(),
                                    [](std::uint8_t value) { return value == 0xD4; }));
}

TEST_F(AddressBookTest, LegacyStaticBootstrapSessionIsMarkedIncomplete)
{
    auto myIdentity = bc::crypto::IdentityKey::Generate();
    auto peer = bc::crypto::IdentityKey::Generate();

    auto bootstrap = bc::crypto::DerivePairwiseMailboxes(myIdentity, peer.GetPublicKey());
    ASSERT_TRUE(bootstrap.has_value());

    {
        std::ofstream out(testDbPath);

        out << "{\n"
               "  \"contacts\": [\n"
               "    {\n"
               "      \"alias\": \"alice\",\n"
               "      \"publicKey\": \""
            << bc::core::EncodeHex(peer.GetPublicKey())
            << "\",\n"
               "      \"rxMailboxId\": \""
            << bc::core::EncodeHex(bootstrap->rxId)
            << "\",\n"
               "      \"txMailboxId\": \""
            << bc::core::EncodeHex(bootstrap->txId)
            << "\",\n"
               "      \"rxKey\": \""
            << bc::core::EncodeHex(bootstrap->rxKey.AsSpan())
            << "\",\n"
               "      \"txKey\": \""
            << bc::core::EncodeHex(bootstrap->txKey.AsSpan())
            << "\"\n"
               "    }\n"
               "  ]\n"
               "}\n";
    }

    AddressBook book;
    book.Initialize(testDbPath, myIdentity);

    const auto* contact = book.GetContact("alice");
    ASSERT_NE(contact, nullptr);

    EXPECT_FALSE(contact->initialPfsComplete);
}

TEST_F(AddressBookTest, LegacyNonBootstrapSessionIsMarkedComplete)
{
    auto myIdentity = bc::crypto::IdentityKey::Generate();
    auto peer = bc::crypto::IdentityKey::Generate();

    bc::protocol::MailboxID rx;
    bc::protocol::MailboxID tx;
    rx.Fill(0x11);
    tx.Fill(0x22);

    bc::core::SecureBuffer rxKey(bc::crypto::symmetricKeySize);
    bc::core::SecureBuffer txKey(bc::crypto::symmetricKeySize);

    std::ranges::fill(rxKey.AsMutableSpan(), 0x33);
    std::ranges::fill(txKey.AsMutableSpan(), 0x44);

    {
        std::ofstream out(testDbPath);

        out << "{\n"
               "  \"contacts\": [\n"
               "    {\n"
               "      \"alias\": \"alice\",\n"
               "      \"publicKey\": \""
            << bc::core::EncodeHex(peer.GetPublicKey())
            << "\",\n"
               "      \"rxMailboxId\": \""
            << bc::core::EncodeHex(rx.AsSpan())
            << "\",\n"
               "      \"txMailboxId\": \""
            << bc::core::EncodeHex(tx.AsSpan())
            << "\",\n"
               "      \"rxKey\": \""
            << bc::core::EncodeHex(rxKey.AsSpan())
            << "\",\n"
               "      \"txKey\": \""
            << bc::core::EncodeHex(txKey.AsSpan())
            << "\"\n"
               "    }\n"
               "  ]\n"
               "}\n";
    }

    AddressBook book;
    book.Initialize(testDbPath, myIdentity);

    const auto* contact = book.GetContact("alice");
    ASSERT_NE(contact, nullptr);

    EXPECT_TRUE(contact->initialPfsComplete);
    EXPECT_EQ(contact->rxMailboxId, rx);
    EXPECT_EQ(contact->txMailboxId, tx);
}

} // namespace bc::domain::client::test
