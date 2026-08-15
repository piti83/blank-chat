#include <algorithm>
#include <cmath>

#include <sodium.h>

#include <client/client_types.h>
#include <core/logger.h>

#include "client/obfuscation_timer.h"

namespace bc::domain::client {

CbrTimer::CbrTimer(std::uint32_t intervalMs) noexcept : interval(intervalMs)
{
}

auto CbrTimer::GetNextInterval() noexcept -> std::chrono::milliseconds
{
    return interval;
}

PoissonTimer::PoissonTimer(float eventsPerSecond) noexcept : lambda(eventsPerSecond)
{
    if (lambda <= 0.0F) {
        BC_ERROR("Poisson lambda must be > 0. Reverting to default high-assurance lambda: 1.0");
        lambda = 1.0F;
    }
}

auto PoissonTimer::GetNextInterval() noexcept -> std::chrono::milliseconds
{
    std::uint32_t randVal = randombytes_random();

    // u = R / 2^32
    double uniformRandom = static_cast<double>(randVal) / maxRandomUint32;

    uniformRandom = std::min(uniformRandom, maxUniformRandom);

    double intervalSeconds = -std::log(1.0 - uniformRandom) / static_cast<double>(lambda);
    double intervalMs = intervalSeconds * msPerSecond;

    double clampedMs = std::max(intervalMs, static_cast<double>(minClampMs));
    return std::chrono::milliseconds(static_cast<long long>(clampedMs));
}

auto CreateObfuscationTimer(const std::string& mode, std::uint32_t cbrInterval, float poissonLambda)
    -> std::unique_ptr<IObfuscationTimer>
{
    if (mode == "poisson") {
        BC_INFO("Initializing Stochastic Poisson Obfuscator (Lambda: {:.2f}, MinClamp: 100ms)",
                poissonLambda);
        return std::make_unique<PoissonTimer>(poissonLambda);
    }
    BC_INFO("Initializing Constant Bit Rate (CBR) Obfuscator (Interval: {}ms)", cbrInterval);
    return std::make_unique<CbrTimer>(cbrInterval);
}

} // namespace bc::domain::client
