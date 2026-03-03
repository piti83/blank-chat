#pragma once
#include "utils/uuid_constants.h"

#include <array>
#include <cstdint>
#include <string>

namespace bc::protocol {

class MailboxId
{
public:
    MailboxId() = delete;
    [[nodiscard]] static auto Create() -> MailboxId;

    MailboxId(const MailboxId&) = default;
    MailboxId(MailboxId&&) = default;
    auto operator=(const MailboxId&) -> MailboxId& = default;
    auto operator=(MailboxId&&) -> MailboxId& = default;

    auto operator==(const MailboxId&) const -> bool = default;
    auto operator<=>(const MailboxId&) const = default;

    ~MailboxId() = default;

    [[nodiscard]] auto GetRaw() const -> const std::array<uint8_t, bc::utils::kUuidSize>&;
    [[nodiscard]] auto GetAsString() const -> std::string;

private:
    explicit MailboxId(const std::array<uint8_t, bc::utils::kUuidSize>& raw_data) : data(raw_data)
    {
    }

    std::array<uint8_t, bc::utils::kUuidSize> data;
};

static_assert(sizeof(MailboxId) == bc::utils::kUuidSize);
static_assert(std::is_trivially_copyable_v<MailboxId>);
static_assert(std::is_standard_layout_v<MailboxId>);
static_assert(std::is_move_constructible_v<MailboxId>);
static_assert(std::is_move_assignable_v<MailboxId>);

} // namespace bc::protocol
