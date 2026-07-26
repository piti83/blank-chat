#include "cli/repl.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>

#include <sodium.h>

#include <client/payload_formatter.h>
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
           const bc::domain::client::ClientConfig& configParam)
    : client(ioContext, configParam.networkConfig.torSocksHost,
             configParam.networkConfig.torSocksPort),
      addressBook(addressBook), cache(cache), identity(identity), config(configParam)
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

    if (client.Connect(config.relayConfig.onionAddress, config.relayConfig.onionPort)) {
        std::cout << "Successfully connected.\n";
        contactAliases = addressBook.GetAllAliases();

        if (config.obfuscationConfig.mode == "poisson") {
            BC_WARN(
                "Poisson obfuscation is configured but not yet implemented. Falling back to CBR.");
        }

        client.StartAsyncEngine(
            [this]() -> bc::protocol::Frame { return GetNextFrameForCBR(); },
            [this](bc::protocol::Frame&& frame) -> void { OnFrameReceived(std::move(frame)); },
            std::chrono::milliseconds(config.obfuscationConfig.cbr_interval_ms));

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

    auto rawPayload = ReadSecurePayload();

    auto* contact = addressBook.GetMutableContact(alias);
    if (contact == nullptr) {
        std::cout << "Error: Contact '" << alias << "' not found in address book.\n";
        sodium_memzero(rawPayload.data(), rawPayload.size());
        return;
    }

    std::string msgId = bc::core::HashPayload(rawPayload);
    bc::domain::client::CacheEntry entry{
        .id = msgId,
        .timestamp = static_cast<std::uint64_t>(std::time(nullptr)),
        .direction = bc::domain::client::MessageDirection::OUTBOUND,
        .alias = alias,
        .status = bc::domain::client::MessageStatus::PENDING_ACK,
        .payload = rawPayload};
    cache.AppendMessage(entry);

    if (contact->pfsState == bc::domain::client::PfsState::ROTATION_REQUESTED ||
        contact->pfsState == bc::domain::client::PfsState::ROTATION_RESPONDING) {
        std::cout << "Key rotation in progress. Message queued.\n";
        contact->pendingMessages.push(std::move(rawPayload));
        return;
    }

    if (config.securityConfig.pfsMessageInterval > 0 &&
        contact->messageCounter >= config.securityConfig.pfsMessageInterval) {

        std::cout << "PFS threshold reached. Initiating key rotation...\n";

        contact->pendingMessages.push(std::move(rawPayload));

        auto ephemeralOpt = bc::crypto::EphemeralKey::Generate();
        if (!ephemeralOpt) {
            BC_ERROR("Failed to generate ephemeral key");
            return;
        }

        std::array<std::uint8_t, bc::domain::client::cryptoSignBytes> signature{};
        crypto_sign_detached(signature.data(), nullptr, ephemeralOpt->GetPublicKey().data(),
                             ephemeralOpt->GetPublicKey().size(),
                             identity.GetSecretKeySpan().data());

        auto formattedPayload = bc::domain::client::PayloadFormatter::BuildPfsRotateRequest(
            ephemeralOpt->GetPublicKey(), signature);

        auto ciphertextOpt = bc::crypto::SymmetricCipher::EncryptWithPadding(
            contact->txKey.AsSpan(), formattedPayload);
        sodium_memzero(formattedPayload.data(), formattedPayload.size());

        if (!ciphertextOpt) {
            BC_ERROR("Encryption failed for PFS Request.");
            return;
        }

        auto frame =
            bc::protocol::Frame::CreatePush(contact->txMailboxId, std::move(*ciphertextOpt));
        {
            std::scoped_lock lock(outboxMutex);
            outbox.push(std::move(frame));
        }

        contact->pfsState = bc::domain::client::PfsState::ROTATION_REQUESTED;
        contact->pendingEphemeralKey = std::move(*ephemeralOpt);
        contact->messageCounter = 0;
        PrintThreadSafe("Rotation request queued for transmission.\n");
        return;
    }

    contact->messageCounter++;
    auto formattedPayload = bc::domain::client::PayloadFormatter::BuildTextMessage(rawPayload);
    sodium_memzero(rawPayload.data(), rawPayload.size());

    auto ciphertextOpt =
        bc::crypto::SymmetricCipher::EncryptWithPadding(contact->txKey.AsSpan(), formattedPayload);
    sodium_memzero(formattedPayload.data(), formattedPayload.size());

    if (!ciphertextOpt) {
        std::cout << "Encryption failed for contact '" << alias << "'. Message dropped.\n";
        return;
    }

    auto frame = bc::protocol::Frame::CreatePush(contact->txMailboxId, std::move(*ciphertextOpt));
    {
        std::scoped_lock lock(outboxMutex);
        outbox.push(std::move(frame));
    }

    PrintThreadSafe("Message queued for transmission.\n");
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
    auto rxId = frame.GetMailboxID();
    std::string alias = addressBook.GetAliasByRxMailboxId(rxId);

    if (alias.empty()) {
        PrintThreadSafe("Received message for unknown MailboxID. Dropping silently.\n");
        return;
    }

    auto* contact = addressBook.GetMutableContact(alias);
    if (contact == nullptr) {
        return;
    }

    auto actionType = frame.GetActionType();

    if (actionType == bc::protocol::ActionType::PUSH) {
        auto payload = std::move(frame).ExtractPayload();
        auto plaintextOpt =
            bc::crypto::SymmetricCipher::DecryptAndUnpad(contact->rxKey.AsSpan(), payload);

        if (!plaintextOpt) {
            PrintThreadSafe("Malformed or tampered PUSH message dropped silently.\n");
            return;
        }

        auto opcodeOpt = bc::domain::client::PayloadFormatter::ExtractOpcode(*plaintextOpt);
        if (opcodeOpt) {
            if (*opcodeOpt == bc::domain::client::PayloadOpcode::TEXT_MESSAGE) {
                if (auto msgDataOpt =
                        bc::domain::client::PayloadFormatter::ParseTextMessage(*plaintextOpt)) {
                    HandleTextMessage(alias, contact, *plaintextOpt, *msgDataOpt);
                }
            } else if (*opcodeOpt == bc::domain::client::PayloadOpcode::PFS_ROTATE_REQUEST) {
                HandlePfsRotateRequest(alias, contact, *plaintextOpt);
            } else if (*opcodeOpt == bc::domain::client::PayloadOpcode::PFS_ROTATE_ACK) {
                HandlePfsRotateAck(alias, contact, *plaintextOpt);
            }
        }

        sodium_memzero(plaintextOpt->data(), plaintextOpt->size());
    } else if (actionType == bc::protocol::ActionType::ACK) {
        auto payload = std::move(frame).ExtractPayload();
        auto plaintextOpt =
            bc::crypto::SymmetricCipher::DecryptAndUnpad(contact->rxKey.AsSpan(), payload);

        if (!plaintextOpt) {
            PrintThreadSafe("Malformed or tampered ACK message dropped silently.\n");
            return;
        }

        std::string msgId(plaintextOpt->begin(), plaintextOpt->end());
        cache.UpdateMessageStatus(alias, msgId, bc::domain::client::MessageStatus::DELIVERED);
        PrintThreadSafe(std::format("\n[System] Message DELIVERED to {}\n>>> ", alias));

        sodium_memzero(plaintextOpt->data(), plaintextOpt->size());
    }
}

