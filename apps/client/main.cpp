#include "utils/logger.h"

#include <spdlog/spdlog.h>

auto main() -> int
{
    bc::utils::InitLogging();
    auto logger = bc::utils::GetLogger(bc::utils::LogModule::Client);
    LOG_INFO(logger, "Starting client app.");
    LOG_CRITICAL(logger, "Critical test.");
    LOG_DEBUG(logger, "Debug test.");
    bc::utils::ShutdownLogging();
    return 0;
}
