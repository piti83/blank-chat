#include <filesystem>

#include <boost/asio.hpp>

#include <core/logger.h>
#include <network/tcp_server.h>
#include <server/config.h>
#include <server/message_broker.h>

auto main() -> int
{
    bc::core::Logger::Init();
    std::filesystem::path configPath = "/etc/blank-chat/client_config.toml";

    bc::domain::server::ServerConfig config;

    if (auto hasVal = bc::domain::server::LoadConfig(configPath)) {
        config = *hasVal;
    } else {
        BC_ERROR("Failed to parse server config file: {}", configPath.string());
        return 1;
    }

    BC_INFO("Starting Blank Chat Server...");

    boost::asio::io_context ioContext;
    bc::domain::server::MessageBroker messageBroker;

    // TODO take config as a parameter and utilize it in class.
    bc::network::TcpServer tcpServer(ioContext, config.networkConfig.listenPort, messageBroker);

    tcpServer.Start();

    ioContext.run();

    return 0;
}
