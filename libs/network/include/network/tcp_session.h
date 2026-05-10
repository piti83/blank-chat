#ifndef BC_LIBS_NETWORK_INCLUDE_TCPSESSION_H_
#define BC_LIBS_NETWORK_INCLUDE_TCPSESSION_H_

#include <array>
#include <cstdint>
#include <memory>

#include <boost/asio.hpp>

#include <protocol/frame_parser.h>
#include <protocol/i_frame_handler.h>

namespace bc::network {

using TcpSocket = boost::asio::ip::tcp::socket;
using ErrorCode = boost::system::error_code;

class TcpSession : public std::enable_shared_from_this<TcpSession>
{
public:
    TcpSession(TcpSocket socket, bc::protocol::IFrameHandler& handler);
    ~TcpSession() = default;

    TcpSession(const TcpSession&) = delete;
    auto operator=(const TcpSession&) -> TcpSession& = delete;

    TcpSession(TcpSession&&) = delete;
    auto operator=(TcpSession&&) -> TcpSession& = delete;

    auto Start() -> void;

private:
    auto DoRead() -> void;
    auto DoWrite(bc::protocol::RawFrame frameData) -> void;
    auto ProcessExtractedFrame() -> void;

    static constexpr std::size_t bufferSize = 8192;
    using BufferType = std::array<std::uint8_t, bufferSize>;

    TcpSocket socket;
    bc::protocol::IFrameHandler& handler;
    protocol::FrameParser parser;
    BufferType readBuffer{};
};

} // namespace bc::network

#endif // BC_LIBS_NETWORK_INCLUDE_TCPSESSION_H_
