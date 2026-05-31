#include <exception>

#include <sodium.h>

#include <core/logger.h>

#include <cli/repl.h>

auto main() -> int
{
    bc::core::Logger::Init();

    bc::cli::Repl repl;
    repl.Run();

    return 0;
}
