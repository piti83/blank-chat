#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_OBFUSCATION_TIMER_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_OBFUSCATION_TIMER_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace bc::domain::client {

class IObfuscationTimer
{
public:
    IObfuscationTimer() = default;
    virtual ~IObfuscationTimer() = default;

    IObfuscationTimer(const IObfuscationTimer&) = delete;
    auto operator=(const IObfuscationTimer&) -> IObfuscationTimer& = delete;
    IObfuscationTimer(IObfuscationTimer&&) = delete;
    auto operator=(IObfuscationTimer&&) -> IObfuscationTimer& = delete;

    [[nodiscard]] virtual auto GetNextInterval() noexcept -> std::chrono::milliseconds = 0;
};

class CbrTimer : public IObfuscationTimer
{
public:
    explicit CbrTimer(std::uint32_t intervalMs) noexcept;
    [[nodiscard]] auto GetNextInterval() noexcept -> std::chrono::milliseconds override;

private:
    std::chrono::milliseconds interval;
};

class PoissonTimer : public IObfuscationTimer
{
public:
    explicit PoissonTimer(float eventsPerSecond) noexcept;
    [[nodiscard]] auto GetNextInterval() noexcept -> std::chrono::milliseconds override;

private:
    float lambda;
};

[[nodiscard]] auto CreateObfuscationTimer(const std::string& mode, std::uint32_t cbrInterval,
                                          float poissonLambda)
    -> std::unique_ptr<IObfuscationTimer>;

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_OBFUSCATION_TIMER_H_
