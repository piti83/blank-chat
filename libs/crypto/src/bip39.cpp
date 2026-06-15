#include "crypto/bip39.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>

#include <sodium.h>

#include <core/secure_string.h>
#include <crypto/bip39_dictionary.h>
#include <crypto/identity_key.h>

#include <sodium/crypto_hash_sha256.h>

namespace bc::crypto::bip39 {

constexpr std::size_t bipBitsAmount = 264;
constexpr std::size_t wordsInMnemonic = 24;

auto Encode(const PublicKeyType& pubKey) -> core::SecureString
{
    core::SecureString result;

    std::array<std::uint8_t, crypto_hash_sha256_BYTES> hashOut{};
    crypto_hash_sha256(hashOut.data(), pubKey.data(), pubKey.size());

    std::array<uint8_t, publicKeySize + 1> buffer{};
    std::memcpy(buffer.data(), pubKey.data(), publicKeySize);

    buffer.at(publicKeySize) = hashOut.at(0);

    std::size_t wordIndex{};

    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
    for (std::size_t i{}; i < bipBitsAmount; ++i) {
        std::size_t byteIndex = i / 8;
        std::size_t bitOffset = 7 - (i % 8);
        bool bit = static_cast<bool>((buffer.at(byteIndex) >> bitOffset) & 1);
        wordIndex = (wordIndex << 1) | static_cast<std::size_t>(bit);
        if ((i + 1) % 11 == 0) {
            if (i > 10)
                result.PushBack('-');

            for (char c : wordlist.at(wordIndex)) {
                result.PushBack(c);
            }
            wordIndex = 0;
        }
    }
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

    return result;
}

auto Decode(std::string_view mnemonic) -> std::optional<PublicKeyType>
{
    std::array<std::string_view, wordsInMnemonic> words;
    std::size_t start = 0;
    std::size_t end = mnemonic.find('-');
    std::size_t ind = 0;

    while (end != std::string_view::npos) {
        if (ind >= wordsInMnemonic - 1) {
            return std::nullopt;
        }
        words.at(ind++) = mnemonic.substr(start, end - start);
        start = end + 1;
        end = mnemonic.find('-', start);
    }

    if (ind != wordsInMnemonic - 1) {
        return std::nullopt;
    }
    words.at(ind) = mnemonic.substr(start);

    std::array<std::uint16_t, wordsInMnemonic> indices{};
    for (std::size_t i = 0; i < wordsInMnemonic; ++i) {
        const auto* it = std::ranges::lower_bound(wordlist.begin(), wordlist.end(), words.at(i));
        if (it == wordlist.end() || *it != words.at(i)) {
            return std::nullopt;
        }
        indices.at(i) = static_cast<std::uint16_t>(std::distance(wordlist.begin(), it));
    }

    std::array<std::uint8_t, publicKeySize + 1> entropyWithChecksum{};

    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
    for (std::size_t i = 0; i < bipBitsAmount; ++i) {
        std::size_t wordIndex = i / 11;
        std::size_t bitInWord = 10 - (i % 11);
        bool bit = static_cast<bool>((indices.at(wordIndex) >> bitInWord) & 1);

        std::size_t byteIndex = i / 8;
        std::size_t bitInByte = 7 - (i % 8);

        entropyWithChecksum.at(byteIndex) |= (static_cast<std::uint8_t>(bit) << bitInByte);
    }
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

    std::array<std::uint8_t, crypto_hash_sha256_BYTES> hashOut{};
    crypto_hash_sha256(hashOut.data(), entropyWithChecksum.data(), publicKeySize);

    if (hashOut.at(0) != entropyWithChecksum.at(publicKeySize)) {
        sodium_memzero(entropyWithChecksum.data(), entropyWithChecksum.size());
        sodium_memzero(indices.data(), indices.size() * sizeof(std::uint16_t));
        return std::nullopt;
    }

    PublicKeyType pubKey{};
    std::copy_n(entropyWithChecksum.begin(), publicKeySize, pubKey.begin());

    sodium_memzero(entropyWithChecksum.data(), entropyWithChecksum.size());
    sodium_memzero(indices.data(), indices.size() * sizeof(std::uint16_t));

    return pubKey;
}

} // namespace bc::crypto::bip39
