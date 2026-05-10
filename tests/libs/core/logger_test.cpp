#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <core/logger.h>

class LoggerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::filesystem::create_directories("logs");
        if (std::filesystem::exists(logFilePath)) {
            std::filesystem::remove(logFilePath);
        }
    }

    void TearDown() override
    {
        spdlog::drop_all();
        if (std::filesystem::exists(logFilePath)) {
            std::filesystem::remove(logFilePath);
        }
    }

    std::string logFilePath = "logs/bc_log.txt";
};

TEST_F(LoggerTest, InitConfiguresDefaultLoggerProperly)
{
    bc::core::Logger::Init();
    auto logger = spdlog::default_logger();

    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->name(), "BC");
    EXPECT_EQ(logger->level(), spdlog::level::trace);
    EXPECT_EQ(logger->flush_level(), spdlog::level::warn);
}

TEST_F(LoggerTest, MacrosLogToFileSuccessfully)
{
    bc::core::Logger::Init();

    BC_TRACE("Test {} message", "trace");
    BC_DEBUG("Test {} message", "debug");
    BC_INFO("Test {} message", "info");
    BC_WARN("Test {} message", "warn");
    BC_ERROR("Test {} message", "error");
    BC_CRITICAL("Test {} message", "critical");

    spdlog::default_logger()->flush();
    ASSERT_TRUE(std::filesystem::exists(logFilePath));

    std::ifstream logFile(logFilePath);
    std::string content((std::istreambuf_iterator<char>(logFile)),
                        std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("Test trace message"), std::string::npos);
    EXPECT_NE(content.find("Test critical message"), std::string::npos);
}

TEST_F(LoggerTest, HandlesNullLoggerGracefully)
{
    spdlog::drop_all();
    BC_INFO("This goes nowhere safely");
    SUCCEED();
}
