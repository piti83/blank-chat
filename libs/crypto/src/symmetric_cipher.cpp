#include "crypto/symmetric_cipher.h"

#include <iterator>

#include <sodium.h>

#include <core/logger.h>

namespace bc::crypto {

auto SymmetricCipher::EncryptWithPadding(std::span<const std::uint8_t> key,
                                         std::span<const std::uint8_t> plaintext)
    -> std::optional<std::vector<std::uint8_t>>
{
    if (key.size() != keySize) {
        BC_ERROR("Invalid key size provided for encryption.");
        return std::nullopt;
    }

    std::size_t minPaddedSize = plaintext.size() + 1;
    std::size_t totalFrameSize = totalFixedOverhead + minPaddedSize;

    std::size_t cellsNeeded = (totalFrameSize + torCellPayloadSize - 1) / torCellPayloadSize;
    std::size_t targetFrameSize = cellsNeeded * torCellPayloadSize;
    std::size_t paddedPlaintextSize = targetFrameSize - totalFixedOverhead;

    std::vector<std::uint8_t> paddedPlaintext;
    paddedPlaintext.reserve(paddedPlaintextSize);
    paddedPlaintext.insert(paddedPlaintext.end(), plaintext.begin(), plaintext.end());
    paddedPlaintext.push_back(paddingMarker);

    while (paddedPlaintext.size() < paddedPlaintextSize) {
        paddedPlaintext.push_back(0x00);
    }

    std::vector<std::uint8_t> outBuffer(nonceSize + paddedPlaintextSize + macSize);

    randombytes_buf(outBuffer.data(), nonceSize);

    unsigned long long ciphertextLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            std::span{outBuffer}.subspan(nonceSize).data(), &ciphertextLen, paddedPlaintext.data(),
            paddedPlaintext.size(), nullptr, 0, nullptr, outBuffer.data(), key.data()) != 0) {
        BC_ERROR("XChaCha20-Poly1305 encryption failed.");
        sodium_memzero(paddedPlaintext.data(), paddedPlaintext.size());
        return std::nullopt;
    }

    sodium_memzero(paddedPlaintext.data(), paddedPlaintext.size());
    return outBuffer;
}

auto SymmetricCipher::DecryptAndUnpad(std::span<const std::uint8_t> key,
                                      std::span<const std::uint8_t> ciphertext)
    -> std::optional<std::vector<std::uint8_t>>
{
    if (key.size() != keySize || ciphertext.size() < nonceSize + macSize) {
        BC_ERROR("Invalid key or ciphertext size.");
        return std::nullopt;
    }

    std::vector<std::uint8_t> decrypted(ciphertext.size() - nonceSize - macSize);
    unsigned long long decryptedLen = 0;

    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            decrypted.data(), &decryptedLen, nullptr, ciphertext.subspan(nonceSize).data(),
            ciphertext.size() - nonceSize, nullptr, 0, ciphertext.data(), key.data()) != 0) {
        return std::nullopt;
    }

    decrypted.resize(decryptedLen);

    auto it = decrypted.rbegin();
    while (it != decrypted.rend() && *it == 0x00) {
        ++it;
    }

    if (it == decrypted.rend() || *it != paddingMarker) {
        BC_WARN("Invalid padding structure detected during decryption.");
        sodium_memzero(decrypted.data(), decrypted.size());
        return std::nullopt;
    }

    std::size_t validDataSize = std::distance(it, decrypted.rend()) - 1;
    decrypted.resize(validDataSize);

    return decrypted;
}

} // namespace bc::crypto
