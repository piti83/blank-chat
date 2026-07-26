#include "crypto/ephemeral_key.h"

#include <sodium.h>

#include <core/logger.h>

namespace bc::crypto {

auto EphemeralKey::Generate() noexcept -> std::optional<EphemeralKey>
{
    PublicKeyType pk{};
    bc::core::SecureBuffer sk(crypto_kx_SECRETKEYBYTES);

    if (crypto_kx_keypair(pk.data(), sk.AsMutableSpan().data()) != 0) {
        BC_ERROR("Failed to generate ephemeral X25519 keypair");
        return std::nullopt;
    }

    return EphemeralKey{pk, std::move(sk)};
}

EphemeralKey::EphemeralKey(PublicKeyType pk, bc::core::SecureBuffer sk)
    : publicKey(pk), secretKey(std::move(sk))
{
}

auto EphemeralKey::GetPublicKey() const noexcept -> const PublicKeyType&
{
    return publicKey;
}

auto EphemeralKey::GetSecretKeySpan() const noexcept -> std::span<const std::uint8_t>
{
    return secretKey.AsSpan();
}

} // namespace bc::crypto
