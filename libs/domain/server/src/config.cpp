#include <filesystem>
#include <optional>

#include <core/logger.h>

#include "server/config.h"
#include <toml++/toml.hpp>

namespace bc::domain::server {

[[nodiscard]] auto LoadConfig(const std::filesystem::path& configFilePath)
    -> std::optional<ServerConfig>
{
    toml::parse_result result = toml::parse_file(configFilePath.string());

    if (!result) {
        BC_ERROR("Failed to parse config file: {}", result.error().description());
        return std::nullopt;
    }

    const toml::table& table = result.table();
    ServerConfig config;

    // Listen host
    if (auto hasValue = table["network"]["listen_host"].value<std::string>()) {
        config.networkConfig.listenHost = std::move(*hasValue);
    } else {
        BC_ERROR("Missing or invalid [network][listen_host] in server config file: {}",
                 configFilePath.string());
        return std::nullopt;
    }

    // Listen port
    if (auto hasValue = table["network"]["listen_port"].value<std::uint16_t>()) {
        config.networkConfig.listenPort = *hasValue;
    } else {
        BC_ERROR("Missing or invalid [network][listen_port] in server config file: {}",
                 configFilePath.string());
        return std::nullopt;
    }

    // Tor Control Host
    if (auto hasValue = table["network"]["tor_control_host"].value<std::string>()) {
        config.networkConfig.torControlHost = std::move(*hasValue);
    } else {
        BC_ERROR("Missing or invalid [network][tor_control_host] in server config file: {}",
                 configFilePath.string());
        return std::nullopt;
    }

    // Tor Control Port
    if (auto hasValue = table["network"]["tor_control_port"].value<std::uint16_t>()) {
        config.networkConfig.torControlPort = *hasValue;
    } else {
        BC_ERROR("Missing or invalid [network][tor_control_port] in server config file: {}",
                 configFilePath.string());
        return std::nullopt;
    }

    // Memory Quota Percent
    if (auto hasValue = table["security"]["memory_quota_percent"].value<std::uint8_t>()) {
        config.securityConfig.memoryQuotaPercent = *hasValue;
    } else {
        BC_ERROR("Missing or invalid [security][memory_quota_percent] in server config file: {}",
                 configFilePath.string());
        return std::nullopt;
    }

    // Max Message Per Mailbox
    if (auto hasValue = table["security"]["max_messages_per_mailbox"].value<std::uint32_t>()) {
        config.securityConfig.maxMessagesPerMailbox = *hasValue;
    } else {
        BC_ERROR(
            "Missing or invalid [security][max_messages_per_mailbox] in server config file: {}",
            configFilePath.string());
        return std::nullopt;
    }

    return config;
}

} // namespace bc::domain::server
