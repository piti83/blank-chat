#ifndef BC_LIBS_DOMAIN_SERVER_INCLUDE_MESSAGEBROKER_H_
#define BC_LIBS_DOMAIN_SERVER_INCLUDE_MESSAGEBROKER_H_

#include <frame.h>
#include <mailbox_id_hash.h>
#include <optional>
#include <queue>
#include <unordered_map>

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

    auto ProcessPush(bc::protocol::frame::Frame&& frame) -> void;
    [[nodiscard]] auto ProcessPoll(const bc::protocol::frame::MailboxID& mid)
        -> std::optional<bc::protocol::frame::Frame>;

private:
    std::unordered_map<bc::protocol::frame::MailboxID, std::queue<bc::protocol::frame::Payload>>
        queues;
};

} // namespace bc::domain::server

#endif // BC_LIBS_DOMAIN_SERVER_INCLUDE_MESSAGEBROKER_H_
