#include <memory>
#include <string>
#include <utility>

#include <core/logger.h>
#include <network/tcp_session.h>

#include "network/tcp_server.h"

namespace bc::network {

TcpServer::TcpServer(boost::asio::io_context& ioContext, std::uint16_t port,
                     bc::protocol::IFrameHandler& handler)
    : acceptor(ioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      handler(handler)
{
}

auto TcpServer::Start() -> void
{
    BC_INFO("Starting TCP Server on port {}", acceptor.local_endpoint().port());
    DoAccept();
}

auto TcpServer::DoAccept() -> void
{
    acceptor.async_accept(
        [this](boost::system::error_code errorCode, boost::asio::ip::tcp::socket socket) -> void {
            HandleAccept(errorCode, std::move(socket));
        });
}

auto TcpServer::HandleAccept(boost::system::error_code errorCode,
                             boost::asio::ip::tcp::socket socket) -> void
{
    if (errorCode == boost::asio::error::operation_aborted ||
        errorCode == boost::asio::error::bad_descriptor) {
        return;
    }

    if (errorCode) {
        BC_WARN("Error accepting connection: {}", errorCode.message());
    } else {
        InitializeSession(std::move(socket));
    }

    if (acceptor.is_open()) {
        DoAccept();
    }
}

auto TcpServer::InitializeSession(boost::asio::ip::tcp::socket socket) -> void
{
    boost::system::error_code endpointEc;
    [[maybe_unused]] auto endpoint = socket.remote_endpoint(endpointEc);

    BC_INFO("Accepted new connection from {}",
            endpointEc ? std::string("UNKNOWN") : endpoint.address().to_string());

    auto session = std::make_shared<TcpSession>(std::move(socket), handler);
    session->Start();
}

} // namespace bc::network
