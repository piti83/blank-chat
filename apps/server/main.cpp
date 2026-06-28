#include <filesystem>

#include <boost/asio.hpp>
#include <sodium.h>

#include <core/logger.h>
#include <network/tcp_server.h>
#include <network/tor_control.h>
#include <server/config.h>
#include <server/message_broker.h>

auto main() -> int
{
    if (sodium_init() < 0) {
        return 1;
    }

    bc::core::Logger::Init();
    std::filesystem::path configPath = "/etc/blank-chat/server_config.toml";

    bc::domain::server::ServerConfig config;

    if (auto hasVal = bc::domain::server::LoadConfig(configPath)) {
        config = *hasVal;
    } else {
        BC_ERROR("Failed to parse server config file: {}", configPath.string());
        return 1;
    }

    BC_INFO("Initializing Blank Chat Ephemeral RAM-Only Server...");

    boost::asio::io_context ioContext;

    auto onionAddressOpt = bc::network::TorControl::CreateEphemeralHiddenService(
        ioContext, config.networkConfig.torControlHost, config.networkConfig.torControlPort,
        config.networkConfig.listenPort);

    if (!onionAddressOpt) {
        BC_CRITICAL("Failed to mount Tor service. Ensure Tor is running.");
        return 1;
    }

    BC_INFO("Successfully mounted Ephemeral Hidden Service: {}.onion", *onionAddressOpt);
    BC_INFO("Distribute this address to your clients Out-Of-Band.");

    bc::domain::server::MessageBroker messageBroker;
    bc::network::TcpServer tcpServer(ioContext, config.networkConfig.listenPort, messageBroker);

    tcpServer.Start();
    ioContext.run();

    return 0;
}
