#include "protocol/mailbox_id.h"
#include "utils/uuid_constants.h"
#include "utils/uuid_factory.h"

#include <format>

namespace blank_chat::protocol {

auto MailboxId::Create() -> MailboxId
{
    return MailboxId(blank_chat::utils::UuidFactory::GenerateUuidV4());
}

auto MailboxId::GetRaw() const -> const std::array<uint8_t, blank_chat::utils::kUuidSize>&
{
    return data;
}

auto MailboxId::GetAsString() const -> std::string
{
    return std::apply(
        [](auto... bytes) -> std::string {
            return std::format("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-"
                               "{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
                               bytes...);
        },
        data);
}

} // namespace blank_chat::protocol
