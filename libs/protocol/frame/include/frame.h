#ifndef BC_LIBS_PROTOCOL_FRAME_INCLUDE_FRAME_H_
#define BC_LIBS_PROTOCOL_FRAME_INCLUDE_FRAME_H_

#include "action_type.h"

#include <array>
#include <cstdint>
#include <vector>

namespace bc::protocol::frame {

constexpr uint8_t mailboxIdSize = 16;

using MailboxID = std::array<std::uint8_t, mailboxIdSize>;
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

private:
    Frame(ActionType action, const MailboxID& mailboxId, Payload&& payload);

    ActionType action;
    MailboxID mailboxId;
    PayloadLength payloadLength;
    Payload payload;
};

} // namespace bc::protocol::frame

#endif // BC_LIBS_PROTOCOL_FRAME_INCLUDE_FRAME_H_
