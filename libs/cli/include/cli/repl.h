#ifndef BC_LIBS_CLI_INCLUDE_REPL_H_
#define BC_LIBS_CLI_INCLUDE_REPL_H_

#include <string_view>

#include <boost/asio.hpp>

#include <network/tcp_client.h>

namespace bc::cli {

class Repl
{
public:
    explicit Repl(std::string_view torHost = bc::network::TcpClient::defaultTorHost,
                  std::uint16_t torPort = bc::network::TcpClient::defaultTorPort);

    ~Repl() = default;

    Repl(const Repl&) = delete;
    auto operator=(const Repl&) -> Repl& = delete;
    Repl(Repl&&) = delete;
    auto operator=(Repl&&) -> Repl& = delete;

    auto Run() -> void;

private:
    auto HandleConnect() -> void;
    auto HandleSend() -> void;
    auto HandlePoll() -> void;

    boost::asio::io_context ioContext;
    bc::network::TcpClient client;
};

} // namespace bc::cli

#endif // BC_LIBS_CLI_INCLUDE_REPL_H_
