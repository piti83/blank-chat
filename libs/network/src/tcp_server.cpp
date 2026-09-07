#include "network/tcp_server.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

#include <core/logger.h>
#include <network/tcp_session.h>

namespace bc::network {

namespace {

[[nodiscard]] auto MakeListenEndpoint(std::string_view host, std::uint16_t port) -> Endpoint
{
    ErrorCode errorCode;
    auto address = boost::asio::ip::make_address(host, errorCode);

    if (errorCode) {
        std::cerr << "Invalid TCP listen host '" << host << "': " << errorCode.message() << '\n';
        std::abort();
    }

    return {address, port};
}

} // namespace

TcpServer::TcpServer(IOContext& ioContext, std::string_view host, std::uint16_t port,
                     bc::protocol::IFrameHandler& handler, std::uint8_t memoryQuotaPercent)
    : acceptor(ioContext, MakeListenEndpoint(host, port)), handler(handler),
      memoryMonitor(ioContext, memoryQuotaPercent)
{
}

auto TcpServer::Start() -> void
{
    const auto endpoint = acceptor.local_endpoint();

    BC_INFO("Starting TCP Server on {}:{}", endpoint.address().to_string(), endpoint.port());

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

    if (memoryMonitor.IsQuotaExceeded()) {
        DoAccept();
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
