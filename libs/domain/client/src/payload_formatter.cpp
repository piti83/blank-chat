#include "client/payload_formatter.h"

#include <algorithm>

#include <core/logger.h>

#include "client/client_types.h"

namespace bc::domain::client {

auto PayloadFormatter::BuildTextMessage(std::span<const std::uint8_t> textData)
    -> bc::protocol::Payload
{
    bc::protocol::Payload buffer;
    buffer.reserve(1 + textData.size());

    buffer.push_back(static_cast<std::uint8_t>(PayloadOpcode::TEXT_MESSAGE));
    buffer.insert(buffer.end(), textData.begin(), textData.end());

    return buffer;
}

auto PayloadFormatter::BuildPfsRotateRequest(
    const bc::crypto::PublicKeyType& ephemeralKey,
    std::span<const std::uint8_t, cryptoSignBytes> signature) -> bc::protocol::Payload
{
    bc::protocol::Payload buffer;
    buffer.reserve(1 + ephemeralKey.size() + signature.size());

    buffer.push_back(static_cast<std::uint8_t>(PayloadOpcode::PFS_ROTATE_REQUEST));
    buffer.insert(buffer.end(), ephemeralKey.begin(), ephemeralKey.end());
    buffer.insert(buffer.end(), signature.begin(), signature.end());

    return buffer;
}

auto PayloadFormatter::BuildPfsRotateAck(const bc::crypto::PublicKeyType& ephemeralKey,
                                         std::span<const std::uint8_t, cryptoSignBytes> signature)
    -> bc::protocol::Payload
{
    bc::protocol::Payload buffer;
    buffer.reserve(1 + ephemeralKey.size() + signature.size());

    buffer.push_back(static_cast<std::uint8_t>(PayloadOpcode::PFS_ROTATE_ACK));
    buffer.insert(buffer.end(), ephemeralKey.begin(), ephemeralKey.end());
    buffer.insert(buffer.end(), signature.begin(), signature.end());

    return buffer;
}

auto PayloadFormatter::ExtractOpcode(std::span<const std::uint8_t> payload) noexcept
    -> std::optional<PayloadOpcode>
{
    if (payload.empty()) {
        return std::nullopt;
    }

    auto rawOpcode = payload.front();
    if (rawOpcode == static_cast<std::uint8_t>(PayloadOpcode::TEXT_MESSAGE) ||
        rawOpcode == static_cast<std::uint8_t>(PayloadOpcode::PFS_ROTATE_REQUEST) ||
        rawOpcode == static_cast<std::uint8_t>(PayloadOpcode::PFS_ROTATE_ACK)) {
        return static_cast<PayloadOpcode>(rawOpcode);
    }

    return std::nullopt;
}

auto PayloadFormatter::ParseTextMessage(std::span<const std::uint8_t> payload) noexcept
    -> std::optional<std::span<const std::uint8_t>>
{
    auto opcode = ExtractOpcode(payload);
    if (!opcode || *opcode != PayloadOpcode::TEXT_MESSAGE) {
        return std::nullopt;
    }

    return payload.subspan(1);
}

auto PayloadFormatter::ParsePfsRotateRequest(std::span<const std::uint8_t> payload) noexcept
    -> std::optional<PfsRotateData>
{
    auto opcode = ExtractOpcode(payload);
    if (!opcode || *opcode != PayloadOpcode::PFS_ROTATE_REQUEST) {
        return std::nullopt;
    }

    constexpr std::size_t expectedSize = 1 + bc::crypto::publicKeySize + 64;
    if (payload.size() != expectedSize) {
        BC_WARN("Invalid PFS_ROTATE_REQUEST payload size. Expected: {}, got: {}", expectedSize,
                payload.size());
        return std::nullopt;
    }

    PfsRotateData request{};
    auto keySpan = payload.subspan(1, bc::crypto::publicKeySize);
    auto sigSpan = payload.subspan(1 + bc::crypto::publicKeySize, cryptoSignBytes);

    std::ranges::copy(keySpan, request.ephemeralPublicKey.begin());
    std::ranges::copy(sigSpan, request.signature.begin());

    return request;
}

auto PayloadFormatter::ParsePfsRotateAck(std::span<const std::uint8_t> payload) noexcept
    -> std::optional<PfsRotateData>
{
    auto opcode = ExtractOpcode(payload);
    if (!opcode || *opcode != PayloadOpcode::PFS_ROTATE_ACK) {
        return std::nullopt;
    }

    constexpr std::size_t expectedSize = 1 + bc::crypto::publicKeySize + cryptoSignBytes;
    if (payload.size() != expectedSize) {
        return std::nullopt;
    }

    PfsRotateData response{};
    auto keySpan = payload.subspan(1, bc::crypto::publicKeySize);
    auto sigSpan = payload.subspan(1 + bc::crypto::publicKeySize, cryptoSignBytes);

    std::ranges::copy(keySpan, response.ephemeralPublicKey.begin());
    std::ranges::copy(sigSpan, response.signature.begin());

    return response;
}

} // namespace bc::domain::client
