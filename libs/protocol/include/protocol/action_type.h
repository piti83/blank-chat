#ifndef BC_LIBS_PROTOCOL_INCLUDE_ACTIONTYPE_H_
#define BC_LIBS_PROTOCOL_INCLUDE_ACTIONTYPE_H_

#include <cstdint>

namespace bc::protocol {

constexpr std::uint8_t actionTypeSize = 1;
enum class ActionType : std::uint8_t { PUSH = 0x01, POLL = 0x02, ACK = 0x03 };

} // namespace bc::protocol

#endif // BC_LIBS_PROTOCOL_INCLUDE_ACTIONTYPE_H_
