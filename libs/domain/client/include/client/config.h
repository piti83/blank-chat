#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONFIG_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONFIG_H_

#include <cstdint>
#include <filesystem>
#include <optional>

namespace bc::domain::client {

constexpr std::uint16_t defaultTorSocksPort = 9050;
constexpr std::uint16_t defaultOnionPort = 80;
constexpr std::uint32_t defaultCbrIntervalMs = 5000;
constexpr float defaultPoissonLambda = 5.0F;

struct NetworkConfig
{
    std::string torSocksHost{"127.0.0.1"};
    std::uint16_t torSocksPort{defaultTorSocksPort};
};

struct RelayConfig
{
    std::string onionAddress{"CHANGE_ME.onion"};
    std::uint16_t onionPort{defaultOnionPort};
};

struct ObfuscationConfig
{
    std::string mode{"cbr"};
    std::uint32_t cbr_interval_ms{defaultCbrIntervalMs};
    float poissonLambda{defaultPoissonLambda};
};

struct StorageConfig
{
    std::string contactsFilePath{"contacts.json"};
};

struct ClientConfig
{
    NetworkConfig networkConfig;
    RelayConfig relayConfig;
    ObfuscationConfig obfuscationConfig;
    StorageConfig storageConfig;
};

[[nodiscard]] auto LoadConfig(const std::filesystem::path& configFilePath)
    -> std::optional<ClientConfig>;

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONFIG_H_
