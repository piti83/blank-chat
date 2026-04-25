#include "log_level.h"
#include "logger.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace bc::core::logger {

auto Logger::Init() -> void
{
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_st>();
    consoleSink->set_level(spdlog::level::trace);

    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_st>("logs/bc_log.txt", true);
    fileSink->set_level(spdlog::level::trace);

    std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};

    auto logger = std::make_shared<spdlog::logger>("BC", sinks.begin(), sinks.end());
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::warn);
}

auto Logger::Log(Level level, std::string_view fmt, std::format_args args) -> void
{
    auto spdlogLevel = MapLevel(level);
    auto logger = spdlog::default_logger();

    if (!logger || !logger->should_log(spdlogLevel)) {
        return;
    }

    std::string formatted = std::vformat(fmt, args);
    logger->log(spdlogLevel, formatted);
}

} // namespace bc::core::logger
