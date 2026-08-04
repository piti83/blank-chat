#ifndef BC_LIBS_NETWORK_INCLUDE_NETWORKTYPES_H_
#define BC_LIBS_NETWORK_INCLUDE_NETWORKTYPES_H_

#include <cstdint>
#include <string_view>

#include <boost/asio.hpp>

#include <protocol/frame.h>

namespace bc::network {

static constexpr std::string_view defaultTorHost = "127.0.0.1";
static constexpr std::uint16_t defaultTorPort = 9050;
static constexpr std::size_t readBufferSize = 4096;
static constexpr std::size_t networkBufferSize = 8192;

static constexpr std::uint32_t defaultCbrIntervalMs = 5000;
static constexpr float defaultPoissonLambda = 5.0F;

static constexpr std::uint8_t socks5Version = 0x05;
static constexpr std::uint8_t socks5AuthMethodsCount = 0x01;
static constexpr std::uint8_t socks5AuthNone = 0x00;

static constexpr std::size_t maxDomainLength = 255;

static constexpr std::size_t reqBufferMaxSize = 262;
static constexpr std::uint8_t socks5CmdConnect = 0x01;
static constexpr std::uint8_t socks5Reserved = 0x00;
static constexpr std::uint8_t socks5AtypDomain = 0x03;

static constexpr std::size_t domainOffset = 5;
static constexpr std::size_t portByteSize = 2;

static constexpr std::uint8_t byteShift = 8;
static constexpr std::uint16_t byteMask = 0xFF;
static constexpr std::size_t respHeaderSize = 4;
static constexpr std::uint8_t socks5RepSuccess = 0x00;

static constexpr std::uint8_t socks5AtypIpv4 = 0x01;
static constexpr std::uint8_t socks5AtypIpv6 = 0x04;
static constexpr std::size_t ipv4AddrPortSize = 6;
static constexpr std::size_t ipv6AddrPortSize = 18;

static constexpr std::size_t dropBufferSize = 256;

static constexpr std::size_t prefixLength = 14;

static constexpr std::size_t challengSize = 32;

static constexpr std::size_t statusBufferSize = 2048;
static constexpr std::size_t vmLckPrefixLength = 6;
static constexpr std::uint64_t bytesInKb = 1024;
static constexpr std::uint64_t percentMax = 100;
static constexpr std::uint64_t defaultFallbackRam = 1073741824ULL;

using IOContext = boost::asio::io_context;
using Socket = boost::asio::ip::tcp::socket;

using FrameProvider = std::function<bc::protocol::Frame()>;
using FrameReceiver = std::function<void(bc::protocol::Frame&&)>;

using TcpAcceptor = boost::asio::ip::tcp::acceptor;
using Socket = boost::asio::ip::tcp::socket;
using IOContext = boost::asio::io_context;
using Endpoint = boost::asio::ip::tcp::endpoint;

using TcpSocket = boost::asio::ip::tcp::socket;
using ErrorCode = boost::system::error_code;

using NetworkBufferType = std::array<std::uint8_t, networkBufferSize>;

} // namespace bc::network

#endif // BC_LIBS_NETWORK_INCLUDE_NETWORKTYPES_H_
