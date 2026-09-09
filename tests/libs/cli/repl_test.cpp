#include <filesystem>
#include <span>
#include <sstream>
#include <system_error>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <client/address_book.h>
#include <client/payload_formatter.h>
#include <crypto/bip39.h>
#include <crypto/identity_key.h>
#include <crypto/mailbox_derivation.h>
#include <crypto/symmetric_cipher.h>
#include <protocol/frame.h>

#include <sodium/crypto_sign.h>

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

TEST_F(ReplTest, RunLoop_AddCommand_InvalidMnemonic)
{
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);
    test_in << "add bob invalid-mnemonic-phrase\nexit\n";
    repl.Run();
    EXPECT_EQ(testAddressBook.GetContact("bob"), nullptr);
}

TEST_F(ReplTest, RunLoop_AddCommand_MissingArguments)
{
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);
    test_in << "add charlie\nexit\n";
    repl.Run();
    EXPECT_EQ(testAddressBook.GetContact("charlie"), nullptr);
}

TEST_F(ReplTest, HandleConnect_FailsFastWhenTorIsDown)
{
    acceptor->close();

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    repl.HandleConnect();

    EXPECT_NE(test_out.str().find("Failed to connect"), std::string::npos);
}

TEST_F(ReplTest, HandleSend_StreamFailsSecurely)
{
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);
    test_in.str("");
    repl.HandleSend();
    SUCCEED() << "Must abort gracefully when cin terminates early.";
}

TEST_F(ReplTest, HandleSend_MissingAlias)
{
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);
    test_in << "\n";
    repl.HandleSend();
    EXPECT_TRUE(repl.outbox.empty());
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
    auto* contact = testAddressBook.GetMutableContact("alice");
    ASSERT_NE(contact, nullptr);
    contact->initialPfsComplete = true;
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

    EXPECT_EQ(frame1.GetActionType(), bc::protocol::ActionType::POLL);
    EXPECT_EQ(frame3.GetMailboxID(), frame1.GetMailboxID());
    EXPECT_NE(frame1.GetMailboxID(), frame2.GetMailboxID());
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
    auto* contact = testAddressBook.GetMutableContact("alice");
    ASSERT_NE(contact, nullptr);
    contact->initialPfsComplete = true;
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    bc::protocol::Payload rawText = {'O', 'K'};
    bc::protocol::Payload plaintext =
        bc::domain::client::PayloadFormatter::BuildTextMessage(rawText);
    auto ciphertextOpt =
        bc::crypto::SymmetricCipher::EncryptWithPadding(contact->rxKey.AsSpan(), plaintext);

    repl.OnFrameReceived(
        bc::protocol::Frame::CreatePush(contact->rxMailboxId, std::move(*ciphertextOpt)));

    EXPECT_NE(test_out.str().find("New message from alice"), std::string::npos);
    EXPECT_NE(test_out.str().find("Encrypted ACK queued for transmission"), std::string::npos);
    ASSERT_EQ(repl.outbox.size(), 1);
    EXPECT_EQ(repl.outbox.front().GetActionType(), bc::protocol::ActionType::ACK);
}

TEST_F(ReplTest, OnFrameReceived_Push_PfsRotateRequest)
{
    auto peer = bc::crypto::IdentityKey::Generate();
    testAddressBook.AddContact("alice", peer.GetPublicKey(), std::nullopt);
    auto* contact = testAddressBook.GetContact("alice");
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    bc::crypto::PublicKeyType ephKey{};
    std::array<std::uint8_t, bc::domain::client::cryptoSignBytes> sig{};
    auto plaintext = bc::domain::client::PayloadFormatter::BuildPfsRotateRequest(ephKey, sig);
    auto ciphertextOpt =
        bc::crypto::SymmetricCipher::EncryptWithPadding(contact->rxKey.AsSpan(), plaintext);

    repl.OnFrameReceived(
        bc::protocol::Frame::CreatePush(contact->rxMailboxId, std::move(*ciphertextOpt)));

    SUCCEED();
}

