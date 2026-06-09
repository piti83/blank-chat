#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_PUBLIC_KEY_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_PUBLIC_KEY_H_

#include <array>
#include <cstdint>

namespace bc::domain::client {

constexpr std::size_t publicKeySize = 32;
using PublicKeyType = std::array<std::uint8_t, publicKeySize>;

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_PUBLIC_KEY_H_
