#ifndef BC_LIBS_CRYPTO_INCLUDE_SYMMETRIC_CIPHER_H_
#define BC_LIBS_CRYPTO_INCLUDE_SYMMETRIC_CIPHER_H_

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <crypto/crypto_types.h>

namespace bc::crypto {

class SymmetricCipher
{
public:
    [[nodiscard]] static auto EncryptWithPadding(std::span<const std::uint8_t> key,
                                                 std::span<const std::uint8_t> plaintext)
        -> std::optional<std::vector<std::uint8_t>>;

    [[nodiscard]] static auto DecryptAndUnpad(std::span<const std::uint8_t> key,
                                              std::span<const std::uint8_t> ciphertext)
        -> std::optional<std::vector<std::uint8_t>>;
};

} // namespace bc::crypto

#endif // BC_LIBS_CRYPTO_INCLUDE_SYMMETRIC_CIPHER_H_
