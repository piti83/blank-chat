#ifndef BC_LIBS_PROTOCOL_INCLUDE_FRAME_H_
#define BC_LIBS_PROTOCOL_INCLUDE_FRAME_H_

#include <protocol/action_type.h>

#include <cstdint>
#include <protocol/mailbox_id.h>
#include <vector>

namespace bc::protocol {

using Payload = std::vector<std::uint8_t>;
using PayloadLength = std::uint32_t;
using RawFrame = std::vector<std::uint8_t>;

class Frame
{
public:
    Frame() = delete;
    Frame(const Frame&) = delete;
    auto operator=(const Frame&) -> Frame& = delete;

    Frame(Frame&&) noexcept = default;
    auto operator=(Frame&&) noexcept -> Frame& = default;

    // TODO: Create custom destructor to wipe frame from ram
    ~Frame() = default;

    [[nodiscard]] static auto CreatePush(const MailboxID& mailboxId, Payload&& payload) -> Frame;
    [[nodiscard]] static auto CreatePoll(const MailboxID& mailboxId) -> Frame;

    [[nodiscard]] auto Serialize() const -> RawFrame;

    [[nodiscard]] auto GetActionType() const noexcept -> ActionType;
    [[nodiscard]] auto GetMailboxID() const noexcept -> const MailboxID&;
    [[nodiscard]] auto GetPayloadLength() const noexcept -> PayloadLength;
    [[nodiscard]] auto GetPayload() const noexcept -> const Payload&;

    [[nodiscard]] auto ExtractPayload() && noexcept -> Payload;

private:
    Frame(ActionType action, const MailboxID& mailboxId, Payload&& payload);

    ActionType action;
    MailboxID mailboxId;
    PayloadLength payloadLength;
    Payload payload;
};

} // namespace bc::protocol

#endif // BC_LIBS_PROTOCOL_INCLUDE_FRAME_H_
