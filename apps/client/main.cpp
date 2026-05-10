#include <sodium.h>

#include <core/logger.h>

auto main() -> int
{
    try {
        bc::core::Logger::Init();
        BC_INFO("Running client...");
        return 0;
    } catch (const std::exception& e) {
        return 1;
    } catch (...) {
        return 2;
    }
}
