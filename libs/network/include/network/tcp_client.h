#ifndef BC_LIBS_NETWORK_INCLUDE_TCPCLIENT_H_
#define BC_LIBS_NETWORK_INCLUDE_TCPCLIENT_H_

#include <cstdint>
#include <queue>
#include <string>

#include <boost/asio.hpp>

#include <network/network_types.h>
#include <protocol/frame.h>
#include <protocol/frame_parser.h>

namespace bc::network {

class TcpClient
{
public:
    explicit TcpClient(IOContext& ioContext, std::string_view torHost = defaultTorHost,
                       std::uint16_t torPort = defaultTorPort);

    TcpClient(const TcpClient&) = delete;
    auto operator=(const TcpClient&) -> TcpClient& = delete;

    TcpClient(TcpClient&&) noexcept = default;
    auto operator=(TcpClient&&) noexcept -> TcpClient& = default;

    [[nodiscard]] auto Connect(std::string_view onionAddress, std::uint16_t destPort) -> bool;
    auto Disconnect() noexcept -> void;

    auto StartAsyncEngine(std::function<bc::protocol::Frame()> provider,
                          std::function<void(bc::protocol::Frame&&)> receiver,
                          std::function<std::chrono::milliseconds()> intervalProvider) -> void;

    ~TcpClient() noexcept;

private:
    auto DoCbrTick() -> void;
    auto DoRead() -> void;
    auto DoWrite() -> void;
    auto HandleAuthChallenge(const bc::protocol::Payload& challenge) -> void;

    Socket socket;
    bc::protocol::FrameParser parser;
    std::string torHost;
    std::uint16_t torPort;

    boost::asio::steady_timer cbrTimer;
    std::function<std::chrono::milliseconds()> intervalProvider;

    FrameProvider frameProvider;
    FrameReceiver frameReceiver;

    std::queue<bc::protocol::RawFrame> writeQueue;
    bool writeInProgress{false};

    std::vector<std::uint8_t> readBuffer;

    bool isAuthenticated{false};
};

} // namespace bc::network

#endif // BC_LIBS_NETWORK_INCLUDE_TCPCLIENT_H_
