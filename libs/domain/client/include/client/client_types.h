#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_CLIENTTYPES_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_CLIENTTYPES_H_

#include <cstdint>

#include <crypto/crypto_types.h>

namespace bc::domain::client {

static constexpr std::uint16_t defaultOnionPort = 80;

enum class MessageDirection : std::uint8_t { INBOUND, OUTBOUND };
enum class MessageStatus : std::uint8_t { PENDING_ACK, DELIVERED, FAILED };

using PublicKeyType = bc::crypto::PublicKeyType;
constexpr std::size_t publicKeySize = bc::crypto::publicKeySize;

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_CLIENTTYPES_H_
