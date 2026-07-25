#ifndef BC_LIBS_CRYPTO_INCLUDE_IDENTITY_KEY_H_
#define BC_LIBS_CRYPTO_INCLUDE_IDENTITY_KEY_H_

#include <cstdint>
#include <optional>
#include <span>

#include <core/secure_buffer.h>
#include <crypto/crypto_types.h>

namespace bc::crypto {

class IdentityKey
{
public:
    static auto Generate() -> IdentityKey;

    [[nodiscard]] static auto Restore(PublicKeyType pk, bc::core::SecureBuffer sk)
        -> std::optional<IdentityKey>;

    IdentityKey(const IdentityKey&) = delete;
    auto operator=(const IdentityKey&) = delete;

    IdentityKey(IdentityKey&&) noexcept = default;
    auto operator=(IdentityKey&&) noexcept -> IdentityKey& = default;

    ~IdentityKey() = default;

    [[nodiscard]] auto GetPublicKey() const noexcept -> const PublicKeyType&;
    [[nodiscard]] auto GetSecretKeySpan() const noexcept -> std::span<const std::uint8_t>;

    [[nodiscard]] auto GetCurve25519PublicKey() const noexcept -> std::optional<PublicKeyType>;
    [[nodiscard]] auto GetCurve25519SecretKey() const noexcept
        -> std::optional<bc::core::SecureBuffer>;

private:
    IdentityKey(PublicKeyType pk, bc::core::SecureBuffer sk);
    PublicKeyType publicKey;
    bc::core::SecureBuffer secretKey;
};

} // namespace bc::crypto

#endif // BC_LIBS_CRYPTO_INCLUDE_IDENTITY_KEY_H_
