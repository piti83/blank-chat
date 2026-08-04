#ifndef BC_LIBS_PROTOCOL_INCLUDE_FRAME_H_
#define BC_LIBS_PROTOCOL_INCLUDE_FRAME_H_

#include <protocol/mailbox_id.h>
#include <protocol/protocol_types.h>

namespace bc::protocol {

class Frame
{
public:
    Frame() = delete;
    Frame(const Frame&) = delete;
    auto operator=(const Frame&) -> Frame& = delete;

    Frame(Frame&&) noexcept = default;
    auto operator=(Frame&&) noexcept -> Frame& = default;

    ~Frame() = default;

    [[nodiscard]] static auto CreatePush(const MailboxID& mailboxId, Payload&& payload) -> Frame;
    [[nodiscard]] static auto CreatePoll(const MailboxID& mailboxId) -> Frame;
    [[nodiscard]] static auto CreateAck(const MailboxID& mailboxId, Payload&& payload) -> Frame;

    [[nodiscard]] auto Serialize() const -> RawFrame;

    [[nodiscard]] auto GetActionType() const noexcept -> ActionType;
    [[nodiscard]] auto GetMailboxID() const noexcept -> const MailboxID&;
    [[nodiscard]] auto GetPayloadLength() const noexcept -> PayloadLength;
    [[nodiscard]] auto GetPayload() const noexcept -> const Payload&;

    [[nodiscard]] auto ExtractPayload() && noexcept -> Payload;

    [[nodiscard]] static auto CreateAuthChallenge(const MailboxID& id, Payload challengeData)
        -> Frame;
    [[nodiscard]] static auto CreateAuthResponse(const MailboxID& id, Payload responseData)
        -> Frame;

private:
    Frame(ActionType action, const MailboxID& mailboxId, Payload&& payload);

    ActionType action;
    MailboxID mailboxId;
    PayloadLength payloadLength;
    Payload payload;
};

} // namespace bc::protocol

#endif // BC_LIBS_PROTOCOL_INCLUDE_FRAME_H_
