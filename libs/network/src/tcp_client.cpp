#include <exception>
#include <utility>
#include <vector>

#include <core/logger.h>

#include "network/tcp_client.h"

namespace bc::network {

TcpClient::TcpClient(boost::asio::io_context& ioContext) : socket(ioContext)
{
}

TcpClient::~TcpClient() noexcept
{
    Disconnect();
}

auto TcpClient::Connect(const std::string& host, std::uint16_t port) -> bool
{
    try {
        boost::asio::ip::tcp::resolver resolver(socket.get_executor());
        auto endpoints = resolver.resolve(host, std::to_string(port));

        boost::system::error_code errorCode;
        boost::asio::connect(socket, endpoints, errorCode);

        if (errorCode) {
            BC_WARN("Failed to connect to {}:{}. Error: {}", host, port, errorCode.message());
            return false;
        }

        BC_INFO("Successfully connected to {}:{}", host, port);
        return true;
    } catch (const std::exception& e) {
        BC_ERROR("Exception during connection: {}", e.what());
        return false;
    }
}

auto TcpClient::Disconnect() noexcept -> void
{
    if (socket.is_open()) {
        boost::system::error_code errorCode;
        // NOLINTNEXTLINE(cert-err33-c, bugprone-unused-return-value)
        socket.close(errorCode);

        if (errorCode) {
            BC_TRACE("Error closing socket: {}", errorCode.message());
        }
    }
}

auto TcpClient::SendFrame(bc::protocol::Frame&& frame) -> bool
{
    bc::protocol::Frame localFrame = std::move(frame);

    auto serialized = localFrame.Serialize();
    boost::system::error_code errorCode;

    boost::asio::write(socket, boost::asio::buffer(serialized), errorCode);

    if (errorCode) {
        BC_WARN("Failed to send frame: {}", errorCode.message());
        return false;
    }

    BC_TRACE("Frame sent successfully (Size: {} bytes)", serialized.size());
    return true;
}

auto TcpClient::ReceiveFrame() -> std::optional<bc::protocol::Frame>
{
    static constexpr std::size_t bufferSize = 4096;
    std::vector<std::uint8_t> buffer(bufferSize);
    boost::system::error_code errorCode;

    while (true) {
        std::size_t bytesRead = socket.read_some(boost::asio::buffer(buffer), errorCode);

        if (errorCode == boost::asio::error::eof) {
            BC_WARN("Server closed connection (EOF).");
            return std::nullopt;
        }

        if (errorCode) {
            BC_WARN("Network read error: {}", errorCode.message());
            return std::nullopt;
        }

        parser.FeedBytes(std::span<const std::uint8_t>(buffer.data(), bytesRead));

        if (parser.HasError()) {
            BC_ERROR("Frame parser error. Data stream might be corrupted or malformed.");
            return std::nullopt;
        }

        if (auto frameOpt = parser.TryExtractFrame()) {
            return frameOpt;
        }
    }
}

} // namespace bc::network
