#ifndef BC_LIBS_CORE_INCLUDE_LOGGER_H_
#define BC_LIBS_CORE_INCLUDE_LOGGER_H_

#include <core/log_level.h>
#include <format>
#include <string_view>

namespace bc::core {

class Logger
{
public:
    static auto Init() -> void;

    static auto Log(Level level, std::string_view fmt, std::format_args args) -> void;
};

} // namespace bc::core

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#if defined(BC_ENABLE_LOGS_ACTIVE) && BC_ENABLE_LOGS_ACTIVE == 1

#define BC_TRACE(fmt, ...)                                                                         \
    ::bc::core::Logger::Log(::bc::core::Level::Trace, fmt,                                         \
                            std::make_format_args(__VA_OPT__(__VA_ARGS__)))
#define BC_DEBUG(fmt, ...)                                                                         \
    ::bc::core::Logger::Log(::bc::core::Level::Debug, fmt,                                         \
                            std::make_format_args(__VA_OPT__(__VA_ARGS__)))
#define BC_INFO(fmt, ...)                                                                          \
    ::bc::core::Logger::Log(::bc::core::Level::Info, fmt,                                          \
                            std::make_format_args(__VA_OPT__(__VA_ARGS__)))
#define BC_WARN(fmt, ...)                                                                          \
    ::bc::core::Logger::Log(::bc::core::Level::Warn, fmt,                                          \
                            std::make_format_args(__VA_OPT__(__VA_ARGS__)))
#define BC_ERROR(fmt, ...)                                                                         \
    ::bc::core::Logger::Log(::bc::core::Level::Error, fmt,                                         \
                            std::make_format_args(__VA_OPT__(__VA_ARGS__)))
#define BC_CRITICAL(fmt, ...)                                                                      \
    ::bc::core::Logger::Log(::bc::core::Level::Critical, fmt,                                      \
                            std::make_format_args(__VA_OPT__(__VA_ARGS__)))

#else

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while)
#define BC_TRACE(fmt, ...)                                                                         \
    do {                                                                                           \
    } while (0)
#define BC_DEBUG(fmt, ...)                                                                         \
    do {                                                                                           \
    } while (0)
#define BC_INFO(fmt, ...)                                                                          \
    do {                                                                                           \
    } while (0)
#define BC_WARN(fmt, ...)                                                                          \
    do {                                                                                           \
    } while (0)
#define BC_ERROR(fmt, ...)                                                                         \
    do {                                                                                           \
    } while (0)
#define BC_CRITICAL(fmt, ...)                                                                      \
    do {                                                                                           \
    } while (0)

#endif
// NOLINTEND(cppcoreguidelines-avoid-do-while)
// NOLINTEND(cppcoreguidelines-macro-usage)

#endif // BC_LIBS_CORE_INCLUDE_LOGGER_H_
