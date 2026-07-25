#include "cli/repl.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>

#include <sodium.h>

#include <core/logger.h>
#include <core/string_utils.h>
#include <crypto/bip39.h>
#include <crypto/symmetric_cipher.h>
#include <network/tcp_client.h>
#include <protocol/frame.h>
#include <protocol/mailbox_id.h>
#include <protocol/protocol_types.h>

#include "network/network_types.h"
#include <cli/cli_types.h>

namespace {

auto ReadSecurePayload() -> bc::protocol::Payload
{
    bc::protocol::Payload payload;
    payload.reserve(bc::cli::maxPayloadReserve);
    char character = 0;
    if (std::cin.peek() == ' ') {
        std::cin.get(character);
    }
    while (std::cin.get(character) && character != '\n') {
        payload.push_back(static_cast<std::uint8_t>(character));
    }
    return payload;
}

} // namespace

namespace bc::cli {

Repl::Repl(bc::domain::client::AddressBook& addressBook,
           bc::domain::client::ConversationCache& cache, const bc::crypto::IdentityKey& identity,
           std::string_view torHost, std::uint16_t torPort, std::string relayAddress,
           std::uint16_t relayPort)
    : client(ioContext, torHost, torPort), addressBook(addressBook), cache(cache),
      identity(identity), relayAddress(std::move(relayAddress)), relayPort(relayPort)
{
}

auto Repl::Run() -> void
{
    std::cout << "--- Blank Chat ---\n";
    while (true) {
        std::cout << ">>> ";
        std::string cmd;
        if (!(std::cin >> cmd) || cmd == "exit") {
            break;
        }

        if (cmd == "connect") {
            HandleConnect();
        } else if (cmd == "send") {
            HandleSend();
        } else if (cmd == "mykey") {
            HandleMyKey();
        } else if (cmd == "add") {
            HandleAddContact();
        } else if (cmd == "history") {
            HandleHistory();
        } else if (cmd == "list") {
            HandleList();
        } else {
            std::cout << "Unknown command.\n";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    }
}

Repl::~Repl()
{
    ioContext.stop();
    if (asioThread.joinable()) {
        asioThread.join();
    }
}

auto Repl::HandleConnect() -> void
{
    std::cout << "Connecting via Tor proxy...\n";
    if (client.Connect(relayAddress, relayPort)) {
        std::cout << "Successfully connected.\n";
        contactAliases = addressBook.GetAllAliases();

        client.StartAsyncEngine(
            [this]() -> bc::protocol::Frame { return GetNextFrameForCBR(); },
            [this](bc::protocol::Frame&& frame) -> void { OnFrameReceived(std::move(frame)); },
            std::chrono::milliseconds(bc::network::defaultCbrIntervalMs));

        ioContext.restart();

        asioThread = std::thread([this]() -> void {
            auto workGuard = boost::asio::make_work_guard(ioContext);
            ioContext.run();
        });
    } else {
        std::cout << "Failed to connect.\n";
    }
}

auto Repl::HandleSend() -> void
{
    std::string alias;
    if (!(std::cin >> alias))
        return;

    auto payload = ReadSecurePayload();

    const auto* contact = addressBook.GetContact(alias);
    if (contact == nullptr) {
        std::cout << "Error: Contact '" << alias << "' not found in address book.\n";
        sodium_memzero(payload.data(), payload.size());
        return;
    }

    std::string msgId = bc::core::HashPayload(payload);

    bc::domain::client::CacheEntry entry{
        .id = msgId,
        .timestamp = static_cast<std::uint64_t>(std::time(nullptr)),
        .direction = bc::domain::client::MessageDirection::OUTBOUND,
        .alias = alias,
        .status = bc::domain::client::MessageStatus::PENDING_ACK,
        .payload = payload};
    cache.AppendMessage(entry);

    auto ciphertextOpt =
        bc::crypto::SymmetricCipher::EncryptWithPadding(contact->txKey.AsSpan(), payload);

    sodium_memzero(payload.data(), payload.size());

    if (!ciphertextOpt) {
        std::cout << "Encryption failed for contact '" << alias << "'. Message dropped.\n";
        return;
    }

    auto frame = bc::protocol::Frame::CreatePush(contact->txMailboxId, std::move(*ciphertextOpt));

    {
        std::scoped_lock lock(outboxMutex);
        outbox.push(std::move(frame));
    }
    PrintThreadSafe("Message added to Outbox 🔒 Will be transmitted on next CBR tick.\n");
}

auto Repl::HandleHistory() -> void
{
    std::string alias;
    if (!(std::cin >> alias)) {
        return;
    }

    auto history = cache.LoadHistory(alias);

    std::cout << "--- History for " << alias << " ---\n";
    for (const auto& entry : history) {
        std::cout << "["
                  << (entry.direction == bc::domain::client::MessageDirection::INBOUND ? "IN"
                                                                                       : "OUT")
                  << "] "
                  << "["
                  << (entry.status == bc::domain::client::MessageStatus::PENDING_ACK ? "WAIT"
                                                                                     : "OK")
                  << "] " << std::string(entry.payload.begin(), entry.payload.end()) << "\n";
    }
}

auto Repl::HandleList() -> void
{
    auto aliases = addressBook.GetAllAliases();
    if (aliases.empty()) {
        std::cout << "Address book is empty.\n";
        return;
    }

    std::cout << "--- Contacts ---\n";
    for (const auto& a : aliases) {
        std::cout << "- " << a << "\n";
    }
}

auto Repl::HandleMyKey() -> void
{
    std::cout << "Your Identity Key (BIP39 Mnemonic) for OOB exchange:\n";
    auto mnemonic = crypto::bip39::Encode(identity.GetPublicKey());
    std::cout << mnemonic.StringView() << "\n";
}

auto Repl::HandleAddContact() -> void
{
    std::string alias;
    std::string mnemonicStr;

    if (!(std::cin >> alias >> mnemonicStr)) {
        return;
    }

    auto pubKeyOpt = bc::crypto::bip39::Decode(mnemonicStr);

    if (!pubKeyOpt) {
        std::cout << "Error: Invalid BIP39 Mnemonic (Check spelling, dashes, and checksum).\n";
        sodium_memzero(mnemonicStr.data(), mnemonicStr.size());
        return;
    }

    if (addressBook.AddContact(alias, *pubKeyOpt, std::nullopt)) {
        std::cout << "Contact '" << alias << "' added successfully. Mailboxes mapped.\n";

        {
            std::scoped_lock lock(outboxMutex);
            if (std::ranges::find(contactAliases, alias) == contactAliases.end()) {
                contactAliases.push_back(alias);
            }
        }

    } else {
        std::cout << "Failed to add contact. Mathematically invalid cryptographic key.\n";
    }

    sodium_memzero(mnemonicStr.data(), mnemonicStr.size());
}

auto Repl::GetNextFrameForCBR() -> bc::protocol::Frame
{
    std::scoped_lock lock(outboxMutex);

    if (!outbox.empty()) {
        auto frame = std::move(outbox.front());
        outbox.pop();
        return frame;
    }

    if (contactAliases.empty()) {
        bc::protocol::MailboxID dummyId;
        dummyId.Fill(0x00);
        return bc::protocol::Frame::CreatePoll(dummyId);
    }

    const std::string& alias = contactAliases.at(currentPollIndex);
    currentPollIndex = (currentPollIndex + 1) % contactAliases.size();

    const auto* contact = addressBook.GetContact(alias);
    if (contact != nullptr) {
        return bc::protocol::Frame::CreatePoll(contact->rxMailboxId);
    }

    bc::protocol::MailboxID dummyId;
    dummyId.Fill(0x00);
    return bc::protocol::Frame::CreatePoll(dummyId);
}

auto Repl::OnFrameReceived(bc::protocol::Frame&& frame) -> void
{
    if (frame.GetActionType() == bc::protocol::ActionType::PUSH) {

        std::string alias = addressBook.GetAliasByRxMailboxId(frame.GetMailboxID());
        if (alias.empty()) {
            PrintThreadSafe("Received message for unknown MailboxID.\n");
            return;
        }

        const auto* contact = addressBook.GetContact(alias);
        if (contact == nullptr) {
            return;
        }

        bc::protocol::Payload ciphertext = std::move(frame).ExtractPayload();

        auto plaintextOpt =
            bc::crypto::SymmetricCipher::DecryptAndUnpad(contact->rxKey.AsSpan(), ciphertext);

        if (!plaintextOpt) {
            PrintThreadSafe("\nMalformed or tampered PUSH message dropped silently.\n>>> ");
            return;
        }

        bc::protocol::Payload plaintext = std::move(*plaintextOpt);
        std::string msgId = bc::core::HashPayload(plaintext);

        bc::domain::client::CacheEntry entry{
            .id = msgId,
            .timestamp = static_cast<std::uint64_t>(std::time(nullptr)),
            .direction = bc::domain::client::MessageDirection::INBOUND,
            .alias = alias,
            .status = bc::domain::client::MessageStatus::DELIVERED,
            .payload = plaintext};
        cache.AppendMessage(entry);

        PrintThreadSafe("\nNew message from " + alias + ": ");
        for (auto byte : plaintext) {
            PrintThreadSafe(std::string(1, static_cast<char>(byte)));
        }
        PrintThreadSafe("\n>>> ");

        bc::protocol::Payload ackPlaintext(msgId.begin(), msgId.end());

        auto ackCiphertextOpt =
            bc::crypto::SymmetricCipher::EncryptWithPadding(contact->txKey.AsSpan(), ackPlaintext);

        sodium_memzero(plaintext.data(), plaintext.size());
        sodium_memzero(ackPlaintext.data(), ackPlaintext.size());

        if (ackCiphertextOpt) {
            auto ackFrame =
                bc::protocol::Frame::CreateAck(contact->txMailboxId, std::move(*ackCiphertextOpt));
            std::scoped_lock lock(outboxMutex);
            outbox.push(std::move(ackFrame));
            PrintThreadSafe("Encrypted ACK queued for transmission.\n>>> ");
        }

    } else if (frame.GetActionType() == bc::protocol::ActionType::ACK) {

        std::string alias = addressBook.GetAliasByRxMailboxId(frame.GetMailboxID());
        if (alias.empty()) {
            return;
        }

        const auto* contact = addressBook.GetContact(alias);
        if (contact == nullptr) {
            return;
        }

        bc::protocol::Payload ackCiphertext = std::move(frame).ExtractPayload();

        auto ackPlaintextOpt =
            bc::crypto::SymmetricCipher::DecryptAndUnpad(contact->rxKey.AsSpan(), ackCiphertext);

        if (!ackPlaintextOpt) {
            PrintThreadSafe("\nMalformed or tampered ACK message dropped silently.\n>>> ");
            return;
        }

        std::string msgId(ackPlaintextOpt->begin(), ackPlaintextOpt->end());
        sodium_memzero(ackPlaintextOpt->data(), ackPlaintextOpt->size());

        cache.UpdateMessageStatus(alias, msgId, bc::domain::client::MessageStatus::DELIVERED);

        PrintThreadSafe("\nMessage DELIVERED 🔒\n>>> ");
    }
}

auto Repl::PrintThreadSafe(std::string_view msg) -> void
{
    std::scoped_lock lock(stdoutMutex);
    std::cout << msg << std::flush;
}

} // namespace bc::cli
