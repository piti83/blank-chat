#ifndef BC_LIBS_CLI_INCLUDE_REPL_H_
#define BC_LIBS_CLI_INCLUDE_REPL_H_

#include <mutex>
#include <queue>
#include <string_view>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include <client/address_book.h>
#include <client/conversation_cache.h>
#include <crypto/identity_key.h>
#include <network/tcp_client.h>

namespace bc::cli {

class Repl
{
public:
    explicit Repl(bc::domain::client::AddressBook& addressBook,
                  bc::domain::client::ConversationCache& cache,
                  const bc::crypto::IdentityKey& identity, std::string_view torHost,
                  std::uint16_t torPort, std::string relayAddress, std::uint16_t relayPort);

    ~Repl();

    Repl(const Repl&) = delete;
    auto operator=(const Repl&) -> Repl& = delete;
    Repl(Repl&&) = delete;
    auto operator=(Repl&&) -> Repl& = delete;

    auto Run() -> void;

private:
    auto HandleConnect() -> void;
    auto HandleSend() -> void;
    auto HandleHistory() -> void;
    auto HandleList() -> void;

    auto HandleMyKey() -> void;
    auto HandleAddContact() -> void;

    auto GetNextFrameForCBR() -> bc::protocol::Frame;
    auto OnFrameReceived(bc::protocol::Frame&& frame) -> void;
    auto PrintThreadSafe(std::string_view msg) -> void;

    std::thread asioThread;

    std::mutex outboxMutex;
    std::queue<bc::protocol::Frame> outbox;

    std::mutex stdoutMutex;

    std::vector<std::string> contactAliases;
    std::size_t currentPollIndex{0};

    boost::asio::io_context ioContext;
    bc::network::TcpClient client;
    bc::domain::client::AddressBook& addressBook;
    bc::domain::client::ConversationCache& cache;
    const bc::crypto::IdentityKey& identity;

    std::string relayAddress;
    std::uint16_t relayPort;
};

} // namespace bc::cli

#endif // BC_LIBS_CLI_INCLUDE_REPL_H_
