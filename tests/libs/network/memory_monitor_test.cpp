#include <chrono>
#include <optional>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#define private public
#include <network/memory_monitor.h>
#undef private

namespace bc::network::test {

class MemoryMonitorTest : public ::testing::Test
{
protected:
    boost::asio::io_context ioContext;
};

TEST_F(MemoryMonitorTest, ParseVmRSS_SuccessfullyExtractsValidValues)
{
    EXPECT_EQ(MemoryMonitor::ParseVmRSS("Name:\tbash\nVmRSS:\t 1024 kB\nVmHWM:\t 5000 kB"), 1024);
    EXPECT_EQ(MemoryMonitor::ParseVmRSS("VmRSS: 0"), 0);
    EXPECT_EQ(MemoryMonitor::ParseVmRSS("VmRSS: 99999999"), 99999999);
}

TEST_F(MemoryMonitorTest, ParseVmRSS_FailsSecurelyOnMalformedInput)
{
    EXPECT_FALSE(MemoryMonitor::ParseVmRSS("Name: bash\nVmLck: 1000 kB").has_value());

    EXPECT_FALSE(MemoryMonitor::ParseVmRSS("VmRSS:    \t  \n").has_value());

    EXPECT_FALSE(MemoryMonitor::ParseVmRSS("VmRSS:").has_value());

    EXPECT_FALSE(MemoryMonitor::ParseVmRSS("VmRSS: NaN").has_value());
}

TEST_F(MemoryMonitorTest, ParseVmRSS_FailsSecurelyOnIntegerOverflow)
{
    EXPECT_FALSE(MemoryMonitor::ParseVmRSS("VmRSS: 999999999999999999999999999999").has_value());
}

TEST_F(MemoryMonitorTest, ReadStatusFile_SucceedsOnNormalExecution)
{
    std::array<char, 2048> buffer{};
    std::size_t bytesRead = MemoryMonitor::ReadStatusFile(buffer);
    EXPECT_GT(bytesRead, 0) << "Should successfully read /proc/self/status on Linux";
}

TEST_F(MemoryMonitorTest, ReadStatusFile_FailsSecurelyWhenFileDescriptorsExhausted)
{

    std::vector<int> fds;
    while (true) {
        int fd = ::open("/dev/null", O_RDONLY);
        if (fd < 0) {
            break;
        }
        fds.push_back(fd);
    }

    std::array<char, 10> buf{};
    std::size_t bytesRead = MemoryMonitor::ReadStatusFile(buf);
    EXPECT_EQ(bytesRead, 0) << "Should fail safely returning 0 when fd allocation fails";

    for (int fd : fds) {
        ::close(fd);
    }
}

TEST_F(MemoryMonitorTest, CalculateMemoryLimit_CalculatesProperly)
{
    std::uint64_t limit80 = MemoryMonitor::CalculateMemoryLimit(80);
    std::uint64_t limit40 = MemoryMonitor::CalculateMemoryLimit(40);
    EXPECT_GT(limit80, 0);

    std::uint64_t maxVal = std::max(limit80, limit40 * 2);
    std::uint64_t minVal = std::min(limit80, limit40 * 2);
    EXPECT_LE(maxVal - minVal, 10) << "Limits differ by more than acceptable truncation margin";
}

TEST_F(MemoryMonitorTest, CheckMemory_TriggersExceededFlagWhenVmRSSIsHigh)
{
    MemoryMonitor monitor(ioContext, 80);

    monitor.memoryLimitBytes = 0;
    monitor.isQuotaExceeded = false;

    monitor.CheckMemory();

    EXPECT_TRUE(monitor.IsQuotaExceeded()) << "Monitor should raise the alarm";
}

TEST_F(MemoryMonitorTest, CheckMemory_TriggersRecoveryWhenVmRSSDropsBelowLimit)
{
    MemoryMonitor monitor(ioContext, 80);

    monitor.memoryLimitBytes = static_cast<std::uint64_t>(-1);
    monitor.isQuotaExceeded = true;

    monitor.CheckMemory();

    EXPECT_FALSE(monitor.IsQuotaExceeded()) << "Monitor should recover and drop the alarm";
}

TEST_F(MemoryMonitorTest, TimerExecutesAutomaticallyAndDoesNotCrash)
{

    MemoryMonitor monitor(ioContext, 80);

    ioContext.run_for(std::chrono::milliseconds(1100));

    SUCCEED() << "Timer executed background loop successfully without aborting";
}

} // namespace bc::network::test
