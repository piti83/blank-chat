#include <algorithm>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>

#include <sodium.h>

#include <core/string_utils.h>
#include <crypto/bip39.h>
#include <protocol/action_type.h>
#include <protocol/frame.h>
#include <protocol/mailbox_id.h>

#include "cli/repl.h"
#include "network/tcp_client.h"

namespace {

constexpr std::size_t maxPayloadReserve = 4096;

auto ReadSecurePayload() -> bc::protocol::Payload
{
    bc::protocol::Payload payload;
    payload.reserve(maxPayloadReserve);
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

Repl::~Repl()
{
    ioContext.stop();
    if (asioThread.joinable()) {
        asioThread.join();
    }
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
        } else {
            std::cout << "Unknown command.\n";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
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

        bc::protocol::Payload payload = std::move(frame).ExtractPayload();

        PrintThreadSafe("\nNew message from " + alias + ": ");
        for (auto byte : payload) {
            PrintThreadSafe(std::string(1, static_cast<char>(byte)));
        }
        PrintThreadSafe("\n>>> ");

        const auto* contact = addressBook.GetContact(alias);
        if (contact != nullptr) {
            std::string msgId = bc::core::HashPayload(payload);
            bc::protocol::Payload ackPayload(msgId.begin(), msgId.end());
            auto ackFrame =
                bc::protocol::Frame::CreateAck(contact->txMailboxId, std::move(ackPayload));

            std::scoped_lock lock(outboxMutex);
            outbox.push(std::move(ackFrame));
            PrintThreadSafe("[Background] ACK queued for transmission.\n>>> ");
        }

    } else if (frame.GetActionType() == bc::protocol::ActionType::ACK) {
        PrintThreadSafe("\n[Background] Message DELIVERED.\n>>> ");
    }
}

auto Repl::PrintThreadSafe(std::string_view msg) -> void
{
    std::scoped_lock lock(stdoutMutex);
    std::cout << msg << std::flush;
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
            std::chrono::milliseconds(bc::network::defaultCbrInterval));

        asioThread = std::thread([this]() -> void {
            ioContext.restart();

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
        std::ranges::fill(payload, 0);
        return;
    }

    auto frame = bc::protocol::Frame::CreatePush(contact->txMailboxId, std::move(payload));

    {
        std::scoped_lock lock(outboxMutex);
        outbox.push(std::move(frame));
    }
    PrintThreadSafe("Message added to Outbox. Will be transmitted on next CBR tick.\n");
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

} // namespace bc::cli
