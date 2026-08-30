#ifndef BC_LIBS_NETWORK_INCLUDE_MEMORY_MONITOR_H_
#define BC_LIBS_NETWORK_INCLUDE_MEMORY_MONITOR_H_

#include <atomic>
#include <cstdint>

#include <boost/asio.hpp>

namespace bc::network {

class MemoryMonitor
{
public:
    MemoryMonitor(boost::asio::io_context& ioContext, std::uint8_t quotaPercent);
    ~MemoryMonitor();

    MemoryMonitor(const MemoryMonitor&) = delete;
    auto operator=(const MemoryMonitor&) -> MemoryMonitor& = delete;
    MemoryMonitor(MemoryMonitor&&) = delete;
    auto operator=(MemoryMonitor&&) -> MemoryMonitor& = delete;

    [[nodiscard]] auto IsQuotaExceeded() const noexcept -> bool;

private:
    auto StartTimer() -> void;
    auto CheckMemory() noexcept -> void;

    [[nodiscard]] static auto ReadStatusFile(std::span<char> buffer) noexcept -> std::size_t;
    [[nodiscard]] static auto ParseVmRSS(std::string_view status) noexcept
        -> std::optional<std::uint64_t>;
    [[nodiscard]] static auto CalculateMemoryLimit(std::uint8_t percent) noexcept -> std::uint64_t;

    boost::asio::steady_timer timer;
    std::uint64_t memoryLimitBytes;
    std::atomic<bool> isQuotaExceeded{false};
};

} // namespace bc::network

#endif // BC_LIBS_NETWORK_INCLUDE_MEMORY_MONITOR_H_
