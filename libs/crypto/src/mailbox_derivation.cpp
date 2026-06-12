#include "crypto/mailbox_derivation.h"

#include <sodium.h>

#include <core/secure_buffer.h>

namespace bc::crypto {

auto DerivePairwiseMailboxes(const IdentityKey& ourIdentity,
                             const PublicKeyType& theirEd25519PublicKey) noexcept
    -> std::optional<DerivedMailboxes>
{
    auto ourX25519PkOpt = ourIdentity.GetCurve25519PublicKey();
    auto ourX25519SkOpt = ourIdentity.GetCurve25519SecretKey();

    if (!ourX25519PkOpt || !ourX25519SkOpt) {
        return std::nullopt;
    }

    PublicKeyType theirX25519Pk{};
    if (crypto_sign_ed25519_pk_to_curve25519(theirX25519Pk.data(), theirEd25519PublicKey.data()) !=
        0) {
        return std::nullopt;
    }

    bc::core::SecureBuffer sharedSecret(crypto_scalarmult_BYTES);
    if (crypto_scalarmult(sharedSecret.AsMutableSpan().data(), ourX25519SkOpt->AsSpan().data(),
                          theirX25519Pk.data()) != 0) {
        return std::nullopt;
    }

    DerivedMailboxes result{};

    crypto_generichash_state stateTx;
    crypto_generichash_init(&stateTx, nullptr, 0, expectedMailboxSize);
    crypto_generichash_update(&stateTx, sharedSecret.AsSpan().data(), sharedSecret.Size());
    crypto_generichash_update(&stateTx, ourX25519PkOpt->data(), ourX25519PkOpt->size());
    crypto_generichash_update(&stateTx, theirX25519Pk.data(), theirX25519Pk.size());
    crypto_generichash_final(&stateTx, result.txId.data(), expectedMailboxSize);

    crypto_generichash_state stateRx;
    crypto_generichash_init(&stateRx, nullptr, 0, expectedMailboxSize);
    crypto_generichash_update(&stateRx, sharedSecret.AsSpan().data(), sharedSecret.Size());
    crypto_generichash_update(&stateRx, theirX25519Pk.data(), theirX25519Pk.size());
    crypto_generichash_update(&stateRx, ourX25519PkOpt->data(), ourX25519PkOpt->size());
    crypto_generichash_final(&stateRx, result.rxId.data(), expectedMailboxSize);

    return result;
}

} // namespace bc::crypto
