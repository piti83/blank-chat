#include "server/config.h"

#include <filesystem>
#include <optional>

#include <core/logger.h>

#include <toml++/toml.hpp>

namespace bc::domain::server {

auto LoadNetworkConfig(const toml::table& table, ServerConfig& config,
                       [[maybe_unused]] const std::filesystem::path& path) -> bool
{
    // Listen host
    if (auto opt = table.at_path("network.listen_host").value<std::string>()) {
        config.networkConfig.listenHost = std::move(*opt);
    } else {
        BC_ERROR("Missing or invalid [network][listen_host] in server config file: {}",
                 path.string());
        return false;
    }

    // Listen port
    if (auto opt = table.at_path("network.listen_port").value<std::uint16_t>()) {
        config.networkConfig.listenPort = *opt;
    } else {
        BC_ERROR("Missing or invalid [network][listen_port] in server config file: {}",
                 path.string());
        return false;
    }

    // Tor Control Host
    if (auto opt = table.at_path("network.tor_control_host").value<std::string>()) {
        config.networkConfig.torControlHost = std::move(*opt);
    } else {
        BC_ERROR("Missing or invalid [network][tor_control_host] in server config file: {}",
                 path.string());
        return false;
    }

    // Tor Control Port
    if (auto opt = table.at_path("network.tor_control_port").value<std::uint16_t>()) {
        config.networkConfig.torControlPort = *opt;
    } else {
        BC_ERROR("Missing or invalid [network][tor_control_port] in server config file: {}",
                 path.string());
        return false;
    }

    return true;
}

auto LoadSecurityConfig(const toml::table& table, ServerConfig& config,
                        [[maybe_unused]] const std::filesystem::path& path) -> bool
{
    // Memory Quota Percent
    if (auto opt = table.at_path("security.memory_quota_percent").value<std::uint8_t>()) {
        config.securityConfig.memoryQuotaPercent = *opt;
    } else {
        BC_ERROR("Missing or invalid [security][memory_quota_percent] in server config file: {}",
                 path.string());
        return false;
    }

    // Max Message Per Mailbox
    if (auto opt = table.at_path("security.max_messages_per_mailbox").value<std::uint32_t>()) {
        config.securityConfig.maxMessagesPerMailbox = *opt;
    } else {
        BC_ERROR(
            "Missing or invalid [security][max_messages_per_mailbox] in server config file: {}",
            path.string());
        return false;
    }

    return true;
}

auto LoadConfig(const std::filesystem::path& configFilePath) -> std::optional<ServerConfig>
{
    toml::parse_result result = toml::parse_file(configFilePath.string());

    if (!result) {
        BC_ERROR("Failed to parse config file: {}", result.error().description());
        return std::nullopt;
    }

    const toml::table& table = result.table();
    ServerConfig config;

    if (!LoadNetworkConfig(table, config, configFilePath) ||
        !LoadSecurityConfig(table, config, configFilePath)) {
        return std::nullopt;
    }

    return config;
}

} // namespace bc::domain::server
