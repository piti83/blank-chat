#include <exception>

#include <sodium.h>

#include <core/logger.h>

#include <cli/repl.h>

auto main() -> int
{
    try {
        bc::core::Logger::Init();

        if (sodium_init() < 0) {
            BC_CRITICAL("High Assurance Crash: Failed to initialize libsodium subsystem.");
            return 1;
        }

        bc::cli::Repl repl;
        repl.Run();

        return 0;
    } catch (const std::exception& e) {
        BC_CRITICAL("Fatal exception: {}", e.what());
        return 1;
    } catch (...) {
        BC_CRITICAL("Unknown fatal exception.");
        return 2;
    }
}
