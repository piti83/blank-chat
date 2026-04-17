#include <iostream>
#include <primitives.hpp>
#include <sodium.h>

auto main() -> int
{
    std::cout << "Running server...\n";
    bc::crypto::primitives::Primitives primitives;
    if (sodium_init()) {
        std::cout << "Sodium linking works...\n";
    }
    return 0;
}
