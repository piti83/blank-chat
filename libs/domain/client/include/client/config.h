#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONFIG_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONFIG_H_

#include <filesystem>
#include <optional>

namespace bc::domain::client {

struct NetworkConfig
{
};

struct ObfuscationConfig
{
};

struct StorageConfig
{
};

struct ClientConfig
{
    NetworkConfig networkConfig;
    ObfuscationConfig obfuscationConfig;
    StorageConfig storageConfig;
};

[[nodiscard]] auto LoadConfig(const std::filesystem::path& configFilePath)
    -> std::optional<ClientConfig>;

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONFIG_H_
