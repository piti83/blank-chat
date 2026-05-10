#include <cstddef>
#include <utility>

#include <boost/asio.hpp>

#include <core/logger.h>
#include <protocol/action_type.h>

#include "network/tcp_session.h"

namespace bc::network {

TcpSession::TcpSession(TcpSocket socket, bc::protocol::IFrameHandler& handler)
    : socket(std::move(socket)), handler(handler)
{
}

auto TcpSession::Start() -> void
{
    DoRead();
}

auto TcpSession::DoRead() -> void
{
    auto self(shared_from_this());

    socket.async_read_some(
        boost::asio::buffer(readBuffer),
        [this, self](ErrorCode errorCode, std::size_t bytesTransferred) -> void {
            if (errorCode) {
                return;
            }

            std::span<const std::uint8_t> dataSpan(readBuffer.data(), bytesTransferred);

            parser.FeedBytes(dataSpan);

            if (parser.HasError()) {
                BC_WARN("Network parser error (potential malformed frame). Dropping connection.");

                ErrorCode closeEc;
                socket.close(closeEc);

                if (closeEc) {
                    BC_TRACE("Socket close error after parser error: {}", closeEc.message());
                }
                return;
            }

            ProcessExtractedFrame();

            if (socket.is_open()) {
                DoRead();
            }
        });
}

auto TcpSession::ProcessExtractedFrame() -> void
{
    while (auto frameOpt = parser.TryExtractFrame()) {
        auto& frame = *frameOpt;

        if (frame.GetActionType() == bc::protocol::ActionType::PUSH) {
            BC_TRACE("Received PUSH frame, injecting into handler.");
            handler.ProcessPush(std::move(frame));
        } else if (frame.GetActionType() == bc::protocol::ActionType::POLL) {
            BC_TRACE("Received POLL frame, checking handler for messages.");
            auto responseOpt = handler.ProcessPoll(frame.GetMailboxID());

            if (responseOpt.has_value()) {
                BC_TRACE("Message found for POLL, sending response.");
                DoWrite(std::move(*responseOpt).Serialize());
            }
        }
    }
}

auto TcpSession::DoWrite(bc::protocol::RawFrame frameData) -> void
{
    auto bufferPtr = std::make_shared<bc::protocol::RawFrame>(std::move(frameData));

    auto self(shared_from_this());

    boost::asio::async_write(
        socket, boost::asio::buffer(*bufferPtr),
        [this, self, bufferPtr](ErrorCode errorCode, std::size_t /*length*/) -> void {
            if (errorCode) {
                BC_WARN("Error writing to socket. Dropping connection.");
                ErrorCode ignoredEc;
                // NOLINTNEXTLINE(cert-err33-c, bugprone-unused-return-value)
                socket.close(ignoredEc);
            }
        });
}

} // namespace bc::network
