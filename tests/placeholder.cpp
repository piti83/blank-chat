#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

TEST(SanityCheck, BasicMath) {
    EXPECT_EQ(2 + 2, 4);
}

TEST(SanityCheck, LoggingWorks) {
    spdlog::info("Test log from inside GoogleTest");
    ASSERT_TRUE(true);
}