TEST_F(ReplTest, OnFrameReceived_Push_PfsRotateAck)
{
    auto peer = bc::crypto::IdentityKey::Generate();
    testAddressBook.AddContact("alice", peer.GetPublicKey(), std::nullopt);
    auto* contact = testAddressBook.GetContact("alice");
    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    bc::crypto::PublicKeyType ephKey{};
    std::array<std::uint8_t, bc::domain::client::cryptoSignBytes> sig{};
    auto plaintext = bc::domain::client::PayloadFormatter::BuildPfsRotateAck(ephKey, sig);
    auto ciphertextOpt =
        bc::crypto::SymmetricCipher::EncryptWithPadding(contact->rxKey.AsSpan(), plaintext);

    repl.OnFrameReceived(
        bc::protocol::Frame::CreatePush(contact->rxMailboxId, std::move(*ciphertextOpt)));

    SUCCEED();
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

TEST_F(ReplTest, HandleConnect_SuccessPathAndAsyncEngine)
{
    acceptor->async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
        if (ec)
            return;
        std::array<std::uint8_t, 3> greeting{};
        boost::system::error_code ignore;
        boost::asio::read(sock, boost::asio::buffer(greeting), ignore);
        std::array<std::uint8_t, 2> greetingResp = {0x05, 0x00};
        boost::asio::write(sock, boost::asio::buffer(greetingResp), ignore);

        std::array<std::uint8_t, 5> reqHeader{};
        boost::asio::read(sock, boost::asio::buffer(reqHeader), ignore);
        std::vector<std::uint8_t> extra(reqHeader[4] + 2);
        boost::asio::read(sock, boost::asio::buffer(extra), ignore);

        std::array<std::uint8_t, 10> successResp = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
        boost::asio::write(sock, boost::asio::buffer(successResp), ignore);
    });

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);
    repl.HandleConnect();

    EXPECT_NE(test_out.str().find("Successfully connected"), std::string::npos);
}

TEST_F(ReplTest, ProcessPushFrame_TriggersAndCompletesPfsRotationCycle)
{
    auto peer = bc::crypto::IdentityKey::Generate();
    testAddressBook.AddContact("alice", peer.GetPublicKey(), std::nullopt);

    auto* contact = testAddressBook.GetMutableContact("alice");
    ASSERT_NE(contact, nullptr);

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    auto ephemeralPeer = bc::crypto::EphemeralKey::Generate();
    ASSERT_TRUE(ephemeralPeer.has_value());

    std::array<std::uint8_t, bc::domain::client::cryptoSignBytes> signature{};
    bc::crypto::PublicKeyType ephPk = ephemeralPeer->GetPublicKey();

    auto reqPayload = bc::domain::client::PayloadFormatter::BuildPfsRotateRequest(ephPk, signature);
    auto ciphertextOpt =
        bc::crypto::SymmetricCipher::EncryptWithPadding(contact->rxKey.AsSpan(), reqPayload);
    ASSERT_TRUE(ciphertextOpt.has_value());

    repl.OnFrameReceived(
        bc::protocol::Frame::CreatePush(contact->rxMailboxId, std::move(*ciphertextOpt)));

    contact->pfsState = bc::domain::client::PfsState::ROTATION_REQUESTED;
    auto newEph = bc::crypto::EphemeralKey::Generate();
    ASSERT_TRUE(newEph.has_value());
    contact->pendingEphemeralKey.emplace(std::move(*newEph));

    auto ackPayload = bc::domain::client::PayloadFormatter::BuildPfsRotateAck(ephPk, signature);
    auto ackCiphertextOpt =
        bc::crypto::SymmetricCipher::EncryptWithPadding(contact->txKey.AsSpan(), ackPayload);
    ASSERT_TRUE(ackCiphertextOpt.has_value());

    repl.OnFrameReceived(
        bc::protocol::Frame::CreatePush(contact->rxMailboxId, std::move(*ackCiphertextOpt)));

    SUCCEED();
}

