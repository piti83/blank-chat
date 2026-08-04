#include "network/tcp_session.h"

#include <cstddef>
#include <span>
#include <utility>

#include <boost/asio.hpp>
#include <sodium.h>

#include <core/logger.h>
#include <core/string_utils.h>
#include <network/network_types.h>
#include <protocol/protocol_types.h>

namespace bc::network {

TcpSession::TcpSession(TcpSocket socket, bc::protocol::IFrameHandler& handler)
    : socket(std::move(socket)), handler(handler)
{
}

auto TcpSession::Start() -> void
{
    currentChallenge.resize(challengSize);
    randombytes_buf(currentChallenge.data(), currentChallenge.size());

    bc::protocol::MailboxID dummyId;
    dummyId.Fill(0x00);
    auto challengeFrame = bc::protocol::Frame::CreateAuthChallenge(dummyId, currentChallenge);

    DoWrite(challengeFrame.Serialize());
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
                // NOLINTNEXTLINE(cert-err33-c, bugprone-unused-return-value)
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

auto TcpSession::DoWrite(bc::protocol::RawFrame frameData) -> void
{
    writeQueue.push(std::move(frameData));
    ProcessWriteQueue();
}

auto TcpSession::ProcessWriteQueue() -> void
{
    if (writeInProgress || writeQueue.empty()) {
        return;
    }

    writeInProgress = true;
    auto self(shared_from_this());

    boost::asio::async_write(socket, boost::asio::buffer(writeQueue.front()),
                             [this, self](ErrorCode errorCode, std::size_t /*length*/) -> void {
                                 writeInProgress = false;

                                 auto& buffer = writeQueue.front();
                                 sodium_memzero(buffer.data(), buffer.size());
                                 writeQueue.pop();

                                 if (errorCode) {
                                     BC_WARN("Error writing to socket. Dropping connection.");
                                     ErrorCode ignoredEc;
                                     auto er = socket.close(ignoredEc);
                                     if (er) {
                                         BC_WARN("Error closing socket: {}", er.what());
                                     }
                                     return;
                                 }

                                 ProcessWriteQueue();
                             });
}

auto TcpSession::ProcessExtractedFrame() -> void
{
    while (auto frameOpt = parser.TryExtractFrame()) {
        auto frame = std::move(*frameOpt);

        if (state == SessionState::UNAUTHENTICATED) {
            if (!HandleAuthResponse(frame)) {
                return;
            }
            continue;
        }

        if (frame.GetActionType() == bc::protocol::ActionType::PUSH ||
            frame.GetActionType() == bc::protocol::ActionType::ACK) {
            handler.ProcessPush(std::move(frame));
        } else if (frame.GetActionType() == bc::protocol::ActionType::POLL) {
            if (auto responseOpt = handler.ProcessPoll(frame.GetMailboxID())) {
                DoWrite(responseOpt->Serialize());
            } else {
                auto emptyPoll = bc::protocol::Frame::CreatePoll(frame.GetMailboxID());
                DoWrite(emptyPoll.Serialize());
            }
        }
    }
}

auto TcpSession::HandleAuthResponse(const bc::protocol::Frame& frame) -> bool
{
    if (frame.GetActionType() != bc::protocol::ActionType::AUTH_RESPONSE) {
        BC_WARN("Expected AUTH_RESPONSE, received different frame type. Dropping connection.");
        boost::system::error_code closeEc;
        [[maybe_unused]] auto retEc = socket.close(closeEc);
        if (closeEc) {
            BC_TRACE("Socket closed with underlying OS message: {}", closeEc.message());
        }
        return false;
    }

    auto payload = frame.GetPayload();
    std::uint64_t nonce = 0;
    if (payload.size() == sizeof(nonce)) {
        std::memcpy(&nonce, payload.data(), sizeof(nonce));
    }

    std::vector<std::uint8_t> combined = currentChallenge;
    combined.resize(currentChallenge.size() + sizeof(nonce));

    std::span<std::uint8_t> combinedSpan(combined);
    auto nonceDest = combinedSpan.subspan(currentChallenge.size(), sizeof(nonce));
    std::memcpy(nonceDest.data(), &nonce, sizeof(nonce));

    std::string hashHex = bc::core::HashPayload(combined);
    if (hashHex.starts_with("000")) {
        BC_INFO("Client successfully authenticated.");
        state = SessionState::AUTHENTICATED;
        return true;
    }

    BC_WARN("Invalid PoW. Dropping connection.");
    boost::system::error_code closeEc;
    [[maybe_unused]] auto retEc = socket.close(closeEc);
    if (closeEc) {
        BC_TRACE("Socket closed with underlying OS message: {}", closeEc.message());
    }
    return false;
}

} // namespace bc::network
