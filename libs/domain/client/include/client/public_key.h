#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_PUBLIC_KEY_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_PUBLIC_KEY_H_

#include <crypto/identity_key.h>

namespace bc::domain::client {

using PublicKeyType = bc::crypto::PublicKeyType;
constexpr std::size_t publicKeySize = bc::crypto::publicKeySize;

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_PUBLIC_KEY_H_
