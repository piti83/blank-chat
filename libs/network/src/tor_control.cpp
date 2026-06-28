#include <istream>

#include <core/logger.h>

#include "network/tor_control.h"

namespace bc::network {

constexpr std::size_t prefixLength = 14;

auto TorControl::CreateEphemeralHiddenService(boost::asio::io_context& ioContext,
                                              std::string_view controlHost,
                                              std::uint16_t controlPort,
                                              std::uint16_t localListenPort)
    -> std::optional<std::string>
{
    boost::system::error_code ec;
    boost::asio::ip::tcp::socket controlSocket(ioContext);
    boost::asio::ip::tcp::resolver resolver(ioContext);

    auto endpoints = resolver.resolve(controlHost, std::to_string(controlPort), ec);
    if (ec) {
        BC_ERROR("Failed to resolve Tor Control port: {}", ec.message());
        return std::nullopt;
    }

    boost::asio::connect(controlSocket, endpoints, ec);
    if (ec) {
        BC_ERROR("Failed to connect to Tor Control port: {}", ec.message());
        return std::nullopt;
    }

    boost::asio::streambuf response;

    std::string_view authCmd = "AUTHENTICATE \"\"\r\n";
    boost::asio::write(controlSocket, boost::asio::buffer(authCmd), ec);

    std::size_t bytesRead = boost::asio::read_until(controlSocket, response, "\r\n", ec);
    if (ec) {
        BC_ERROR("Failed to authenticate with Tor daemon: {}", ec.message());
        return std::nullopt;
    }
    response.consume(bytesRead);

    std::string command = "ADD_ONION NEW:ED25519-V3 Flags=DiscardPK,Detach Port=80,127.0.0.1:" +
                          std::to_string(localListenPort) + "\r\n";
    boost::asio::write(controlSocket, boost::asio::buffer(command), ec);

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

    // NOLINTNEXTLINE(cert-err33-c, bugprone-unused-return-value)
    controlSocket.close(ec);

    if (onionAddress.empty()) {
        BC_ERROR("Failed to extract .onion address from Tor's response.");
        return std::nullopt;
    }

    return onionAddress;
}

} // namespace bc::network
