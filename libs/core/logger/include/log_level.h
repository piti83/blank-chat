#ifndef BC_LIBS_CORE_LOGGER_INCLUDE_LOGLEVEL_H_
#define BC_LIBS_CORE_LOGGER_INCLUDE_LOGLEVEL_H_

#include <cstdint>
#include <spdlog/spdlog.h>

namespace bc::core::logger {

enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error, Critical };

static constexpr auto MapLevel(Level level) -> spdlog::level::level_enum
{
    switch (level) {
    case Level::Trace:
        return spdlog::level::trace;
    case Level::Debug:
        return spdlog::level::debug;
    case Level::Info:
        return spdlog::level::info;
    case Level::Warn:
        return spdlog::level::warn;
    case Level::Error:
        return spdlog::level::err;
    case Level::Critical:
        return spdlog::level::critical;
    }
    return spdlog::level::off;
}

} // namespace bc::core::logger

#endif // BC_LIBS_CORE_LOGGER_INCLUDE_LOGLEVEL_H_
