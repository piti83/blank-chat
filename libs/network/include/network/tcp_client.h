#ifndef BC_LIBS_NETWORK_INCLUDE_TCPCLIENT_H_
#define BC_LIBS_NETWORK_INCLUDE_TCPCLIENT_H_

#include <cstdint>
#include <optional>
#include <string>

#include <boost/asio.hpp>

#include <protocol/frame.h>
#include <protocol/frame_parser.h>

namespace bc::network {

using IOContext = boost::asio::io_context;
using Socket = boost::asio::ip::tcp::socket;

class TcpClient
{
public:
    explicit TcpClient(IOContext& ioContext);
    ~TcpClient() noexcept;

    TcpClient(const TcpClient&) = delete;
    auto operator=(const TcpClient&) -> TcpClient& = delete;

    TcpClient(TcpClient&&) noexcept = default;
    auto operator=(TcpClient&&) noexcept -> TcpClient& = default;

    [[nodiscard]] auto Connect(const std::string& host, std::uint16_t port) -> bool;
    auto Disconnect() noexcept -> void;

    auto SendFrame(bc::protocol::Frame&& frame) -> bool;

    [[nodiscard]] auto ReceiveFrame() -> std::optional<bc::protocol::Frame>;

private:
    Socket socket;
    bc::protocol::FrameParser parser;
};

} // namespace bc::network

#endif // BC_LIBS_NETWORK_INCLUDE_TCPCLIENT_H_