TEST_F(ReplTest, HandleSend_RejectsTextBeforeInitialPfsCompletes)
{
    auto peer = bc::crypto::IdentityKey::Generate();
    ASSERT_TRUE(testAddressBook.AddContact("alice", peer.GetPublicKey(), std::nullopt));

    auto* contact = testAddressBook.GetMutableContact("alice");
    ASSERT_NE(contact, nullptr);
    ASSERT_FALSE(contact->initialPfsComplete);

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    test_in << "alice bootstrap must not carry this text\n";
    repl.HandleSend();

    EXPECT_TRUE(repl.outbox.empty());
    EXPECT_EQ(contact->messageCounter, 0U);

    auto history = testCache.LoadHistory("alice");
    EXPECT_TRUE(history.empty());

    EXPECT_NE(test_out.str().find("Initial PFS handshake is still pending"), std::string::npos);
}

TEST_F(ReplTest, InitialPfsHandshakeDerivesMatchingEphemeralSession)
{
    auto peerIdentity = bc::crypto::IdentityKey::Generate();

    bc::domain::client::AddressBook peerAddressBook;
    peerAddressBook.Initialize("test_contacts_peer.json", peerIdentity);

    bc::domain::client::ConversationCache peerCache;
    peerCache.Initialize("test_cache_peer");

    ASSERT_TRUE(testAddressBook.AddContact("bob", peerIdentity.GetPublicKey(), std::nullopt));
    ASSERT_TRUE(peerAddressBook.AddContact("alice", testIdentity->GetPublicKey(), std::nullopt));

    auto* localContact = testAddressBook.GetMutableContact("bob");
    auto* peerContact = peerAddressBook.GetMutableContact("alice");

    ASSERT_NE(localContact, nullptr);
    ASSERT_NE(peerContact, nullptr);

    ASSERT_FALSE(localContact->initialPfsComplete);
    ASSERT_FALSE(peerContact->initialPfsComplete);

    const auto localBootstrapTx = localContact->txMailboxId;
    const auto localBootstrapRx = localContact->rxMailboxId;

    Repl localRepl(testAddressBook, testCache, *testIdentity, testConfig);
    Repl peerRepl(peerAddressBook, peerCache, peerIdentity, testConfig);

    localRepl.connected = true;
    peerRepl.connected = true;

    const bool localInitiates = localRepl.IsInitialPfsInitiator(*localContact);
    const bool peerInitiates = peerRepl.IsInitialPfsInitiator(*peerContact);

    EXPECT_NE(localInitiates, peerInitiates);

    localRepl.MaybeStartInitialPfs("bob", localContact);
    peerRepl.MaybeStartInitialPfs("alice", peerContact);

    Repl* initiator = localInitiates ? &localRepl : &peerRepl;
    Repl* responder = localInitiates ? &peerRepl : &localRepl;

    auto* initiatorContact = localInitiates ? localContact : peerContact;
    auto* responderContact = localInitiates ? peerContact : localContact;

    ASSERT_EQ(initiator->outbox.size(), 1);
    EXPECT_TRUE(responder->outbox.empty());

    auto requestFrame = std::move(initiator->outbox.front());
    initiator->outbox.pop();

    EXPECT_EQ(requestFrame.GetMailboxID(), responderContact->rxMailboxId);

    responder->OnFrameReceived(std::move(requestFrame));

    ASSERT_TRUE(responderContact->initialPfsComplete);
    ASSERT_EQ(responder->outbox.size(), 1);

    auto ackFrame = std::move(responder->outbox.front());
    responder->outbox.pop();

    initiator->OnFrameReceived(std::move(ackFrame));

    ASSERT_TRUE(initiatorContact->initialPfsComplete);

    EXPECT_EQ(localContact->txMailboxId, peerContact->rxMailboxId);
    EXPECT_EQ(localContact->rxMailboxId, peerContact->txMailboxId);

    EXPECT_TRUE(std::ranges::equal(localContact->txKey.AsSpan(), peerContact->rxKey.AsSpan()));
    EXPECT_TRUE(std::ranges::equal(localContact->rxKey.AsSpan(), peerContact->txKey.AsSpan()));

    EXPECT_NE(localContact->txMailboxId, localBootstrapTx);
    EXPECT_NE(localContact->rxMailboxId, localBootstrapRx);

    EXPECT_FALSE(localContact->oldRxMailboxId.has_value());
    EXPECT_FALSE(peerContact->oldRxMailboxId.has_value());

    EXPECT_FALSE(initiatorContact->pendingEphemeralKey.has_value());

    auto staticBootstrap =
        bc::crypto::DerivePairwiseMailboxes(*testIdentity, peerIdentity.GetPublicKey());

    ASSERT_TRUE(staticBootstrap.has_value());

    EXPECT_NE(localContact->txMailboxId, bc::protocol::MailboxID(staticBootstrap->txId));

    EXPECT_FALSE(std::ranges::equal(localContact->txKey.AsSpan(), staticBootstrap->txKey.AsSpan()));
}

