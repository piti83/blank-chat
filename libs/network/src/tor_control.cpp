#include "network/tor_control.h"

#include <istream>

#include <core/logger.h>

namespace bc::network {

namespace {

constexpr std::size_t prefixLength = 14;

[[nodiscard]] auto ConnectToTorControl(boost::asio::io_context& ioContext,
                                       std::string_view controlHost, std::uint16_t controlPort)
    -> std::optional<boost::asio::ip::tcp::socket>
{
    boost::system::error_code ec;
    boost::asio::ip::tcp::resolver resolver(ioContext);

    auto endpoints = resolver.resolve(controlHost, std::to_string(controlPort), ec);
    if (ec) {
        BC_ERROR("Failed to resolve Tor Control port: {}", ec.message());
        return std::nullopt;
    }

    boost::asio::ip::tcp::socket controlSocket(ioContext);
    boost::asio::connect(controlSocket, endpoints, ec);
    if (ec) {
        BC_ERROR("Failed to connect to Tor Control port: {}", ec.message());
        return std::nullopt;
    }

    return controlSocket;
}

[[nodiscard]] auto Authenticate(boost::asio::ip::tcp::socket& controlSocket) -> bool
{
    boost::system::error_code ec;
    boost::asio::streambuf response;

    std::string_view authCmd = "AUTHENTICATE \"\"\r\n";
    boost::asio::write(controlSocket, boost::asio::buffer(authCmd), ec);
    if (ec) {
        BC_ERROR("Failed to send AUTHENTICATE command: {}", ec.message());
        return false;
    }

    std::size_t bytesRead = boost::asio::read_until(controlSocket, response, "\r\n", ec);
    if (ec) {
        BC_ERROR("Failed to authenticate with Tor daemon: {}", ec.message());
        return false;
    }
    response.consume(bytesRead);

    return true;
}

[[nodiscard]] auto ParseAddOnionResponse(boost::asio::ip::tcp::socket& controlSocket)
    -> std::optional<std::string>
{
    boost::system::error_code ec;
    boost::asio::streambuf response;
    std::string onionAddress;

    while (!ec) {
        boost::asio::read_until(controlSocket, response, "\r\n", ec);
        if (ec) {
            break;
        }

        std::istream responseStream(&response);
        std::string line;
        std::getline(responseStream, line);

        if (line.find("250-ServiceID=") != std::string::npos) {
            onionAddress = line.substr(prefixLength);
            if (!onionAddress.empty() && onionAddress.back() == '\r') {
                onionAddress.pop_back();
            }
        }

        if (line.starts_with("5")) {
            BC_ERROR("Tor rejected ADD_ONION with error: {}", line);
            break;
        }

        if (line.starts_with("250 OK")) {
            break;
        }
    }

    if (onionAddress.empty()) {
        BC_ERROR("Failed to extract .onion address from Tor's response.");
        return std::nullopt;
    }

    return onionAddress;
}

} // namespace

auto TorControl::CreateEphemeralHiddenService(boost::asio::io_context& ioContext,
                                              const HiddenServiceConfig& config)
    -> std::optional<std::string>
{
    auto controlSocketOpt = ConnectToTorControl(ioContext, config.controlHost, config.controlPort);
    if (!controlSocketOpt) {
        return std::nullopt;
    }

    auto& controlSocket = *controlSocketOpt;

    if (!Authenticate(controlSocket)) {
        return std::nullopt;
    }

    boost::system::error_code ec;
    std::string command = "ADD_ONION NEW:ED25519-V3 Flags=DiscardPK,Detach Port=80,127.0.0.1:" +
                          std::to_string(config.localListenPort) + "\r\n";
    boost::asio::write(controlSocket, boost::asio::buffer(command), ec);

    if (ec) {
        BC_ERROR("Failed to send ADD_ONION command: {}", ec.message());
        return std::nullopt;
    }

    auto onionAddressOpt = ParseAddOnionResponse(controlSocket);

    // NOLINTNEXTLINE(cert-err33-c, bugprone-unused-return-value)
    controlSocket.close(ec);

    return onionAddressOpt;
}

} // namespace bc::network
