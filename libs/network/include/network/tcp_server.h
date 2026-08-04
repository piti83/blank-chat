#ifndef BC_LIBS_NETWORK_INCLUDE_TCPSERVER_H_
#define BC_LIBS_NETWORK_INCLUDE_TCPSERVER_H_

#include <cstdint>

#include <boost/asio.hpp>

#include <network/memory_monitor.h>
#include <network/network_types.h>
#include <protocol/i_frame_handler.h>

namespace bc::network {

class TcpServer
{
public:
    TcpServer(IOContext& ioContext, std::uint16_t port, bc::protocol::IFrameHandler& handler,
              std::uint8_t memoryQuotaPercent);
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
    MemoryMonitor memoryMonitor;
};

} // namespace bc::network

#endif // BC_LIBS_NETWORK_INCLUDE_TCPSERVER_H_