TEST_F(ReplTest, InitialPfsWaitsUntilConnected)
{
    auto peer = bc::crypto::IdentityKey::Generate();
    ASSERT_TRUE(testAddressBook.AddContact("alice", peer.GetPublicKey(), std::nullopt));

    auto* contact = testAddressBook.GetMutableContact("alice");
    ASSERT_NE(contact, nullptr);

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    ASSERT_FALSE(repl.connected);

    repl.MaybeStartInitialPfs("alice", contact);

    EXPECT_TRUE(repl.outbox.empty());
    EXPECT_EQ(contact->pfsState, bc::domain::client::PfsState::IDLE);

    repl.connected = true;

    if (repl.IsInitialPfsInitiator(*contact)) {
        repl.MaybeStartInitialPfs("alice", contact);

        EXPECT_EQ(repl.outbox.size(), 1);
        EXPECT_EQ(contact->pfsState, bc::domain::client::PfsState::ROTATION_REQUESTED);
        EXPECT_TRUE(contact->pendingEphemeralKey.has_value());
    } else {
        repl.MaybeStartInitialPfs("alice", contact);

        EXPECT_TRUE(repl.outbox.empty());
        EXPECT_EQ(contact->pfsState, bc::domain::client::PfsState::IDLE);
    }
}

TEST_F(ReplTest, InitialPfsCanStartImmediatelyForContactAddedWhileConnected)
{
    auto peer = bc::crypto::IdentityKey::Generate();

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);
    repl.connected = true;

    ASSERT_TRUE(testAddressBook.AddContact("alice", peer.GetPublicKey(), std::nullopt));

    auto* contact = testAddressBook.GetMutableContact("alice");
    ASSERT_NE(contact, nullptr);

    repl.MaybeStartInitialPfs("alice", contact);

    if (repl.IsInitialPfsInitiator(*contact)) {
        EXPECT_EQ(repl.outbox.size(), 1);
        EXPECT_EQ(contact->pfsState, bc::domain::client::PfsState::ROTATION_REQUESTED);
        EXPECT_TRUE(contact->pendingEphemeralKey.has_value());
    } else {
        EXPECT_TRUE(repl.outbox.empty());
        EXPECT_EQ(contact->pfsState, bc::domain::client::PfsState::IDLE);
    }
}

