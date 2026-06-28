#include "client/config.h"

#include <filesystem>
#include <optional>

#include <core/logger.h>

#include <toml++/toml.hpp>

namespace bc::domain::client {

auto LoadNetworkConfig(const toml::table& table, ClientConfig& config,
                       [[maybe_unused]] const std::filesystem::path& path) -> bool
{
    // Tor SOCKS Host
    if (auto opt = table.at_path("network.tor_socks_host").value<std::string>()) {
        config.networkConfig.torSocksHost = std::move(*opt);
    } else {
        BC_ERROR("Missing or invalid [network][tor_socks_host] in client config file: {}",
                 path.string());
        return false;
    }

    // Tor SOCKS Port
    if (auto opt = table.at_path("network.tor_socks_port").value<std::uint16_t>()) {
        config.networkConfig.torSocksPort = *opt;
    } else {
        BC_ERROR("Missing or invalid [network][tor_socks_port] in client config file: {}",
                 path.string());
        return false;
    }

    return true;
}

auto LoadRelayConfig(const toml::table& table, ClientConfig& config,
                     [[maybe_unused]] const std::filesystem::path& path) -> bool
{
    // Onion address
    if (auto opt = table.at_path("relay.onion_address").value<std::string>()) {
        config.relayConfig.onionAddress = std::move(*opt);
    } else {
        BC_ERROR("Missing [relay][onion_address] in client config.");
        return false;
    }

    // Onion port
    if (auto opt = table.at_path("relay.onion_port").value<std::uint16_t>()) {
        config.relayConfig.onionPort = *opt;
    } else {
        BC_ERROR("Missing [relay][onion_port] in client config.");
        return false;
    }

    return true;
}

auto LoadObfuscationConfig(const toml::table& table, ClientConfig& config,
                           [[maybe_unused]] const std::filesystem::path& path) -> bool
{
    // Mode
    if (auto opt = table.at_path("obfuscation.mode").value<std::string>()) {
        config.obfuscationConfig.mode = std::move(*opt);
    } else {
        BC_ERROR("Missing or invalid [obfuscation][mode] in client config file: {}", path.string());
        return false;
    }

    // CBR Interval MS
    if (auto opt = table.at_path("obfuscation.cbr_interval_ms").value<std::uint32_t>()) {
        config.obfuscationConfig.cbr_interval_ms = *opt;
    } else {
        BC_ERROR("Missing or invalid [obfuscation][cbr_interval_ms] in client config file: {}",
                 path.string());
        return false;
    }

    // Poisson Lambda
    if (auto opt = table.at_path("obfuscation.poisson_lambda").value<float>()) {
        config.obfuscationConfig.poissonLambda = *opt;
    } else {
        BC_ERROR("Missing or invalid [obfuscation][poisson_lambda] in client config file: {}",
                 path.string());
        return false;
    }
    return true;
}

auto LoadStorageConfig(const toml::table& table, ClientConfig& config,
                       [[maybe_unused]] const std::filesystem::path& path) -> bool
{
    // Contacts File Path
    if (auto opt = table.at_path("storage.contacts_file_path").value<std::string>()) {
        config.storageConfig.contactsFilePath = std::move(*opt);
    } else {
        BC_ERROR("Missing or invalid [storage][contacts_file_path] in client config file: {}",
                 path.string());
        return false;
    }
    return true;
}

auto LoadConfig(const std::filesystem::path& configFilePath) -> std::optional<ClientConfig>
{
    toml::parse_result result = toml::parse_file(configFilePath.string());

    if (!result) {
        BC_ERROR("Failed to parse client config file: {}", result.error().description());
        return std::nullopt;
    }

    const toml::table& table = result.table();
    ClientConfig config;

    if (!LoadNetworkConfig(table, config, configFilePath) ||
        !LoadRelayConfig(table, config, configFilePath) ||
        !LoadObfuscationConfig(table, config, configFilePath) ||
        !LoadStorageConfig(table, config, configFilePath)) {
        return std::nullopt;
    }

    return config;
}

} // namespace bc::domain::client
