#include "core/secure_string.h"

#include <cstdlib>
#include <utility>

#include <sodium.h>

#include <core/logger.h>

namespace bc::core {

SecureString::SecureString(std::size_t capacity)
    : maxCapacity(capacity), strData(static_cast<char*>(sodium_malloc(maxCapacity)))
{
    if (strData == nullptr) {
        BC_CRITICAL("sodium_malloc failed to allocate secure memory!");
        std::abort();
    }

    // NOLINTNEXTLINE
    strData[0] = '\0';
}

SecureString::~SecureString() noexcept
{
    if (strData != nullptr) {
        sodium_free(strData);
        strData = nullptr;
    }
}

SecureString::SecureString(SecureString&& other) noexcept
{
    maxCapacity = std::exchange(other.maxCapacity, 0);
    strSize = std::exchange(other.strSize, 0);
    strData = std::exchange(other.strData, nullptr);
}

auto SecureString::operator=(SecureString&& other) noexcept -> SecureString&
{
    if (this != &other) {
        if (strData != nullptr) {
            sodium_free(strData);
        }
        maxCapacity = std::exchange(other.maxCapacity, 0);
        strSize = std::exchange(other.strSize, 0);
        strData = std::exchange(other.strData, nullptr);
    }
    return *this;
}

auto SecureString::PushBack(char character) -> void
{
    if (strSize < maxCapacity - 1) {
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        strData[strSize++] = character;
        strData[strSize] = '\0';
        // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    } else {
        BC_WARN("SecureString capacity exceeded. Character discarded.");
    }
}

auto SecureString::StringView() const noexcept -> std::string_view
{
    return strData != nullptr ? std::string_view(strData, strSize) : std::string_view();
}

} // namespace bc::core
