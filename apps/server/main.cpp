#include <core/logger.h>
#include <sodium.h>

auto main() -> int
{
    bc::core::Logger::Init();
    BC_INFO("Running server...");
    return 0;
}
