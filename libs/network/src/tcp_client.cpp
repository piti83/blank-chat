#include "network/tcp_client.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <utility>
#include <vector>

#include <sodium.h>

#include <core/logger.h>

namespace bc::network {

namespace {

[[nodiscard]] auto PerformTorHandshake(boost::asio::ip::tcp::socket& socket,
                                       std::string_view onionAddress, std::uint16_t destPort)
    -> bool
{
    boost::system::error_code errorCode;

    std::array<std::uint8_t, 3> greeting = {socks5Version, socks5AuthMethodsCount, socks5AuthNone};
    boost::asio::write(socket, boost::asio::buffer(greeting), errorCode);
    if (errorCode) {
        return false;
    }

    std::array<std::uint8_t, 2> greetingResponse{};
    boost::asio::read(socket, boost::asio::buffer(greetingResponse), errorCode);
    if (errorCode || greetingResponse.at(0) != socks5Version ||
        greetingResponse.at(1) != socks5AuthNone) {
        BC_ERROR("SOCKS5 greeting failed. Is local Tor daemon running and accepting NoAuth?");
        return false;
    }

    if (onionAddress.length() > maxDomainLength) {
        BC_ERROR("Onion address is too long for SOCKS5h protocol.");
        return false;
    }

    std::array<std::uint8_t, reqBufferMaxSize> connectReq{};
    connectReq.at(0) = socks5Version;
    connectReq.at(1) = socks5CmdConnect;
    connectReq.at(2) = socks5Reserved;
    connectReq.at(3) = socks5AtypDomain;
    connectReq.at(4) = static_cast<std::uint8_t>(onionAddress.length());

    std::ranges::copy(onionAddress, std::next(connectReq.begin(), domainOffset));

    const std::size_t portOffset = domainOffset + onionAddress.length();

    connectReq.at(portOffset) = static_cast<std::uint8_t>((destPort >> byteShift) & byteMask);
    connectReq.at(portOffset + 1) = static_cast<std::uint8_t>(destPort & byteMask);

    boost::asio::write(socket, boost::asio::buffer(connectReq.data(), portOffset + portByteSize),
                       errorCode);
    if (errorCode) {
        return false;
    }

    std::array<std::uint8_t, respHeaderSize> connectRespHeader{};
    boost::asio::read(socket, boost::asio::buffer(connectRespHeader), errorCode);
    if (errorCode || connectRespHeader.at(0) != socks5Version) {
        BC_ERROR("Invalid SOCKS5 response header.");
        return false;
    }

    if (connectRespHeader.at(1) != socks5RepSuccess) {
        BC_WARN("Tor failed to build circuit. SOCKS5 REP code: 0x{:02x}", connectRespHeader.at(1));
        return false;
    }

    auto atyp = connectRespHeader.at(3);
    std::size_t remainingBytes = 0;

    if (atyp == socks5AtypIpv4) {
        remainingBytes = ipv4AddrPortSize;
    } else if (atyp == socks5AtypDomain) {
        std::array<std::uint8_t, 1> domainLen{};
        boost::asio::read(socket, boost::asio::buffer(domainLen), errorCode);
        if (errorCode) {
            return false;
        }
        remainingBytes = domainLen.at(0) + portByteSize;
    } else if (atyp == socks5AtypIpv6) {
        remainingBytes = ipv6AddrPortSize;
    } else {
        return false;
    }

    std::array<std::uint8_t, dropBufferSize> dropBuffer{};
    boost::asio::read(socket, boost::asio::buffer(dropBuffer.data(), remainingBytes), errorCode);

    return !static_cast<bool>(errorCode);
}

} // namespace

TcpClient::TcpClient(boost::asio::io_context& ioContext, std::string_view torHost,
                     std::uint16_t torPort)
    : socket(ioContext), torHost(torHost), torPort(torPort), cbrTimer(ioContext),
      readBuffer(readBufferSize)
{
}

auto TcpClient::Connect(std::string_view onionAddress, std::uint16_t destPort) -> bool
{
    boost::asio::ip::tcp::resolver resolver(socket.get_executor());

    auto endpoints = resolver.resolve(torHost, std::to_string(torPort));

    boost::system::error_code errorCode;
    boost::asio::connect(socket, endpoints, errorCode);

    if (errorCode) {
        BC_WARN("Failed to connect to local Tor proxy at {}:{}. Error: {}", torHost, torPort,
                errorCode.message());
        return false;
    }

    BC_INFO("Connected to Tor proxy. Negotiating SOCKS5h handshake for {}...", onionAddress);

    if (!PerformTorHandshake(socket, onionAddress, destPort)) {
        BC_ERROR("Tor Handshake failed. Dropping connection.");
        Disconnect();
        return false;
    }

    BC_INFO("Successfully built Tor circuit to {} on port {}", onionAddress, destPort);
    return true;
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

auto TcpClient::StartAsyncEngine(FrameProvider provider, FrameReceiver receiver,
                                 std::chrono::milliseconds interval) -> void
{
    frameProvider = std::move(provider);
    frameReceiver = std::move(receiver);
    cbrInterval = interval;

    DoRead();

    cbrTimer.expires_after(cbrInterval);
    cbrTimer.async_wait([this](boost::system::error_code ec) -> void {
        if (!ec)
            DoCbrTick();
    });
}

TcpClient::~TcpClient() noexcept
{
    Disconnect();
}

auto TcpClient::DoCbrTick() -> void
{
    cbrTimer.expires_at(cbrTimer.expiry() + cbrInterval);
    cbrTimer.async_wait([this](boost::system::error_code ec) -> void {
        if (!ec)
            DoCbrTick();
    });

    auto frame = frameProvider();
    writeQueue.push(frame.Serialize());

    DoWrite();
}

auto TcpClient::DoRead() -> void
{
    socket.async_read_some(
        boost::asio::buffer(readBuffer),
        [this](boost::system::error_code ec, std::size_t bytesTransferred) -> void {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    BC_WARN("Network read error: {}", ec.message());
                }
                return;
            }

            parser.FeedBytes(std::span<const std::uint8_t>(readBuffer.data(), bytesTransferred));

            if (parser.HasError()) {
                BC_ERROR("Parser error in TcpClient. Dropping connection.");
                Disconnect();
                return;
            }

            while (auto frameOpt = parser.TryExtractFrame()) {
                if (frameReceiver) {
                    frameReceiver(std::move(*frameOpt));
                }
            }

            if (socket.is_open()) {
                DoRead();
            }
        });
}

auto TcpClient::DoWrite() -> void
{
    if (writeInProgress || writeQueue.empty()) {
        return;
    }

    writeInProgress = true;

    boost::asio::async_write(socket, boost::asio::buffer(writeQueue.front()),
                             [this](boost::system::error_code ec, std::size_t /*length*/) -> void {
                                 writeInProgress = false;

                                 auto& buffer = writeQueue.front();
                                 sodium_memzero(buffer.data(), buffer.size());
                                 writeQueue.pop();

                                 if (ec) {
                                     BC_WARN("Network write error: {}. Socket might be dead.",
                                             ec.message());
                                     return;
                                 }

                                 DoWrite();
                             });
}

} // namespace bc::network
