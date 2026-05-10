#ifndef BC_LIBS_DOMAIN_SERVER_INCLUDE_MESSAGEBROKER_H_
#define BC_LIBS_DOMAIN_SERVER_INCLUDE_MESSAGEBROKER_H_

#include <optional>
#include <queue>
#include <unordered_map>

#include <protocol/frame.h>
#include <protocol/mailbox_id_hash.h>

namespace bc::domain::server {

class MessageBroker
{
public:
    MessageBroker() = default;
    ~MessageBroker() = default;

    MessageBroker(const MessageBroker&) = delete;
    auto operator=(const MessageBroker&) -> MessageBroker& = delete;

    MessageBroker(MessageBroker&&) noexcept = default;
    auto operator=(MessageBroker&&) noexcept -> MessageBroker& = default;

    auto ProcessPush(bc::protocol::Frame&& frame) -> void;
    [[nodiscard]] auto ProcessPoll(const bc::protocol::MailboxID& mid)
        -> std::optional<bc::protocol::Frame>;

private:
    std::unordered_map<bc::protocol::MailboxID, std::queue<bc::protocol::Payload>> queues;
};

} // namespace bc::domain::server

#endif // BC_LIBS_DOMAIN_SERVER_INCLUDE_MESSAGEBROKER_H_
