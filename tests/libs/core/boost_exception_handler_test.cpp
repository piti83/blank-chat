#include <stdexcept>

#include <boost/assert/source_location.hpp>
#include <gtest/gtest.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace boost {
void throw_exception(const std::exception& err);
void throw_exception(const std::exception& err, const boost::source_location& loc);
} // namespace boost

namespace bc::core::test {

class BoostExceptionHandlerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto stderrSink = std::make_shared<spdlog::sinks::stderr_color_sink_st>();
        auto testLogger = std::make_shared<spdlog::logger>("death_test_logger", stderrSink);

        testLogger->set_pattern("%v");

        testLogger->flush_on(spdlog::level::critical);

        spdlog::set_default_logger(testLogger);
    }

    void TearDown() override
    {
        spdlog::drop_all();
    }
};

TEST_F(BoostExceptionHandlerTest, ThrowExceptionWithoutLocationAbortsSystem)
{
    EXPECT_DEATH(boost::throw_exception(std::runtime_error("simulated critical core failure")),
                 "FATAL BOOST ERROR.*simulated critical core failure");
}

TEST_F(BoostExceptionHandlerTest, ThrowExceptionWithLocationAbortsSystem)
{
    boost::source_location loc{"secure_enclave.cpp", 1337, "DecryptPayload"};

    EXPECT_DEATH(boost::throw_exception(std::runtime_error("simulated memory corruption"), loc),
                 "FATAL BOOST ERROR at secure_enclave.cpp:1337: simulated memory corruption");
}

} // namespace bc::core::test
