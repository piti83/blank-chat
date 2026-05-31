#include <filesystem>
#include <optional>

#include <core/logger.h>

#include "client/config.h"
#include <toml++/toml.hpp>

namespace bc::domain::client {

auto LoadConfig(const std::filesystem::path& configFilePath) -> std::optional<ClientConfig>
{
    toml::parse_result result = toml::parse_file(configFilePath.string());

    if (!result) {
        BC_ERROR("Failed to parse client config file: {}", result.error().description());
        return std::nullopt;
    }

    const toml::table& table = result.table();
    ClientConfig config;

    // Tor SOCKS Host
    if (auto opt = table.at_path("network.tor_socks_host").value<std::string>()) {
        config.networkConfig.torSocksHost = std::move(*opt);
    } else {
        BC_ERROR("Missing or invalid [network][tor_socks_host] in client config file: {}",
                 configFilePath.string());
        return std::nullopt;
    }

    // Tor SOCKS Port
    if (auto opt = table.at_path("network.tor_socks_port").value<std::uint16_t>()) {
        config.networkConfig.torSocksPort = *opt;
    } else {
        BC_ERROR("Missing or invalid [network][tor_socks_port] in client config file: {}",
                 configFilePath.string());
        return std::nullopt;
    }

    // Mode
    if (auto opt = table.at_path("obfuscation.mode").value<std::string>()) {
        config.obfuscationConfig.mode = std::move(*opt);
    } else {
        BC_ERROR("Missing or invalid [obfuscation][mode] in client config file: {}",
                 configFilePath.string());
        return std::nullopt;
    }

    // CBR Interval MS
    if (auto opt = table.at_path("obfuscation.cbr_interval_ms").value<std::uint32_t>()) {
        config.obfuscationConfig.cbr_interval_ms = *opt;
    } else {
        BC_ERROR("Missing or invalid [obfuscation][cbr_interval_ms] in client config file: {}",
                 configFilePath.string());
        return std::nullopt;
    }

    // Poisson Lambda
    if (auto opt = table.at_path("obfuscation.poisson_lambda").value<float>()) {
        config.obfuscationConfig.poissonLambda = *opt;
    } else {
        BC_ERROR("Missing or invalid [obfuscation][poisson_lambda] in client config file: {}",
                 configFilePath.string());
        return std::nullopt;
    }

    // Contacts File Path
    if (auto opt = table.at_path("storage.contacts_file_path").value<std::string>()) {
        config.storageConfig.constactsFilePath = std::move(*opt);
    } else {
        BC_ERROR("Missing or invalid [storage][contacts_file_path] in client config file: {}",
                 configFilePath.string());
        return std::nullopt;
    }

    return config;
}

} // namespace bc::domain::client
