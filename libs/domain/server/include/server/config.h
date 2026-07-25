#ifndef BC_LIBS_DOMAIN_SERVER_INCLUDE_CONFIG_H_
#define BC_LIBS_DOMAIN_SERVER_INCLUDE_CONFIG_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <server/server_types.h>

namespace bc::domain::server {

struct NetworkConfig
{
    std::string listenHost;
    std::uint16_t listenPort{0};
    std::string torControlHost;
    std::uint16_t torControlPort{0};
};

struct SecurityConfig
{
    std::uint8_t memoryQuotaPercent{0};
    std::uint32_t maxMessagesPerMailbox{0};
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
