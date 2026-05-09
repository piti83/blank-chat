#include <algorithm>
#include <protocol/frame_parser.h>

namespace bc::protocol {

auto FrameParser::FeedBytes(std::span<const std::uint8_t> data) -> std::size_t
{
    const std::size_t initialSize = data.size();

    while (!data.empty() && currentState != ParserState::FRAME_READY &&
           currentState != ParserState::ERROR_MALFORMED) {

        if (currentState == ParserState::READING_HEADER) {
            const std::size_t needed = headerSize - headerBuffer.size();
            const std::size_t toCopy = std::min(needed, data.size());

            auto toInsert = data.first(toCopy);
            headerBuffer.insert(headerBuffer.end(), toInsert.begin(), toInsert.end());

            data = data.subspan(toCopy);

            if (headerBuffer.size() == headerSize) {
                ParseHeader();
            }
        } else if (currentState == ParserState::READING_PAYLOAD) {
            const std::size_t needed = expectedPayloadLength - payloadBuffer.size();
            const std::size_t toCopy = std::min(needed, data.size());

            auto toInsert = data.first(toCopy);
            payloadBuffer.insert(payloadBuffer.end(), toInsert.begin(), toInsert.end());

            data = data.subspan(toCopy);

            if (payloadBuffer.size() == expectedPayloadLength) {
                currentState = ParserState::FRAME_READY;
            }
        }
    }

    return initialSize - data.size();
}

auto FrameParser::ParseHeader() -> void
{
    currentAction = static_cast<ActionType>(headerBuffer.at(0));

    std::span<const std::uint8_t, mailboxIdSize> mailboxSpan(headerBuffer.begin() + actionTypeSize,
                                                             mailboxIdSize);
    currentMailbox = MailboxID(mailboxSpan);

    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
    // NOLINTBEGIN(readability-magic-numbers)
    const std::size_t lengthOffset = actionTypeSize + mailboxIdSize;
    expectedPayloadLength = static_cast<PayloadLength>(headerBuffer.at(lengthOffset)) |
                            (static_cast<PayloadLength>(headerBuffer.at(lengthOffset + 1)) << 8) |
                            (static_cast<PayloadLength>(headerBuffer.at(lengthOffset + 2)) << 16) |
                            (static_cast<PayloadLength>(headerBuffer.at(lengthOffset + 3)) << 24);
    // NOLINTEND(readability-magic-numbers)
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

    if (expectedPayloadLength > maxPayloadSize) {
        currentState = ParserState::ERROR_MALFORMED;
        return;
    }

    if (expectedPayloadLength == 0) {
        currentState = ParserState::FRAME_READY;
    } else {
        payloadBuffer.reserve(expectedPayloadLength);
        currentState = ParserState::READING_PAYLOAD;
    }
}

auto FrameParser::TryExtractFrame() -> std::optional<Frame>
{
    if (currentState != ParserState::FRAME_READY) {
        return std::nullopt;
    }

    if (!currentAction.has_value() || !currentMailbox.has_value()) {
        currentState = ParserState::ERROR_MALFORMED;
        return std::nullopt;
    }

    std::optional<Frame> result;

    if (*currentAction == ActionType::PUSH) {
        result = Frame::CreatePush(*currentMailbox, std::move(payloadBuffer));
    } else if (*currentAction == ActionType::POLL) {
        result = Frame::CreatePoll(*currentMailbox);
    }

    headerBuffer.clear();
    payloadBuffer.clear();
    currentAction = std::nullopt;
    currentMailbox = std::nullopt;
    expectedPayloadLength = 0;
    currentState = ParserState::READING_HEADER;

    return result;
}

auto FrameParser::HasError() const noexcept -> bool
{
    return currentState == ParserState::ERROR_MALFORMED;
}

} // namespace bc::protocol
