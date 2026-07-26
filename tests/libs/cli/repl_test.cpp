#include <filesystem>
#include <sstream>
#include <system_error>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <client/address_book.h>
#include <crypto/bip39.h>
#include <crypto/identity_key.h>
#include <crypto/symmetric_cipher.h>
#include <protocol/frame.h>

#define private public
#include <cli/repl.h>
#undef private

namespace bc::cli::test {

class ReplTest : public ::testing::Test
{
protected:
    boost::asio::io_context serverIo;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor;
    std::uint16_t serverPort{0};
    std::thread serverThread;

    std::streambuf *orig_cin, *orig_cout;
    std::stringstream test_in, test_out;

    std::optional<bc::crypto::IdentityKey> testIdentity;
    bc::domain::client::AddressBook testAddressBook;
    bc::domain::client::ConversationCache testCache;

    bc::domain::client::ClientConfig testConfig{};

    void SetUp() override
    {
        std::error_code ec;
        std::filesystem::remove("test_contacts.json", ec);
        std::filesystem::remove_all("test_cache", ec);

        testIdentity.emplace(bc::crypto::IdentityKey::Generate());
        testAddressBook.Initialize("test_contacts.json", *testIdentity);
        testCache.Initialize("test_cache");

        std::cin.clear();
        orig_cin = std::cin.rdbuf(test_in.rdbuf());
        orig_cout = std::cout.rdbuf(test_out.rdbuf());

        acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(
            serverIo, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
        serverPort = acceptor->local_endpoint().port();

        testConfig.networkConfig.torSocksHost = "127.0.0.1";
        testConfig.networkConfig.torSocksPort = serverPort;
        testConfig.relayConfig.onionAddress = "test.onion";
        testConfig.relayConfig.onionPort = 80;
        testConfig.obfuscationConfig = {"cbr", 5000, 5.0F};
        testConfig.securityConfig.pfsMessageInterval = 50;

        serverThread = std::thread([this]() {
            auto workGuard = boost::asio::make_work_guard(serverIo);
            serverIo.run();
        });
    }

    void TearDown() override
    {
        std::cin.clear();
        std::cin.rdbuf(orig_cin);
        std::cout.rdbuf(orig_cout);

        serverIo.stop();
        if (serverThread.joinable()) {
            serverThread.join();
        }

        std::error_code ec;
        std::filesystem::remove("test_contacts.json", ec);
        std::filesystem::remove_all("test_cache", ec);
    }
};

TEST_F(ReplTest, RunLoop_ParsesBasicCommandsAndExits)
{
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    auto mockContact = bc::crypto::IdentityKey::Generate();
    auto mnemonic = bc::crypto::bip39::Encode(mockContact.GetPublicKey());

    test_in << "add alice " << mnemonic.StringView() << "\n";
    test_in << "mykey\n";
    test_in << "list\n";
    test_in << "invalid_command_name\n";
    test_in << "exit\n";

    repl.Run();

    EXPECT_NE(test_out.str().find("added successfully"), std::string::npos);
    EXPECT_NE(test_out.str().find("Your Identity Key"), std::string::npos);
    EXPECT_NE(test_out.str().find("alice"), std::string::npos);
    EXPECT_NE(test_out.str().find("Unknown command"), std::string::npos);
}

TEST_F(ReplTest, HandleSend_StreamFailsSecurely)
{
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);
    test_in.str("");
    repl.HandleSend();
    SUCCEED() << "Must abort gracefully when cin terminates early.";
}

TEST_F(ReplTest, HandleSend_FailsOnUnknownContact)
{
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);
    test_in << "ghost Target Data\n";
    repl.HandleSend();
    EXPECT_NE(test_out.str().find("not found in address book"), std::string::npos);
}

TEST_F(ReplTest, HandleSend_SucceedsAndQueuesFrame)
{
    auto peer = bc::crypto::IdentityKey::Generate();
    testAddressBook.AddContact("alice", peer.GetPublicKey(), std::nullopt);

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);
    test_in << "alice Highly Classified Data\n";
    repl.HandleSend();

    EXPECT_NE(test_out.str().find("Message queued for transmission"), std::string::npos);
    EXPECT_EQ(repl.outbox.size(), 1);
    EXPECT_EQ(repl.outbox.front().GetActionType(), bc::protocol::ActionType::PUSH);
}

TEST_F(ReplTest, HandleHistory_PopulatedAndEmpty)
{
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    bc::domain::client::CacheEntry entry{.id = "hash123",
                                         .timestamp = 0,
                                         .direction = bc::domain::client::MessageDirection::INBOUND,
                                         .alias = "alice",
                                         .status = bc::domain::client::MessageStatus::DELIVERED,
                                         .payload = {'O', 'K'}};
    testCache.AppendMessage(entry);

    test_in << "alice\nghost\n";
    repl.HandleHistory();
    repl.HandleHistory();

    EXPECT_NE(test_out.str().find("[IN] [OK] OK"), std::string::npos);
    EXPECT_NE(test_out.str().find("--- History for ghost ---"), std::string::npos);
}

TEST_F(ReplTest, GetNextFrameForCBR_PopsFromOutbox)
{
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    bc::protocol::MailboxID dummy;
    dummy.Fill(0x11);
    repl.outbox.push(bc::protocol::Frame::CreatePoll(dummy));

    auto frame = repl.GetNextFrameForCBR();
    EXPECT_EQ(frame.GetActionType(), bc::protocol::ActionType::POLL);
    EXPECT_EQ(frame.GetMailboxID(), dummy);
    EXPECT_TRUE(repl.outbox.empty());
}

