#ifndef BC_LIBS_CORE_INCLUDE_LOGGER_H_
#define BC_LIBS_CORE_INCLUDE_LOGGER_H_

#include <format>
#include <iostream>
#include <utility>

#include <core/log_level.h>

namespace bc::core {

class Logger
{
public:
    static auto Init() -> void;

    template <typename... Args>
    static auto Log(Level level, std::format_string<Args...> fmt, Args&&... args) noexcept -> void
    {
        auto spdlogLevel = MapLevel(level);
        auto logger = spdlog::default_logger();

        if (!logger || !logger->should_log(spdlogLevel)) {
            return;
        }

        logger->log(spdlogLevel, std::format(fmt, std::forward<Args>(args)...));
    }
};

} // namespace bc::core

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#if defined(BC_ENABLE_LOGS_ACTIVE) && BC_ENABLE_LOGS_ACTIVE == 1

#define BC_TRACE(...) ::bc::core::Logger::Log(::bc::core::Level::Trace, __VA_ARGS__)
#define BC_DEBUG(...) ::bc::core::Logger::Log(::bc::core::Level::Debug, __VA_ARGS__)
#define BC_INFO(...) ::bc::core::Logger::Log(::bc::core::Level::Info, __VA_ARGS__)
#define BC_WARN(...) ::bc::core::Logger::Log(::bc::core::Level::Warn, __VA_ARGS__)
#define BC_ERROR(...) ::bc::core::Logger::Log(::bc::core::Level::Error, __VA_ARGS__)
#define BC_CRITICAL(...) ::bc::core::Logger::Log(::bc::core::Level::Critical, __VA_ARGS__)

#else

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while)
#define BC_TRACE(...)                                                                              \
    do {                                                                                           \
    } while (0)
#define BC_DEBUG(...)                                                                              \
    do {                                                                                           \
    } while (0)
#define BC_INFO(...)                                                                               \
    do {                                                                                           \
    } while (0)
#define BC_WARN(...)                                                                               \
    do {                                                                                           \
    } while (0)
#define BC_ERROR(...)                                                                              \
    do {                                                                                           \
    } while (0)
#define BC_CRITICAL(...)                                                                           \
    do {                                                                                           \
    } while (0)

#endif
// NOLINTEND(cppcoreguidelines-avoid-do-while)
// NOLINTEND(cppcoreguidelines-macro-usage)

#endif // BC_LIBS_CORE_INCLUDE_LOGGER_H_