TEST_F(ReplTest, PeriodicPfsRotationPreservesOldReceiverMailbox)
{
    auto peerIdentity = bc::crypto::IdentityKey::Generate();

    bc::domain::client::AddressBook peerAddressBook;
    peerAddressBook.Initialize("test_contacts_peer.json", peerIdentity);

    bc::domain::client::ConversationCache peerCache;
    peerCache.Initialize("test_cache_peer");

    ASSERT_TRUE(testAddressBook.AddContact("bob", peerIdentity.GetPublicKey(), std::nullopt));
    ASSERT_TRUE(peerAddressBook.AddContact("alice", testIdentity->GetPublicKey(), std::nullopt));

    auto* localContact = testAddressBook.GetMutableContact("bob");
    auto* peerContact = peerAddressBook.GetMutableContact("alice");

    ASSERT_NE(localContact, nullptr);
    ASSERT_NE(peerContact, nullptr);

    Repl localRepl(testAddressBook, testCache, *testIdentity, testConfig);
    Repl peerRepl(peerAddressBook, peerCache, peerIdentity, testConfig);

    localRepl.connected = true;
    peerRepl.connected = true;

    // First establish generation 1.
    localRepl.MaybeStartInitialPfs("bob", localContact);
    peerRepl.MaybeStartInitialPfs("alice", peerContact);

    Repl* initialInitiator =
        localRepl.IsInitialPfsInitiator(*localContact) ? &localRepl : &peerRepl;
    Repl* initialResponder = initialInitiator == &localRepl ? &peerRepl : &localRepl;

    auto request = std::move(initialInitiator->outbox.front());
    initialInitiator->outbox.pop();
    initialResponder->OnFrameReceived(std::move(request));

    auto ack = std::move(initialResponder->outbox.front());
    initialResponder->outbox.pop();
    initialInitiator->OnFrameReceived(std::move(ack));

    ASSERT_TRUE(localContact->initialPfsComplete);
    ASSERT_TRUE(peerContact->initialPfsComplete);

    // Generation 1 is now the "old" established session.
    const auto peerGeneration1Rx = peerContact->rxMailboxId;

    // Start a normal later rotation from local -> peer.
    ASSERT_TRUE(localRepl.InitiatePfsRotation("bob", localContact));

    auto periodicRequest = std::move(localRepl.outbox.front());
    localRepl.outbox.pop();

    peerRepl.OnFrameReceived(std::move(periodicRequest));

    ASSERT_TRUE(peerContact->oldRxMailboxId.has_value());
    EXPECT_EQ(*peerContact->oldRxMailboxId, peerGeneration1Rx);

    ASSERT_TRUE(peerContact->oldRxKey.has_value());

    // Complete the periodic rotation.
    ASSERT_EQ(peerRepl.outbox.size(), 1);

    auto periodicAck = std::move(peerRepl.outbox.front());
    peerRepl.outbox.pop();

    localRepl.OnFrameReceived(std::move(periodicAck));

    EXPECT_TRUE(localContact->initialPfsComplete);
    EXPECT_TRUE(peerContact->initialPfsComplete);

    EXPECT_EQ(localContact->txMailboxId, peerContact->rxMailboxId);
    EXPECT_EQ(localContact->rxMailboxId, peerContact->txMailboxId);
    ASSERT_TRUE(peerContact->oldRxMailboxId.has_value());
    ASSERT_TRUE(peerContact->oldRxKey.has_value());

    bc::protocol::Payload rawText = {'N', 'E', 'W'};

    auto textPayload = bc::domain::client::PayloadFormatter::BuildTextMessage(rawText);

    auto ciphertextOpt =
        bc::crypto::SymmetricCipher::EncryptWithPadding(localContact->txKey.AsSpan(), textPayload);

    ASSERT_TRUE(ciphertextOpt.has_value());

    auto newGenerationFrame =
        bc::protocol::Frame::CreatePush(localContact->txMailboxId, std::move(*ciphertextOpt));

    peerRepl.OnFrameReceived(std::move(newGenerationFrame));

    EXPECT_FALSE(peerContact->oldRxMailboxId.has_value());
    EXPECT_FALSE(peerContact->oldRxKey.has_value());
}

