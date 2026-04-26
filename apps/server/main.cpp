#include <logger.h>
#include <sodium.h>

auto main() -> int
{
    bc::core::logger::Logger::Init();
    BC_INFO("Running server...");
    return 0;
}
