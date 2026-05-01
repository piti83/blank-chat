#ifndef BC_LIBS_PROTOCOL_FRAME_INCLUDE_MAILBOXID_H_
#define BC_LIBS_PROTOCOL_FRAME_INCLUDE_MAILBOXID_H_

#include <array>
#include <cstdint>
#include <span>

namespace bc::protocol::frame {

constexpr std::uint8_t mailboxIdSize = 16;

class MailboxID
{
public:
    MailboxID() = default;
    explicit MailboxID(const std::array<std::uint8_t, mailboxIdSize>& bytes);
    explicit MailboxID(std::span<const std::uint8_t, mailboxIdSize> buffer);

    auto operator==(const MailboxID&) const -> bool = default;
    auto operator<=>(const MailboxID&) const = default;
    // NOLINTBEGIN(readability-identifier-naming)
    [[nodiscard]] auto data() const noexcept -> const std::uint8_t*;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto begin() const noexcept
        -> std::array<std::uint8_t, mailboxIdSize>::const_iterator;
    [[nodiscard]] auto end() const noexcept
        -> std::array<std::uint8_t, mailboxIdSize>::const_iterator;
    // NOLINTEND(readability-identifier-naming)

    auto Fill(std::uint8_t value) -> void;
    [[nodiscard]] auto AsSpan() const noexcept -> std::span<const std::uint8_t, mailboxIdSize>;

private:
    std::array<std::uint8_t, mailboxIdSize> bytes{};
};

} // namespace bc::protocol::frame

#endif // BC_LIBS_PROTOCOL_FRAME_INCLUDE_MAILBOXID_H_
