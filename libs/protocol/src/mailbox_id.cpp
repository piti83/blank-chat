#include "protocol/mailbox_id.h"

#include <algorithm>
#include <span>

namespace bc::protocol {

MailboxID::MailboxID(const std::array<uint8_t, mailboxIdSize>& bytes) : bytes(bytes)
{
}

MailboxID::MailboxID(std::span<const std::uint8_t, mailboxIdSize> buffer)
{
    std::ranges::copy(buffer.begin(), buffer.end(), bytes.begin());
}

auto MailboxID::data() const noexcept -> const std::uint8_t*
{
    return bytes.data();
}

auto MailboxID::size() const noexcept -> std::size_t
{
    return bytes.size();
}

auto MailboxID::begin() const noexcept -> std::array<uint8_t, mailboxIdSize>::const_iterator
{
    return bytes.begin();
}

auto MailboxID::end() const noexcept -> std::array<uint8_t, mailboxIdSize>::const_iterator
{
    return bytes.end();
}

auto MailboxID::Fill(std::uint8_t value) -> void
{
    bytes.fill(value);
}

auto MailboxID::AsSpan() const noexcept -> std::span<const std::uint8_t, mailboxIdSize>
{
    return bytes;
}

} // namespace bc::protocol
