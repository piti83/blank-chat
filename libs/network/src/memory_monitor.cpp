#include "network/memory_monitor.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <iterator>
#include <system_error>

#include <core/logger.h>

#include "network/network_types.h"
#include <fcntl.h>
#include <unistd.h>

namespace bc::network {

MemoryMonitor::MemoryMonitor(boost::asio::io_context& ioContext, std::uint8_t quotaPercent)
    : timer(ioContext), memoryLimitBytes(CalculateMemoryLimit(quotaPercent))
{
    BC_INFO("Memory Monitor initialized. Quota: {}% ({} bytes)", quotaPercent, memoryLimitBytes);
    StartTimer();
}

MemoryMonitor::~MemoryMonitor()
{
    timer.cancel();
}

auto MemoryMonitor::IsQuotaExceeded() const noexcept -> bool
{
    return isQuotaExceeded.load(std::memory_order_relaxed);
}

auto MemoryMonitor::StartTimer() -> void
{
    timer.expires_after(std::chrono::seconds(1));
    timer.async_wait([this](const boost::system::error_code& ec) -> void {
        if (!ec) {
            CheckMemory();
            StartTimer();
        }
    });
}

auto MemoryMonitor::ReadStatusFile(std::span<char> buffer) noexcept -> std::size_t
{
    int fd = ::open("/proc/self/status", O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    ssize_t bytesRead = ::read(fd, buffer.data(), buffer.size() - 1);

    if (::close(fd) < 0) {
    }

    return bytesRead > 0 ? static_cast<std::size_t>(bytesRead) : 0;
}

auto MemoryMonitor::ParseVmRSS(std::string_view status) noexcept -> std::optional<std::uint64_t>
{
    auto pos = status.find("VmRSS:");
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }

    const auto* startIt =
        std::next(status.begin(), static_cast<std::ptrdiff_t>(pos + vmLckPrefixLength));

    auto isSpace = [](char c) -> bool { return c == ' ' || c == '\t'; };
    auto isDigit = [](char c) -> bool { return c >= '0' && c <= '9'; };

    const auto* numStart = std::find_if_not(startIt, status.end(), isSpace);
    if (numStart == status.end() || !isDigit(*numStart)) {
        return std::nullopt;
    }

    const auto* numEnd = std::find_if_not(numStart, status.end(), isDigit);

    const char* firstPtr = std::to_address(numStart);
    const char* lastPtr = std::to_address(numEnd);

    std::uint64_t vmRssKb = 0;
    auto res = std::from_chars(firstPtr, lastPtr, vmRssKb);

    if (res.ec == std::errc()) {
        return vmRssKb;
    }

    return std::nullopt;
}

auto MemoryMonitor::CheckMemory() noexcept -> void
{
    std::array<char, statusBufferSize> buffer{};
    std::size_t bytesRead = ReadStatusFile(buffer);

    if (bytesRead > 0) {
        std::string_view status(buffer.data(), bytesRead);
        auto vmRssKb = ParseVmRSS(status);

        if (vmRssKb.has_value()) {
            std::uint64_t vmRssBytes = *vmRssKb * bytesInKb;
            bool exceeded = vmRssBytes >= memoryLimitBytes;

            bool currentlyExceeded = isQuotaExceeded.load(std::memory_order_relaxed);

            if (exceeded && !currentlyExceeded) {
                BC_WARN("CRITICAL: Resident memory ({} bytes) exceeded quota! Dropping new "
                        "connections.",
                        vmRssBytes);
            }

            if (!exceeded && currentlyExceeded) {
                BC_INFO("Resident memory ({} bytes) dropped below quota. Accepting connections.",
                        vmRssBytes);
            }

            if (exceeded != currentlyExceeded) {
                isQuotaExceeded.store(exceeded, std::memory_order_relaxed);
            }
        }
    }
}

auto MemoryMonitor::CalculateMemoryLimit(std::uint8_t percent) noexcept -> std::uint64_t
{
    long pages = sysconf(_SC_PHYS_PAGES);
    long pageSize = sysconf(_SC_PAGESIZE);

    if (pages > 0 && pageSize > 0) {
        std::uint64_t totalRam =
            static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize);
        return (totalRam * percent) / percentMax;
    }

    return defaultFallbackRam * percent / percentMax;
}

} // namespace bc::network
