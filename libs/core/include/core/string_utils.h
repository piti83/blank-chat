#ifndef BC_LIBS_CORE_INCLUDE_STRING_UTILS_H_
#define BC_LIBS_CORE_INCLUDE_STRING_UTILS_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bc::core {

[[nodiscard]] auto DecodeHexToArray(std::string_view hexStr, std::span<std::uint8_t> outArray)
    -> bool;

[[nodiscard]] auto EncodeHex(std::span<const std::uint8_t> data) -> std::string;

[[nodiscard]] auto EscapeJsonString(std::string_view input) -> std::string;

[[nodiscard]] auto HashPayload(const std::vector<std::uint8_t>& payload) -> std::string;

} // namespace bc::core

#endif // BC_LIBS_CORE_INCLUDE_STRING_UTILS_H_
