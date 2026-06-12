#ifndef BC_LIBS_CORE_INCLUDE_STRING_HASH_H_
#define BC_LIBS_CORE_INCLUDE_STRING_HASH_H_

#include <functional>
#include <string_view>

namespace bc::core {

struct StringHash
{
    using is_transparent = void;

    [[nodiscard]] auto operator()(std::string_view txt) const noexcept -> std::size_t
    {
        return std::hash<std::string_view>{}(txt);
    }
};

} // namespace bc::core

#endif // BC_LIBS_CORE_INCLUDE_STRING_HASH_H_
