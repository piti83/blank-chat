#ifndef BC_LIBS_CRYPTO_INCLUDE_MAILBOX_DERIVATION_H_
#define BC_LIBS_CRYPTO_INCLUDE_MAILBOX_DERIVATION_H_

#include <crypto/identity_key.h>

namespace bc::crypto {

constexpr std::size_t expectedMailboxSize = 16;
constexpr std::size_t symmetricKeySize = 32;
constexpr std::size_t extendedHashSize = expectedMailboxSize + symmetricKeySize;

using MailboxIdBuffer = std::array<std::uint8_t, expectedMailboxSize>;

struct DerivedMailboxes
{
    MailboxIdBuffer txId;
    bc::core::SecureBuffer txKey;
    MailboxIdBuffer rxId;
    bc::core::SecureBuffer rxKey;
};

[[nodiscard]] auto DerivePairwiseMailboxes(const IdentityKey& ourIdentity,
                                           const PublicKeyType& theirEd25519PublicKey) noexcept
    -> std::optional<DerivedMailboxes>;

} // namespace bc::crypto

#endif // BC_LIBS_CRYPTO_INCLUDE_MAILBOX_DERIVATION_H_
