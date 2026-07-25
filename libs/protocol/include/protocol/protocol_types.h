#ifndef BC_LIBS_PROTOCOL_INCLUDE_PROTOCOLTYPES_H_
#define BC_LIBS_PROTOCOL_INCLUDE_PROTOCOLTYPES_H_

#include <cstdint>
#include <vector>

namespace bc::protocol {

using Payload = std::vector<std::uint8_t>;
using PayloadLength = std::uint32_t;
using RawFrame = std::vector<std::uint8_t>;

constexpr std::uint8_t actionTypeSize = 1;
constexpr std::uint8_t mailboxIdSize = 16;
static constexpr std::size_t headerSize = actionTypeSize + mailboxIdSize + sizeof(PayloadLength);
static constexpr PayloadLength maxPayloadSize = 1024 * 1024;

enum class ActionType : std::uint8_t { PUSH = 0x01, POLL = 0x02, ACK = 0x03 };

enum class ParserState : std::uint8_t {
    READING_HEADER,
    READING_PAYLOAD,
    FRAME_READY,
    ERROR_MALFORMED
};

} // namespace bc::protocol

#endif // BC_LIBS_PROTOCOL_INCLUDE_PROTOCOLTYPES_H_
