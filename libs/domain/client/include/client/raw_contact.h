#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_RAW_CONTACT_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_RAW_CONTACT_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace bc::domain::client {

constexpr std::size_t publicKeySize = 32;

using PublicKeyType = std::array<std::uint8_t, publicKeySize>;

struct RawContact
{
    std::string alias;
    PublicKeyType publicKey;
    std::optional<std::string> note;
};

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_RAW_CONTACT_H_
