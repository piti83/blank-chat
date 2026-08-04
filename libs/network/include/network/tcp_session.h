#ifndef BC_LIBS_NETWORK_INCLUDE_TCPSESSION_H_
#define BC_LIBS_NETWORK_INCLUDE_TCPSESSION_H_

#include <cstdint>
#include <memory>
#include <queue>
#include <vector>

#include <boost/asio.hpp>

#include <network/network_types.h>
#include <protocol/frame_parser.h>
#include <protocol/i_frame_handler.h>

namespace bc::network {

enum class SessionState : std::uint8_t { UNAUTHENTICATED, AUTHENTICATED };

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
    auto HandleAuthResponse(const bc::protocol::Frame& frame) -> bool;

    TcpSocket socket;
    bc::protocol::IFrameHandler& handler;
    protocol::FrameParser parser;
    NetworkBufferType readBuffer{};

    std::queue<bc::protocol::RawFrame> writeQueue;
    bool writeInProgress{false};

    SessionState state{SessionState::UNAUTHENTICATED};
    std::vector<std::uint8_t> currentChallenge;
};

} // namespace bc::network

#endif // BC_LIBS_NETWORK_INCLUDE_TCPSESSION_H_
