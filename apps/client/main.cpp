#include <sodium.h>

#include <core/logger.h>

auto main() -> int
{
    bc::core::Logger::Init();
    BC_INFO("Running client...");
    return 0;
}
