#include <cstdint>
#include <exception>

#include <boost/asio.hpp>

#include <core/logger.h>
#include <network/tcp_server.h>
#include <server/message_broker.h>

auto main() -> int
{
    try {

        constexpr std::uint16_t defaultPort = 8080;

        bc::core::Logger::Init();

        BC_INFO("Starting Blank Chat Server...");

        boost::asio::io_context ioContext;
        bc::domain::server::MessageBroker messageBroker;

        std::uint16_t port = defaultPort;
        bc::network::TcpServer tcpServer(ioContext, port, messageBroker);

        tcpServer.Start();

        ioContext.run();

        return 0;
    } catch (const std::exception& e) {
        BC_CRITICAL("Fatal exception: {}", e.what());
        return 1;
    } catch (...) {
        BC_CRITICAL("Unknown fatal exception.");
        return 2;
    }
}
