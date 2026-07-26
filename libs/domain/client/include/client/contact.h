#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_H_

#include <optional>
#include <queue>
#include <string>

#include <client/client_types.h>
#include <core/secure_buffer.h>
#include <crypto/ephemeral_key.h>
#include <protocol/mailbox_id.h>
#include <protocol/protocol_types.h>

namespace bc::domain::client {

enum class PfsState : std::uint8_t {
    IDLE = 0x00,
    ROTATION_REQUESTED = 0x01,
    ROTATION_RESPONDING = 0x02
};

struct Contact
{
    std::string alias = "contact";
    PublicKeyType publicKey{};
    std::optional<std::string> note = std::nullopt;
    bc::protocol::MailboxID rxMailboxId;
    bc::protocol::MailboxID txMailboxId;
    bc::core::SecureBuffer rxKey;
    bc::core::SecureBuffer txKey;

    std::uint32_t messageCounter{0};
    PfsState pfsState{PfsState::IDLE};

    std::optional<bc::crypto::EphemeralKey> pendingEphemeralKey{std::nullopt};
    std::queue<bc::protocol::Payload> pendingMessages;
};

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_H_