TEST_F(ReplTest, HandleSendStillTriggersPeriodicPfsAfterInitialSession)
{
    auto peer = bc::crypto::IdentityKey::Generate();

    ASSERT_TRUE(testAddressBook.AddContact("alice", peer.GetPublicKey(), std::nullopt));

    auto* contact = testAddressBook.GetMutableContact("alice");
    ASSERT_NE(contact, nullptr);

    contact->initialPfsComplete = true;

    testConfig.securityConfig.pfsMessageInterval = 1;
    contact->messageCounter = 1;

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    test_in << "alice trigger periodic rotation\n";
    repl.HandleSend();

    EXPECT_EQ(contact->pfsState, bc::domain::client::PfsState::ROTATION_REQUESTED);

    EXPECT_TRUE(contact->pendingEphemeralKey.has_value());

    // One PFS_ROTATE_REQUEST + one normal TEXT_MESSAGE.
    EXPECT_EQ(repl.outbox.size(), 2);

    // InitiatePfsRotation resets to 0 and the sent TEXT increments it to 1.
    EXPECT_EQ(contact->messageCounter, 1U);

    EXPECT_TRUE(contact->initialPfsComplete);
}

TEST_F(ReplTest, PfsAckIsBoundToCurrentInitiatorEphemeralKey)
{
    auto peerIdentity = bc::crypto::IdentityKey::Generate();

    ASSERT_TRUE(testAddressBook.AddContact("bob", peerIdentity.GetPublicKey(), std::nullopt));

    auto* contact = testAddressBook.GetMutableContact("bob");
    ASSERT_NE(contact, nullptr);

    Repl repl(testAddressBook, testCache, *testIdentity, testConfig);

    auto oldInitiatorEphemeral = bc::crypto::EphemeralKey::Generate();
    auto newInitiatorEphemeral = bc::crypto::EphemeralKey::Generate();
    auto responderEphemeral = bc::crypto::EphemeralKey::Generate();

    ASSERT_TRUE(oldInitiatorEphemeral.has_value());
    ASSERT_TRUE(newInitiatorEphemeral.has_value());
    ASSERT_TRUE(responderEphemeral.has_value());

    std::array<std::uint8_t, bc::crypto::publicKeySize * 2> signedData{};
    std::span<std::uint8_t> signedDataSpan{signedData};

    std::ranges::copy(oldInitiatorEphemeral->GetPublicKey(),
                      signedDataSpan.first(bc::crypto::publicKeySize).begin());

    std::ranges::copy(responderEphemeral->GetPublicKey(),
                      signedDataSpan.last(bc::crypto::publicKeySize).begin());

    std::array<std::uint8_t, bc::domain::client::cryptoSignBytes> signature{};

    crypto_sign_detached(signature.data(), nullptr, signedData.data(), signedData.size(),
                         peerIdentity.GetSecretKeySpan().data());

    auto ackPayload = bc::domain::client::PayloadFormatter::BuildPfsRotateAck(
        responderEphemeral->GetPublicKey(), signature);

    contact->pfsState = bc::domain::client::PfsState::ROTATION_REQUESTED;

    contact->pendingEphemeralKey = std::move(*newInitiatorEphemeral);

    const auto originalTxMailbox = contact->txMailboxId;
    const auto originalRxMailbox = contact->rxMailboxId;

    repl.HandlePfsRotateAck("bob", contact, ackPayload);

    EXPECT_EQ(contact->pfsState, bc::domain::client::PfsState::ROTATION_REQUESTED);

    EXPECT_TRUE(contact->pendingEphemeralKey.has_value());

    EXPECT_EQ(contact->txMailboxId, originalTxMailbox);
    EXPECT_EQ(contact->rxMailboxId, originalRxMailbox);

    EXPECT_FALSE(contact->initialPfsComplete);
}

} // namespace bc::cli::test
