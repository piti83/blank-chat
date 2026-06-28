#ifndef BC_LIBS_CORE_INCLUDE_TERMINAL_H_
#define BC_LIBS_CORE_INCLUDE_TERMINAL_H_

#include <string_view>

#include <core/secure_string.h>

namespace bc::core {

class Terminal
{
public:
    [[nodiscard]] static auto ReadPasswordSecurely(std::string_view prompt) -> SecureString;
};

} // namespace bc::core
#endif