TEST_F(ReplTest, GetNextFrameForCBR_NoContacts_ReturnsDummyPoll)
{
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    auto frame = repl.GetNextFrameForCBR();

    bc::protocol::MailboxID zeros;
    zeros.Fill(0x00);
    EXPECT_EQ(frame.GetActionType(), bc::protocol::ActionType::POLL);
    EXPECT_EQ(frame.GetMailboxID(), zeros);
}

TEST_F(ReplTest, GetNextFrameForCBR_WithContacts_RotatesPolls)
{
    auto peer1 = bc::crypto::IdentityKey::Generate();
    auto peer2 = bc::crypto::IdentityKey::Generate();
    testAddressBook.AddContact("alice", peer1.GetPublicKey(), std::nullopt);
    testAddressBook.AddContact("bob", peer2.GetPublicKey(), std::nullopt);

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);
    repl.contactAliases = {"alice", "bob", "ghost"};
    repl.currentPollIndex = 0;

    auto frame1 = repl.GetNextFrameForCBR();
    auto frame2 = repl.GetNextFrameForCBR();
    auto frame3 = repl.GetNextFrameForCBR();
    auto frame4 = repl.GetNextFrameForCBR();

    EXPECT_EQ(frame1.GetActionType(), bc::protocol::ActionType::POLL);
    EXPECT_EQ(frame4.GetMailboxID(), frame1.GetMailboxID());
}

TEST_F(ReplTest, OnFrameReceived_Push_UnknownMailbox)
{
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    bc::protocol::MailboxID dummy;
    dummy.Fill(0x99);
    repl.OnFrameReceived(bc::protocol::Frame::CreatePush(dummy, {0x00}));

    EXPECT_NE(test_out.str().find("Received message for unknown MailboxID"), std::string::npos);
}

TEST_F(ReplTest, OnFrameReceived_Push_TamperedCiphertext)
{
    auto peer = bc::crypto::IdentityKey::Generate();
    testAddressBook.AddContact("alice", peer.GetPublicKey(), std::nullopt);
    auto* contact = testAddressBook.GetContact("alice");

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);
    repl.OnFrameReceived(bc::protocol::Frame::CreatePush(contact->rxMailboxId, {0xBA, 0xAD}));

    EXPECT_NE(test_out.str().find("Malformed or tampered PUSH message dropped silently"),
              std::string::npos);
}

TEST_F(ReplTest, OnFrameReceived_Push_ValidSendsAck)
{
    auto peer = bc::crypto::IdentityKey::Generate();
    testAddressBook.AddContact("alice", peer.GetPublicKey(), std::nullopt);
    auto* contact = testAddressBook.GetContact("alice");

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    bc::protocol::Payload plaintext = {'O', 'K'};
    auto ciphertextOpt =
        bc::crypto::SymmetricCipher::EncryptWithPadding(contact->rxKey.AsSpan(), plaintext);

    repl.OnFrameReceived(
        bc::protocol::Frame::CreatePush(contact->rxMailboxId, std::move(*ciphertextOpt)));

    EXPECT_NE(test_out.str().find("New message from alice"), std::string::npos);
    EXPECT_NE(test_out.str().find("Encrypted ACK queued for transmission"), std::string::npos);
    ASSERT_EQ(repl.outbox.size(), 1);
    EXPECT_EQ(repl.outbox.front().GetActionType(), bc::protocol::ActionType::ACK);
}

TEST_F(ReplTest, OnFrameReceived_Ack_UnknownAndTampered)
{
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    bc::protocol::MailboxID dummy;
    dummy.Fill(0x99);
    repl.OnFrameReceived(bc::protocol::Frame::CreateAck(dummy, {0x00}));

    auto peer = bc::crypto::IdentityKey::Generate();
    testAddressBook.AddContact("alice", peer.GetPublicKey(), std::nullopt);
    auto* contact = testAddressBook.GetContact("alice");

    repl.OnFrameReceived(bc::protocol::Frame::CreateAck(contact->rxMailboxId, {0xBA, 0xAD}));

    EXPECT_NE(test_out.str().find("Malformed or tampered ACK message dropped silently"),
              std::string::npos);
}

TEST_F(ReplTest, OnFrameReceived_Ack_ValidUpdatesStatus)
{
    auto peer = bc::crypto::IdentityKey::Generate();
    testAddressBook.AddContact("alice", peer.GetPublicKey(), std::nullopt);
    auto* contact = testAddressBook.GetContact("alice");

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    std::string msgId = "test_msg_id";
    bc::domain::client::CacheEntry entry{.id = msgId,
                                         .timestamp = 0,
                                         .direction =
                                             bc::domain::client::MessageDirection::OUTBOUND,
                                         .alias = "alice",
                                         .status = bc::domain::client::MessageStatus::PENDING_ACK,
                                         .payload = {}};
    testCache.AppendMessage(entry);

    bc::protocol::Payload msgIdPayload(msgId.begin(), msgId.end());
    auto ciphertextOpt =
        bc::crypto::SymmetricCipher::EncryptWithPadding(contact->rxKey.AsSpan(), msgIdPayload);

    repl.OnFrameReceived(
        bc::protocol::Frame::CreateAck(contact->rxMailboxId, std::move(*ciphertextOpt)));

    EXPECT_NE(test_out.str().find("Message DELIVERED"), std::string::npos);

    auto history = testCache.LoadHistory("alice");
    ASSERT_EQ(history.size(), 1);
    EXPECT_EQ(history[0].status, bc::domain::client::MessageStatus::DELIVERED);
}

} // namespace bc::cli::test
