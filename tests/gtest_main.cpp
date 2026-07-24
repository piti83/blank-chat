#include <gtest/gtest.h>
#include <sodium.h>

auto main(int argc, char** argv) -> int
{
    if (sodium_init() < 0) {
        return 1;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
