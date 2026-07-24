#ifndef BC_LIBS_CRYPTO_INCLUDE_BIP39_H_
#define BC_LIBS_CRYPTO_INCLUDE_BIP39_H_

#include <optional>
#include <string_view>

#include <core/secure_string.h>
#include <crypto/identity_key.h>

namespace bc::crypto::bip39 {

[[nodiscard]] auto Encode(const PublicKeyType& pubKey) -> core::SecureString;

[[nodiscard]] auto Decode(std::string_view mnemonic) -> std::optional<PublicKeyType>;

} // namespace bc::crypto::bip39

#endif
