#ifndef BC_LIBS_CRYPTO_INCLUDE_SYMMETRIC_CIPHER_H_
#define BC_LIBS_CRYPTO_INCLUDE_SYMMETRIC_CIPHER_H_

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace bc::crypto {

class SymmetricCipher
{
public:
    static constexpr std::size_t keySize = 32;
    static constexpr std::size_t nonceSize = 24;
    static constexpr std::size_t macSize = 16;

    static constexpr std::size_t torCellPayloadSize = 498;
    static constexpr std::size_t frameHeaderSize = 21;
    static constexpr std::size_t cryptoOverhead = nonceSize + macSize;
    static constexpr std::size_t totalFixedOverhead = frameHeaderSize + cryptoOverhead;
    static constexpr std::uint8_t paddingMarker = 0x80;

    [[nodiscard]] static auto EncryptWithPadding(std::span<const std::uint8_t> key,
                                                 std::span<const std::uint8_t> plaintext)
        -> std::optional<std::vector<std::uint8_t>>;

    [[nodiscard]] static auto DecryptAndUnpad(std::span<const std::uint8_t> key,
                                              std::span<const std::uint8_t> ciphertext)
        -> std::optional<std::vector<std::uint8_t>>;
};

} // namespace bc::crypto

#endif // BC_LIBS_CRYPTO_INCLUDE_SYMMETRIC_CIPHER_H_
