#include <iostream>
#include <primitives.hpp>
#include <sodium.h>

auto main() -> int
{
    std::cout << "Running client...\n";
    bc::crypto::primitives::Primitives primitives;
    if (sodium_init() == 0) {
        std::cout << "Sodium linking works...\n";
    }
    return 0;
}
