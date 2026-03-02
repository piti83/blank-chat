#pragma once

/* clang-format off */
#ifdef BC_ENABLE_LOGS
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_TRACE
#else
    #define SPDLOG_ACTIVE_LEVEL SPDLOG_OFF
#endif
/* clang-format on */

#include "log_modules.h"

#include <memory>
#include <spdlog/spdlog.h>

namespace bc::utils {

auto InitLogging() -> void;

auto ShutdownLogging() -> void;

auto GetLogger(LogModule module) -> std::shared_ptr<spdlog::logger>;

} // namespace bc::utils

// NOLINTBEGIN (cppcoreguidelines-macro-usage)
// It's for logging. As it has to be as close to 0-overhead as possible NOLINT just to be done.
// It is also needed for __FILE__ and __LINE__ to work.
#define LOG_TRACE(logger, ...) SPDLOG_LOGGER_TRACE(logger, __VA_ARGS__)
#define LOG_DEBUG(logger, ...) SPDLOG_LOGGER_DEBUG(logger, __VA_ARGS__)
#define LOG_INFO(logger, ...) SPDLOG_LOGGER_INFO(logger, __VA_ARGS__)
#define LOG_WARN(logger, ...) SPDLOG_LOGGER_WARN(logger, __VA_ARGS__)
#define LOG_ERROR(logger, ...) SPDLOG_LOGGER_ERROR(logger, __VA_ARGS__)
#define LOG_CRITICAL(logger, ...) SPDLOG_LOGGER_CRITICAL(logger, __VA_ARGS__)
// NOLINTEND
