#ifndef BC_LIBS_PROTOCOL_INCLUDE_FRAMEPARSER_H_
#define BC_LIBS_PROTOCOL_INCLUDE_FRAMEPARSER_H_

#include <cstdint>
#include <optional>
#include <protocol/frame.h>
#include <protocol/frame_parser_state.h>
#include <span>

namespace bc::protocol {

class FrameParser
{
public:
    FrameParser() = default;
    ~FrameParser() = default;

    FrameParser(const FrameParser&) = delete;
    auto operator=(const FrameParser&) -> FrameParser& = delete;

    FrameParser(FrameParser&&) noexcept = default;
    auto operator=(FrameParser&&) noexcept -> FrameParser& = default;

    auto FeedBytes(std::span<const std::uint8_t> data) -> std::size_t;
    [[nodiscard]] auto TryExtractFrame() -> std::optional<Frame>;
    [[nodiscard]] auto HasError() const noexcept -> bool;

private:
    auto ParseHeader() -> void;

    ParserState currentState{ParserState::READING_HEADER};
    Payload headerBuffer;
    Payload payloadBuffer;

    std::optional<ActionType> currentAction;
    std::optional<MailboxID> currentMailbox;
    PayloadLength expectedPayloadLength{0};

    static constexpr std::size_t headerSize =
        actionTypeSize + mailboxIdSize + sizeof(PayloadLength);

    static constexpr PayloadLength maxPayloadSize = 1024 * 1024;
};

} // namespace bc::protocol

#endif // BC_LIBS_PROTOCOL_INCLUDE_FRAMEPARSER_H_
