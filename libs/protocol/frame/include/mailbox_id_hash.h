#ifndef BC_LIBS_PROTOCOL_FRAME_INCLUDE_MAILBOXIDHASH_H_
#define BC_LIBS_PROTOCOL_FRAME_INCLUDE_MAILBOXIDHASH_H_

#include "mailbox_id.h"

#include <functional>

namespace std {

template <> struct hash<bc::protocol::frame::MailboxID>
{
    [[nodiscard]] auto
    operator()(const bc::protocol::frame::MailboxID& mailboxId) const noexcept -> std::size_t
    {
        constexpr size_t hashingConstant = 0x9e3779b9;
        std::size_t seed = mailboxId.size();
        for (const auto& byte : mailboxId) {
            // NOLINTBEGIN(readability-magic-numbers)
            // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
            seed ^= byte + hashingConstant + (seed << 6) + (seed >> 2);
            // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
            // NOLINTEND(readability-magic-numbers)
        }
        return seed;
    }
};

} // namespace std

#endif // BC_LIBS_PROTOCOL_FRAME_INCLUDE_MAILBOXIDHASH_H_
