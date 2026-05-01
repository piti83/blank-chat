#ifndef BC_LIBS_PROTOCOL_FRAME_INCLUDE_ACTIONTYPE_H_
#define BC_LIBS_PROTOCOL_FRAME_INCLUDE_ACTIONTYPE_H_

#include <cstdint>

namespace bc::protocol::frame {

constexpr std::uint8_t actionTypeSize = 1;
enum class ActionType : std::uint8_t { PUSH = 0x01, POLL = 0x02 };

} // namespace bc::protocol::frame

#endif // BC_LIBS_PROTOCOL_FRAME_INCLUDE_ACTIONTYPE_H_
