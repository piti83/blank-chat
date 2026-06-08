#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "protocol/mailbox_id.h"

namespace bc::domain::client {

constexpr std::size_t publicKeySize = 32;

using PublicKeyType = std::array<std::uint8_t, publicKeySize>;

struct Contact
{
    std::string alias;
    PublicKeyType publicKey;
    std::optional<std::string> note;
    bc::protocol::MailboxID rxMailboxId;
    bc::protocol::MailboxID txMailboxId;
};

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_H_
