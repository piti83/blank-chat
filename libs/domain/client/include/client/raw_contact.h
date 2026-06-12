#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_RAW_CONTACT_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_RAW_CONTACT_H_

#include <optional>
#include <string>

#include <client/public_key.h>

namespace bc::domain::client {

struct RawContact
{
    std::string alias = "contact";
    PublicKeyType publicKey{};
    std::optional<std::string> note = std::nullopt;
};

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_RAW_CONTACT_H_
