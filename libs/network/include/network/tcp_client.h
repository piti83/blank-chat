#ifndef BC_LIBS_NETWORK_INCLUDE_TCPCLIENT_H_
#define BC_LIBS_NETWORK_INCLUDE_TCPCLIENT_H_

#include <cstdint>
#include <functional>
#include <string>

#include <boost/asio.hpp>

#include <protocol/frame.h>
#include <protocol/frame_parser.h>

namespace bc::network {

static constexpr size_t defaultCbrInterval = 5000;

using IOContext = boost::asio::io_context;
using Socket = boost::asio::ip::tcp::socket;

class TcpClient
{
public:
    using FrameProvider = std::function<bc::protocol::Frame()>;
    using FrameReceiver = std::function<void(bc::protocol::Frame&&)>;

    static constexpr std::string_view defaultTorHost = "127.0.0.1";
    static constexpr uint16_t defaultTorPort = 9050;

    explicit TcpClient(IOContext& ioContext, std::string_view torHost = defaultTorHost,
                       std::uint16_t torPort = defaultTorPort);
    ~TcpClient() noexcept;

    TcpClient(const TcpClient&) = delete;
    auto operator=(const TcpClient&) -> TcpClient& = delete;

    TcpClient(TcpClient&&) noexcept = default;
    auto operator=(TcpClient&&) noexcept -> TcpClient& = default;

    [[nodiscard]] auto Connect(std::string_view onionAddress, std::uint16_t destPort) -> bool;
    auto Disconnect() noexcept -> void;

    auto StartAsyncEngine(FrameProvider provider, FrameReceiver receiver,
                          std::chrono::milliseconds cbrInterval) -> void;

private:
    auto DoCbrTick() -> void;
    auto DoRead() -> void;

    Socket socket;
    bc::protocol::FrameParser parser;
    std::string torHost;
    std::uint16_t torPort;

    boost::asio::steady_timer cbrTimer;
    std::chrono::milliseconds cbrInterval{defaultCbrInterval};

    FrameProvider frameProvider;
    FrameReceiver frameReceiver;

    bc::protocol::RawFrame cbrWriteBuffer;

    static constexpr std::size_t readBufferSize = 4096;
    std::vector<std::uint8_t> readBuffer;
};

} // namespace bc::network

#endif // BC_LIBS_NETWORK_INCLUDE_TCPCLIENT_H_