auto Repl::PrintThreadSafe(std::string_view msg) -> void
{
    std::scoped_lock lock(stdoutMutex);
    std::cout << msg << std::flush;
}

auto Repl::HandleTextMessage(std::string_view alias, const bc::domain::client::Contact* contact,
                             const std::vector<std::uint8_t>& plaintext,
                             std::span<const std::uint8_t> msgData) -> void
{
    std::string msgStr(msgData.begin(), msgData.end());
    PrintThreadSafe(std::format("\n[+] New message from {}: {}\n>>> ", alias, msgStr));

    std::string msgId = bc::core::HashPayload(plaintext);
    bc::domain::client::CacheEntry entry{.id = msgId,
                                         .timestamp =
                                             static_cast<std::uint64_t>(std::time(nullptr)),
                                         .direction = bc::domain::client::MessageDirection::INBOUND,
                                         .alias = std::string(alias),
                                         .status = bc::domain::client::MessageStatus::DELIVERED,
                                         .payload = plaintext};
    cache.AppendMessage(entry);

    bc::protocol::Payload ackPayload(msgId.begin(), msgId.end());
    auto ackCiphertextOpt =
        bc::crypto::SymmetricCipher::EncryptWithPadding(contact->txKey.AsSpan(), ackPayload);

    if (ackCiphertextOpt) {
        auto ackFrame =
            bc::protocol::Frame::CreateAck(contact->txMailboxId, std::move(*ackCiphertextOpt));
        std::scoped_lock lock(outboxMutex);
        outbox.push(std::move(ackFrame));
        PrintThreadSafe("[System] Encrypted ACK queued for transmission.\n>>> ");
    }
}

