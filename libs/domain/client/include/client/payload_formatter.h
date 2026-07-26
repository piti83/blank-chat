#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_PAYLOAD_FORMATTER_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_PAYLOAD_FORMATTER_H_

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include <crypto/crypto_types.h>
#include <protocol/protocol_types.h>

#include "client/client_types.h"

namespace bc::domain::client {

struct PfsRotateData
{
    bc::crypto::PublicKeyType ephemeralPublicKey;
    std::array<std::uint8_t, cryptoSignBytes> signature;
};

class PayloadFormatter
{
public:
    PayloadFormatter() = delete;

    [[nodiscard]] static auto BuildTextMessage(std::span<const std::uint8_t> textData)
        -> bc::protocol::Payload;

    [[nodiscard]] static auto
    BuildPfsRotateRequest(const bc::crypto::PublicKeyType& ephemeralKey,
                          std::span<const std::uint8_t, cryptoSignBytes> signature)
        -> bc::protocol::Payload;

    [[nodiscard]] static auto
    BuildPfsRotateAck(const bc::crypto::PublicKeyType& ephemeralKey,
                      std::span<const std::uint8_t, cryptoSignBytes> signature)
        -> bc::protocol::Payload;

    [[nodiscard]] static auto ExtractOpcode(std::span<const std::uint8_t> payload) noexcept
        -> std::optional<PayloadOpcode>;

    [[nodiscard]] static auto ParseTextMessage(std::span<const std::uint8_t> payload) noexcept
        -> std::optional<std::span<const std::uint8_t>>;

    [[nodiscard]] static auto ParsePfsRotateRequest(std::span<const std::uint8_t> payload) noexcept
        -> std::optional<PfsRotateData>;

    [[nodiscard]] static auto ParsePfsRotateAck(std::span<const std::uint8_t> payload) noexcept
        -> std::optional<PfsRotateData>;
};

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_PAYLOAD_FORMATTER_H_
