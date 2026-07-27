#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_CLIENTTYPES_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_CLIENTTYPES_H_

#include <cstdint>

#include <crypto/crypto_types.h>

namespace bc::domain::client {

static constexpr std::uint16_t defaultOnionPort = 80;
static constexpr std::size_t cryptoSignBytes = 64;

enum class MessageDirection : std::uint8_t { INBOUND = 0x01, OUTBOUND = 0x02 };
enum class MessageStatus : std::uint8_t { PENDING_ACK = 0x01, DELIVERED = 0x02, FAILED = 0x03 };
enum class PayloadOpcode : std::uint8_t {
    TEXT_MESSAGE = 0x01,
    PFS_ROTATE_REQUEST = 0x02,
    PFS_ROTATE_ACK = 0x03
};

using PublicKeyType = bc::crypto::PublicKeyType;
constexpr std::size_t publicKeySize = bc::crypto::publicKeySize;

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_CLIENTTYPES_H_
