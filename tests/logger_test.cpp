#include "utils/log_modules.h"
#include "utils/logger.h"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

using namespace bc::utils;

class LoggerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        InitLogging();
    }

    void TearDown() override
    {
        ShutdownLogging();
    }
};

TEST_F(LoggerTest, GetLoggerReturnsValidPointerWhenLogsAreEnabled)
{
    auto logger = GetLogger(LogModule::Utils);

#ifdef BC_ENABLE_LOGS
    EXPECT_NE(logger, nullptr) << "Logger should not be NULL after initialization.";
    EXPECT_EQ(logger->name(), "Utils");
#else
    EXPECT_EQ(logger, nullptr);
#endif
}

TEST_F(LoggerTest, AllModulesHaveInstantiatedLoggers)
{
#ifdef BC_ENABLE_LOGS
    EXPECT_NE(GetLogger(LogModule::Crypto), nullptr);
    EXPECT_NE(GetLogger(LogModule::Network), nullptr);
    EXPECT_NE(GetLogger(LogModule::Protocol), nullptr);
    EXPECT_NE(GetLogger(LogModule::Utils), nullptr);
    EXPECT_NE(GetLogger(LogModule::Server), nullptr);
    EXPECT_NE(GetLogger(LogModule::Client), nullptr);
#else
    GTEST_SKIP() << "Logi są wyłączone (brak definicji BC_ENABLE_LOGS). Pomijam test.";
#endif
}

TEST_F(LoggerTest, LoggingMacrosDoNotCrash)
{
    auto logger = GetLogger(LogModule::Client);

#ifdef BC_ENABLE_LOGS
    ASSERT_NE(logger, nullptr)
        << "Logger is NULL. Trying to use logger would crash the program in this case.";
#endif

    EXPECT_NO_FATAL_FAILURE({
        LOG_TRACE(logger, "Test trace message");
        LOG_DEBUG(logger, "Test debug message");
        LOG_INFO(logger, "Test info message");
        LOG_WARN(logger, "Test warn message");
        LOG_ERROR(logger, "Test error message");
        LOG_CRITICAL(logger, "Test critical message");
    });
}

TEST_F(LoggerTest, ShutdownLoggingClearsLoggers)
{
#ifdef BC_ENABLE_LOGS
    auto logger = GetLogger(LogModule::Network);
    ASSERT_NE(logger, nullptr);

    logger.reset();

    ShutdownLogging();

    SUCCEED();
#else
    GTEST_SKIP() << "Logs are turned off.";
#endif
}
