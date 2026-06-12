#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_H_

#include <optional>
#include <string>

#include <client/public_key.h>
#include <protocol/mailbox_id.h>

namespace bc::domain::client {

struct Contact
{
    std::string alias = "contact";
    PublicKeyType publicKey{};
    std::optional<std::string> note = std::nullopt;
    bc::protocol::MailboxID rxMailboxId;
    bc::protocol::MailboxID txMailboxId;
};

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_H_
