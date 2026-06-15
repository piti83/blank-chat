#ifndef BC_LIBS_CORE_INCLUDE_SECURE_STRING_H_
#define BC_LIBS_CORE_INCLUDE_SECURE_STRING_H_

#include <cstddef>
#include <string_view>

namespace bc::core {

constexpr std::size_t defaultSecureStringCapacity = 256;

class SecureString
{
public:
    explicit SecureString(std::size_t capacity = defaultSecureStringCapacity);
    ~SecureString() noexcept;

    SecureString(const SecureString&) = delete;
    auto operator=(const SecureString&) -> SecureString& = delete;

    SecureString(SecureString&& other) noexcept;
    auto operator=(SecureString&& other) noexcept -> SecureString&;

    auto PushBack(char character) -> void;
    [[nodiscard]] auto StringView() const noexcept -> std::string_view;

private:
    std::size_t maxCapacity;
    char* strData{nullptr};
    std::size_t strSize{0};
};

} // namespace bc::core

#endif // BC_LIBS_CORE_INCLUDE_SECURE_STRING_H_
