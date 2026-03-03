#include "utils/logger.h"

#include <spdlog/sinks/stdout_sinks.h>

#ifdef BC_ENABLE_LOGS
#include "utils/log_modules.h"

#include <filesystem>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace bc::utils {

#ifdef BC_ENABLE_LOGS

constexpr uint16_t logQueueSize = 8192;
constexpr uint16_t logThreadCount = 1;

constexpr std::array<std::string_view, static_cast<uint8_t>(LogModule::Count)> moduleNames = {
    "Crypto", "Network", "Protocol", "Utils", "Server", "Client"};

// NOLINTBEGIN (cppcoreguidelines-avoid-non-const-global-variables)
// Seems like there is no better way of doing this. Just be careful and do not modify it. Its
// handled by logging system :)
static std::array<std::shared_ptr<spdlog::logger>, static_cast<uint8_t>(LogModule::Count)> loggers;
// NOLINTEND

#endif

void InitLogging()
{
#ifdef BC_ENABLE_LOGS
    std::filesystem::create_directories("logs");
    spdlog::init_thread_pool(logQueueSize, logThreadCount);

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/bc.log", true);

    std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};

    const std::string logPattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] [%s:%#] %v";
    for (auto& sink : sinks) {
        sink->set_pattern(logPattern);
    }

    for (uint8_t i = 0; i < static_cast<uint8_t>(LogModule::Count); ++i) {
        auto logger = std::make_shared<spdlog::async_logger>(
            std::string(moduleNames.at(i)), sinks.begin(), sinks.end(), spdlog::thread_pool(),
            spdlog::async_overflow_policy::overrun_oldest);
        loggers.at(i) = logger;
        spdlog::register_logger(logger);
    }

    spdlog::set_level(spdlog::level::trace);
#endif
}

void ShutdownLogging()
{
#ifdef BC_ENABLE_LOGS
    for (auto& logger : loggers) {
        logger.reset();
    }
    spdlog::shutdown();
#endif
}

auto GetLogger(LogModule module) -> std::shared_ptr<spdlog::logger>
{
#ifdef BC_ENABLE_LOGS
    return loggers.at(static_cast<size_t>(module));
#else
    return nullptr;
#endif
}

} // namespace bc::utils
