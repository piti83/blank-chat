#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONFIG_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONFIG_H_

#include <cstdint>
#include <filesystem>
#include <optional>

#include <client/client_types.h>
#include <network/network_types.h>

namespace bc::domain::client {

struct NetworkConfig
{
    std::string torSocksHost;
    std::uint16_t torSocksPort{0};
};

struct RelayConfig
{
    std::string onionAddress;
    std::uint16_t onionPort{0};
};

struct ObfuscationConfig
{
    std::string mode;
    std::uint32_t cbr_interval_ms{0};
    float poissonLambda{0.0F};
};

struct StorageConfig
{
    std::string contactsFilePath;
};

struct SecurityConfig
{
    std::uint32_t pfsMessageInterval{0};
};

struct ClientConfig
{
    NetworkConfig networkConfig;
    RelayConfig relayConfig;
    ObfuscationConfig obfuscationConfig;
    StorageConfig storageConfig;
    SecurityConfig securityConfig;
};

[[nodiscard]] auto LoadConfig(const std::filesystem::path& configFilePath)
    -> std::optional<ClientConfig>;

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONFIG_H_
