#include "protocol/frame.h"

#include <utility>

namespace bc::protocol {

Frame::Frame(ActionType action, const MailboxID& mailboxId, Payload&& payload)
    : action(action), mailboxId(mailboxId),
      payloadLength(static_cast<PayloadLength>(payload.size())), payload(std::move(payload))
{
}

auto Frame::CreatePush(const MailboxID& mailboxId, Payload&& payload) -> Frame
{
    return {ActionType::PUSH, mailboxId, std::move(payload)};
}

auto Frame::CreatePoll(const MailboxID& mailboxId) -> Frame
{
    return {ActionType::POLL, mailboxId, Payload{}};
}

auto Frame::CreateAck(const MailboxID& mailboxId, Payload&& payload) -> Frame
{
    return {ActionType::ACK, mailboxId, std::move(payload)};
}

auto Frame::Serialize() const -> RawFrame
{
    RawFrame buffer;

    buffer.reserve(actionTypeSize + mailboxIdSize + sizeof(payloadLength) + payload.size());

    buffer.push_back(static_cast<uint8_t>(action));

    buffer.insert(buffer.end(), mailboxId.begin(), mailboxId.end());

    // NOLINTBEGIN(readability-magic-numbers)
    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
    buffer.push_back(payloadLength & 0xFF);
    buffer.push_back((payloadLength >> 8) & 0xFF);
    buffer.push_back((payloadLength >> 16) & 0xFF);
    buffer.push_back((payloadLength >> 24) & 0xFF);
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
    // NOLINTEND(readability-magic-numbers)

    if (payloadLength > 0 && !payload.empty()) {
        buffer.insert(buffer.end(), payload.begin(), payload.end());
    }

    return buffer;
}

auto Frame::GetActionType() const noexcept -> ActionType
{
    return action;
}

auto Frame::GetMailboxID() const noexcept -> const MailboxID&
{
    return mailboxId;
}

auto Frame::GetPayloadLength() const noexcept -> PayloadLength
{
    return payloadLength;
}

auto Frame::GetPayload() const noexcept -> const Payload&
{
    return payload;
}

auto Frame::ExtractPayload() && noexcept -> Payload
{
    return std::move(payload);
}

} // namespace bc::protocol