auto Repl::HandlePfsRotateRequest(std::string_view alias, bc::domain::client::Contact* contact,
                                  const std::vector<std::uint8_t>& plaintext) -> void
{
    auto reqDataOpt = bc::domain::client::PayloadFormatter::ParsePfsRotateRequest(plaintext);
    if (!reqDataOpt) {
        return;
    }

    if (crypto_sign_verify_detached(
            reqDataOpt->signature.data(), reqDataOpt->ephemeralPublicKey.data(),
            reqDataOpt->ephemeralPublicKey.size(), contact->publicKey.data()) != 0) {
        BC_ERROR("Invalid signature on PFS_ROTATE_REQUEST from {}. Potential MITM attack!", alias);
        return;
    }

    auto myEphemeralOpt = bc::crypto::EphemeralKey::Generate();
    if (!myEphemeralOpt) {
        return;
    }

    std::array<std::uint8_t, bc::domain::client::cryptoSignBytes> mySignature{};
    crypto_sign_detached(mySignature.data(), nullptr, myEphemeralOpt->GetPublicKey().data(),
                         myEphemeralOpt->GetPublicKey().size(), identity.GetSecretKeySpan().data());

    auto ackPayload = bc::domain::client::PayloadFormatter::BuildPfsRotateAck(
        myEphemeralOpt->GetPublicKey(), mySignature);

    auto ciphertextAckOpt =
        bc::crypto::SymmetricCipher::EncryptWithPadding(contact->txKey.AsSpan(), ackPayload);
    sodium_memzero(ackPayload.data(), ackPayload.size());

    if (ciphertextAckOpt) {
        auto frameAck =
            bc::protocol::Frame::CreatePush(contact->txMailboxId, std::move(*ciphertextAckOpt));
        {
            std::scoped_lock lock(outboxMutex);
            outbox.push(std::move(frameAck));
        }

        std::array<std::uint8_t, crypto_scalarmult_BYTES> sharedSecret{};
        if (crypto_scalarmult(sharedSecret.data(), myEphemeralOpt->GetSecretKeySpan().data(),
                              reqDataOpt->ephemeralPublicKey.data()) == 0) {

            crypto_generichash(contact->rxKey.AsMutableSpan().data(), contact->rxKey.Size(),
                               sharedSecret.data(), sharedSecret.size(), nullptr, 0);
            crypto_generichash(contact->txKey.AsMutableSpan().data(), contact->txKey.Size(),
                               sharedSecret.data(), sharedSecret.size(), nullptr, 0);

            contact->messageCounter = 0;
            PrintThreadSafe(std::format("[System] Key rotation completed with {}.\n>>> ", alias));
        }
        sodium_memzero(sharedSecret.data(), sharedSecret.size());
    }
}

auto Repl::HandlePfsRotateAck(std::string_view alias, bc::domain::client::Contact* contact,
                              const std::vector<std::uint8_t>& plaintext) -> void
{
    if (contact->pfsState != bc::domain::client::PfsState::ROTATION_REQUESTED ||
        !contact->pendingEphemeralKey.has_value()) {
        PrintThreadSafe("Unexpected PFS_ROTATE_ACK dropped.\n>>> ");
        return;
    }

    auto ackDataOpt = bc::domain::client::PayloadFormatter::ParsePfsRotateAck(plaintext);
    if (!ackDataOpt) {
        return;
    }

    if (crypto_sign_verify_detached(
            ackDataOpt->signature.data(), ackDataOpt->ephemeralPublicKey.data(),
            ackDataOpt->ephemeralPublicKey.size(), contact->publicKey.data()) != 0) {
        BC_ERROR("Invalid signature on PFS_ROTATE_ACK from {}. Potential MITM attack!", alias);
        return;
    }

    std::array<std::uint8_t, crypto_scalarmult_BYTES> sharedSecret{};
    if (crypto_scalarmult(sharedSecret.data(),
                          contact->pendingEphemeralKey->GetSecretKeySpan().data(),
                          ackDataOpt->ephemeralPublicKey.data()) == 0) {

        crypto_generichash(contact->rxKey.AsMutableSpan().data(), contact->rxKey.Size(),
                           sharedSecret.data(), sharedSecret.size(), nullptr, 0);
        crypto_generichash(contact->txKey.AsMutableSpan().data(), contact->txKey.Size(),
                           sharedSecret.data(), sharedSecret.size(), nullptr, 0);

        contact->pfsState = bc::domain::client::PfsState::IDLE;
        contact->pendingEphemeralKey = std::nullopt;
        contact->messageCounter = 0;

        PrintThreadSafe(
            std::format("[System] Key rotation completed with {}. Resuming queue...\n>>> ", alias));

        while (!contact->pendingMessages.empty()) {
            auto pendingRaw = std::move(contact->pendingMessages.front());
            contact->pendingMessages.pop();

            auto pendingFormatted =
                bc::domain::client::PayloadFormatter::BuildTextMessage(pendingRaw);
            sodium_memzero(pendingRaw.data(), pendingRaw.size());

            auto pendingCipherOpt = bc::crypto::SymmetricCipher::EncryptWithPadding(
                contact->txKey.AsSpan(), pendingFormatted);
            sodium_memzero(pendingFormatted.data(), pendingFormatted.size());

            if (pendingCipherOpt) {
                auto queuedFrame = bc::protocol::Frame::CreatePush(contact->txMailboxId,
                                                                   std::move(*pendingCipherOpt));
                std::scoped_lock lock(outboxMutex);
                outbox.push(std::move(queuedFrame));
                contact->messageCounter++;
            }
        }
    }
    sodium_memzero(sharedSecret.data(), sharedSecret.size());
}

} // namespace bc::cli
