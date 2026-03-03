#include "protocol/mailbox_id.h"

#include "utils/uuid_constants.h"
#include "utils/uuid_factory.h"

#include <format>

namespace bc::protocol {

auto MailboxId::Create() -> MailboxId
{
    return MailboxId(bc::utils::UuidFactory::GenerateUuidV4());
}

auto MailboxId::GetRaw() const -> const std::array<uint8_t, bc::utils::kUuidSize>&
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

} // namespace bc::protocol
