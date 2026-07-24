#include "core/string_utils.h"

#include <sodium.h>

namespace {

// NOLINTBEGIN

constexpr auto HexCharToInt(char c) -> int
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

constexpr char HexIntToChar(int v)
{
    return "0123456789abcdef"[v & 0x0F];
}

} // namespace

namespace bc::core {

[[nodiscard]] auto DecodeHexToArray(std::string_view hexStr, std::span<uint8_t> outArray) -> bool
{
    if (hexStr.length() != outArray.size() * 2) {
        return false;
    }

    for (size_t i = 0; i < outArray.size(); ++i) {
        int high = HexCharToInt(hexStr[i * 2]);
        int low = HexCharToInt(hexStr[(i * 2) + 1]);

        if (high == -1 || low == -1) {
            return false;
        }

        outArray[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

[[nodiscard]] auto EncodeHex(std::span<const uint8_t> data) -> std::string
{
    std::string hex;
    hex.reserve(data.size() * 2);
    for (uint8_t b : data) {
        hex.push_back(HexIntToChar(b >> 4));
        hex.push_back(HexIntToChar(b & 0x0F));
    }
    return hex;
}
// NOLINTEND

[[nodiscard]] auto EscapeJsonString(std::string_view input) -> std::string
{
    std::string output;
    output.reserve(input.length() + 4);
    for (char c : input) {
        if (c == '"')
            output += "\\\"";
        else if (c == '\\')
            output += "\\\\";
        else if (c == '\b')
            output += "\\b";
        else if (c == '\f')
            output += "\\f";
        else if (c == '\n')
            output += "\\n";
        else if (c == '\r')
            output += "\\r";
        else if (c == '\t')
            output += "\\t";
        else
            output += c;
    }
    return output;
}

auto HashPayload(const std::vector<std::uint8_t>& payload) -> std::string
{
    // NOLINTBEGIN
    std::array<std::uint8_t, 16> hashOut{};
    // NOLINTEND

    crypto_generichash(hashOut.data(), hashOut.size(), payload.data(), payload.size(), nullptr, 0);

    return EncodeHex(hashOut);
}

} // namespace bc::core
