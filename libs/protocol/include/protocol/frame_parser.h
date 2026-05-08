#ifndef BC_LIBS_PROTOCOL_INCLUDE_FRAMEPARSER_H_
#define BC_LIBS_PROTOCOL_INCLUDE_FRAMEPARSER_H_

#include <cstdint>
#include <optional>
#include <protocol/frame.h>
#include <protocol/frame_parser_state.h>
#include <span>

namespace bc::protocol {

using Payload = std::vector<std::uint8_t>;
using PayloadLength = std::uint32_t;

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

private:
    ParserState currentState{ParserState::READING_HEADER};
    std::vector<std::uint8_t> headerBuffer;
    Payload payloadBuffer;
    PayloadLength expectedPayloadLength{0};
};

} // namespace bc::protocol

#endif // BC_LIBS_PROTOCOL_INCLUDE_FRAMEPARSER_H_
