#ifndef BC_LIBS_NETWORK_INCLUDE_TCPSESSION_H_
#define BC_LIBS_NETWORK_INCLUDE_TCPSESSION_H_

#include <memory>
#include <queue>

#include <boost/asio.hpp>

#include <network/network_types.h>
#include <protocol/frame_parser.h>
#include <protocol/i_frame_handler.h>

namespace bc::network {

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
    auto ProcessWriteQueue() -> void;

    auto ProcessExtractedFrame() -> void;

    TcpSocket socket;
    bc::protocol::IFrameHandler& handler;
    protocol::FrameParser parser;
    NetworkBufferType readBuffer{};

    std::queue<bc::protocol::RawFrame> writeQueue;
    bool writeInProgress{false};
};

} // namespace bc::network

#endif // BC_LIBS_NETWORK_INCLUDE_TCPSESSION_H_
