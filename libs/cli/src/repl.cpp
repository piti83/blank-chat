#include "cli/repl.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>

#include <sodium.h>

#include <crypto/bip39.h>
#include <protocol/action_type.h>
#include <protocol/frame.h>
#include <protocol/mailbox_id.h>

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

Repl::Repl(bc::domain::client::AddressBook& addressBook, const bc::crypto::IdentityKey& identity,
           std::string_view torHost, std::uint16_t torPort)
    : client(ioContext, torHost, torPort), addressBook(addressBook), identity(identity)
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
        } else if (cmd == "poll") {
            HandlePoll();
        } else if (cmd == "mykey") {
            HandleMyKey();
        } else if (cmd == "add") {
            HandleAddContact();
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
    } else {
        std::cout << "Failed to add contact. Mathematically invalid cryptographic key.\n";
    }

    sodium_memzero(mnemonicStr.data(), mnemonicStr.size());
}

auto Repl::HandleConnect() -> void
{
    std::string host;
    std::uint16_t port = 0;
    if (!(std::cin >> host >> port)) {
        return;
    }
    std::cout << "Connecting to " << host << ":" << port << " via Tor proxy...\n";
    if (client.Connect(host, port)) {
        std::cout << "Successfully connected to the Onion network.\n";
    } else {
        std::cout << "Failed to connect (ensure Tor daemon is running).\n";
    }
}

auto Repl::HandleSend() -> void
{
    std::string alias;
    if (!(std::cin >> alias)) {
        return;
    }

    auto payload = ReadSecurePayload();

    const auto* contact = addressBook.GetContact(alias);
    if (!static_cast<bool>(contact)) {
        std::cout << "Error: Contact '" << alias << "' not found in address book.\n";
        std::ranges::fill(payload, 0);
        return;
    }

    if (client.SendFrame(
            bc::protocol::Frame::CreatePush(contact->txMailboxId, std::move(payload)))) {
        std::cout << "Frame serialized and transmitted securely to " << alias << ".\n";
    } else {
        std::cout << "Network error during transmission.\n";
    }
}

auto Repl::HandlePoll() -> void
{
    std::string alias;
    if (!(std::cin >> alias)) {
        return;
    }
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    const auto* contact = addressBook.GetContact(alias);
    if (!static_cast<bool>(contact)) {
        std::cout << "Error: Contact '" << alias << "' not found in address book.\n";
        return;
    }

    if (client.SendFrame(bc::protocol::Frame::CreatePoll(contact->rxMailboxId))) {
        std::cout << "POLL request sent. Waiting for server response...\n";
        if (auto response = client.ReceiveFrame()) {
            if (response->GetActionType() == bc::protocol::ActionType::PUSH) {
                std::cout << "New message from " << alias << ": ";
                for (auto byte : response->GetPayload()) {
                    std::cout << static_cast<char>(byte);
                }
                std::cout << "\n";
            } else {
                std::cout << "Mailbox is empty.\n";
            }
        }
    }
}

} // namespace bc::cli
