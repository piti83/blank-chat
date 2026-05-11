#ifndef BC_LIBS_NETWORK_INCLUDE_TCPSERVER_H_
#define BC_LIBS_NETWORK_INCLUDE_TCPSERVER_H_

#include <cstdint>

#include <boost/asio.hpp>

#include <protocol/i_frame_handler.h>

namespace bc::network {

using TcpAcceptor = boost::asio::ip::tcp::acceptor;
using Socket = boost::asio::ip::tcp::socket;
using IOContext = boost::asio::io_context;
using Endpoint = boost::asio::ip::tcp::endpoint;

class TcpServer
{
public:
    TcpServer(IOContext& ioContext, std::uint16_t port, bc::protocol::IFrameHandler& handler);
    ~TcpServer() = default;

    TcpServer(const TcpServer&) = delete;
    auto operator=(const TcpServer&) -> TcpServer& = delete;

    TcpServer(TcpServer&&) = delete;
    auto operator=(TcpServer&&) -> TcpServer& = delete;

    auto Start() -> void;

private:
    auto DoAccept() -> void;
    auto HandleAccept(boost::system::error_code errorCode, Socket socket) -> void;
    auto InitializeSession(Socket socket) -> void;

    TcpAcceptor acceptor;
    bc::protocol::IFrameHandler& handler;
};

} // namespace bc::network

#endif // BC_LIBS_NETWORK_INCLUDE_TCPSERVER_H_
