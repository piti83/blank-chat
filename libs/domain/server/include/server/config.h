#ifndef BC_LIBS_DOMAIN_SERVER_INCLUDE_CONFIG_H_
#define BC_LIBS_DOMAIN_SERVER_INCLUDE_CONFIG_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace bc::domain::server {

constexpr uint8_t defaultMemoryQuotaPercent = 80;
constexpr uint32_t defaultMaxMessagesPerMailbox = 50;

struct NetworkConfig
{
    std::string listenHost{"127.0.0.1"};
    std::uint16_t listenPort{0};
    std::string torControlHost{"127.0.0.1"};
    std::uint16_t torControlPort{0};
};

struct SecurityConfig
{
    std::uint8_t memoryQuotaPercent{defaultMemoryQuotaPercent};
    std::uint32_t maxMessagesPerMailbox{defaultMaxMessagesPerMailbox};
};

struct ServerConfig
{
    NetworkConfig networkConfig;
    SecurityConfig securityConfig;
};

[[nodiscard]] auto LoadConfig(const std::filesystem::path& configFilePath)
    -> std::optional<ServerConfig>;

} // namespace bc::domain::server

#endif // BC_LIBS_DOMAIN_SERVER_INCLUDE_CONFIG_H_
