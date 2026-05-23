#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <optional>

#include <protocol/action_type.h>
#include <protocol/frame.h>
#include <protocol/mailbox_id.h>

#include "cli/repl.h"

namespace {
constexpr std::size_t maxPayloadReserve = 4096;
constexpr std::size_t hexIdLength = 32;

auto HexCharToByte(char character) -> std::optional<std::uint8_t>
{
    if (character >= '0' && character <= '9') {
        return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f') {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
        return static_cast<std::uint8_t>(character - 'a' + 10);
    }
    if (character >= 'A' && character <= 'F') {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
        return static_cast<std::uint8_t>(character - 'A' + 10);
    }
    return std::nullopt;
}

auto ParseMailboxId(std::string_view hex) -> std::optional<bc::protocol::MailboxID>
{
    if (hex.length() != hexIdLength) {
        return std::nullopt;
    }
    std::array<std::uint8_t, bc::protocol::mailboxIdSize> bytes{};
    for (std::size_t i = 0; i < bc::protocol::mailboxIdSize; ++i) {
        auto highNibble = HexCharToByte(hex.at(2 * i));
        auto lowNibble = HexCharToByte(hex.at((2 * i) + 1));
        if (!highNibble || !lowNibble) {
            return std::nullopt;
        }
        bytes.at(i) = static_cast<std::uint8_t>((*highNibble << 4) | *lowNibble);
    }
    return bc::protocol::MailboxID(bytes);
}

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

Repl::Repl(std::string_view torHost, std::uint16_t torPort) : client(ioContext, torHost, torPort)
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
        } else {
            std::cout << "Unknown command.\n";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    }
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
    std::string hexId;
    if (!(std::cin >> hexId)) {
        return;
    }
    auto mailboxIdOpt = ParseMailboxId(hexId);
    auto payload = ReadSecurePayload();
    if (!mailboxIdOpt) {
        std::cout << "Error: Invalid Mailbox ID (32 hex characters required).\n";
        std::ranges::fill(payload, 0);
        return;
    }
    if (client.SendFrame(bc::protocol::Frame::CreatePush(*mailboxIdOpt, std::move(payload)))) {
        std::cout << "Frame serialized and transmitted.\n";
    } else {
        std::cout << "Network error during transmission.\n";
    }
}

auto Repl::HandlePoll() -> void
{
    std::string hexId;
    if (!(std::cin >> hexId)) {
        return;
    }
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    auto mailboxIdOpt = ParseMailboxId(hexId);
    if (!mailboxIdOpt) {
        return;
    }
    if (client.SendFrame(bc::protocol::Frame::CreatePoll(*mailboxIdOpt))) {
        std::cout << "POLL request sent. Waiting for server response...\n";
        if (auto response = client.ReceiveFrame()) {
            if (response->GetActionType() == bc::protocol::ActionType::PUSH) {
                std::cout << "New message received: ";
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
