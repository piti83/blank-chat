#ifndef BC_LIBS_CRYPTO_INCLUDE_EPHEMERAL_KEY_H_
#define BC_LIBS_CRYPTO_INCLUDE_EPHEMERAL_KEY_H_

#include <cstdint>
#include <optional>
#include <span>

#include <core/secure_buffer.h>
#include <crypto/crypto_types.h>

namespace bc::crypto {

class EphemeralKey
{
public:
    [[nodiscard]] static auto Generate() noexcept -> std::optional<EphemeralKey>;

    EphemeralKey(const EphemeralKey&) = delete;
    auto operator=(const EphemeralKey&) -> EphemeralKey& = delete;
    EphemeralKey(EphemeralKey&&) noexcept = default;
    auto operator=(EphemeralKey&&) noexcept -> EphemeralKey& = default;
    ~EphemeralKey() = default;

    [[nodiscard]] auto GetPublicKey() const noexcept -> const PublicKeyType&;
    [[nodiscard]] auto GetSecretKeySpan() const noexcept -> std::span<const std::uint8_t>;

private:
    EphemeralKey(PublicKeyType pk, bc::core::SecureBuffer sk);

    PublicKeyType publicKey;
    bc::core::SecureBuffer secretKey;
};

} // namespace bc::crypto

#endif // BC_LIBS_CRYPTO_INCLUDE_EPHEMERAL_KEY_H_
