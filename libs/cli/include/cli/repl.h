#ifndef BC_LIBS_CLI_INCLUDE_REPL_H_
#define BC_LIBS_CLI_INCLUDE_REPL_H_

#include <mutex>
#include <queue>
#include <string_view>
#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include <client/address_book.h>
#include <client/config.h>
#include <client/conversation_cache.h>
#include <crypto/identity_key.h>
#include <network/tcp_client.h>

namespace bc::cli {

class Repl
{
public:
    Repl(bc::domain::client::AddressBook& addressBook, bc::domain::client::ConversationCache& cache,
         const bc::crypto::IdentityKey& identity,
         const bc::domain::client::ClientConfig& configParam);

    auto Run() -> void;

    Repl(const Repl&) = delete;
    auto operator=(const Repl&) -> Repl& = delete;
    Repl(Repl&&) = delete;
    auto operator=(Repl&&) -> Repl& = delete;

    ~Repl();

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

    auto HandleTextMessage(std::string_view alias, const domain::client::Contact* contact,
                           const std::vector<std::uint8_t>& plaintext,
                           std::span<const std::uint8_t> msgData) -> void;
    auto HandlePfsRotateRequest(std::string_view alias, domain::client::Contact* contact,
                                const std::vector<std::uint8_t>& plaintext) -> void;
    auto HandlePfsRotateAck(std::string_view alias, domain::client::Contact* contact,
                            const std::vector<std::uint8_t>& plaintext) -> void;

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

    const bc::domain::client::ClientConfig& config;
};

} // namespace bc::cli

#endif // BC_LIBS_CLI_INCLUDE_REPL_H_
