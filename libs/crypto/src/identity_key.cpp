#include "crypto/identity_key.h"

#include <sodium.h>

namespace bc::crypto {

IdentityKey::IdentityKey(PublicKeyType pk, bc::core::SecureBuffer sk)
    : publicKey(pk), secretKey(std::move(sk))
{
}

auto IdentityKey::Generate() -> IdentityKey
{
    PublicKeyType pk{};
    bc::core::SecureBuffer sk(crypto_sign_SECRETKEYBYTES);

    crypto_sign_keypair(pk.data(), sk.AsMutableSpan().data());

    return {pk, std::move(sk)};
}

auto IdentityKey::GetPublicKey() const noexcept -> const PublicKeyType&
{
    return publicKey;
}

auto IdentityKey::GetSecretKeySpan() const noexcept -> std::span<const std::uint8_t>
{
    return secretKey.AsSpan();
}

auto IdentityKey::GetCurve25519PublicKey() const noexcept -> std::optional<PublicKeyType>
{
    PublicKeyType x25519Pk{};

    if (crypto_sign_ed25519_pk_to_curve25519(x25519Pk.data(), publicKey.data()) != 0) {
        return std::nullopt;
    }

    return x25519Pk;
}

auto IdentityKey::GetCurve25519SecretKey() const noexcept -> std::optional<bc::core::SecureBuffer>
{
    bc::core::SecureBuffer x25519Sk(crypto_scalarmult_SCALARBYTES);

    if (crypto_sign_ed25519_sk_to_curve25519(x25519Sk.AsMutableSpan().data(),
                                             secretKey.AsSpan().data()) != 0) {
        return std::nullopt;
    }

    return x25519Sk;
}

} // namespace bc::crypto
