#include "core/logger.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <core/core_types.h>

namespace bc::core {

auto Logger::Init() -> void
{
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(spdlog::level::trace);

    std::vector<spdlog::sink_ptr> sinks{consoleSink};

#if defined(BC_ENABLE_LOGS_ACTIVE) && BC_ENABLE_LOGS_ACTIVE == 1
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/bc_log.txt", true);
    fileSink->set_level(spdlog::level::trace);
    sinks.push_back(fileSink);
#endif

    auto logger = std::make_shared<spdlog::logger>("BC", sinks.begin(), sinks.end());
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::warn);
}

} // namespace bc::core
