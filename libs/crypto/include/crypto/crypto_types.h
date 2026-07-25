#ifndef BC_LIBS_CRYPTO_INCLUDE_CRYPTOTYPES_H_
#define BC_LIBS_CRYPTO_INCLUDE_CRYPTOTYPES_H_

#include <array>
#include <cstddef>
#include <cstdint>

#include <protocol/protocol_types.h>

namespace bc::crypto {

static constexpr std::size_t publicKeySize = 32;
static constexpr std::size_t symmetricKeySize = 32;
static constexpr std::size_t extendedHashSize = bc::protocol::mailboxIdSize + symmetricKeySize;

static constexpr std::size_t keySize = 32;
static constexpr std::size_t nonceSize = 24;
static constexpr std::size_t macSize = 16;

static constexpr std::size_t torCellPayloadSize = 498;
static constexpr std::size_t cryptoOverhead = nonceSize + macSize;
static constexpr std::size_t totalFixedOverhead = bc::protocol::headerSize + cryptoOverhead;
static constexpr std::uint8_t paddingMarker = 0x80;

static constexpr std::size_t bipBitsAmount = 264;
static constexpr std::size_t wordsInMnemonic = 24;

using PublicKeyType = std::array<std::uint8_t, publicKeySize>;
using MailboxIdBuffer = std::array<std::uint8_t, bc::protocol::mailboxIdSize>;

} // namespace bc::crypto

#endif
