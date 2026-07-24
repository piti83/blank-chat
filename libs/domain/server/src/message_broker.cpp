#include "server/message_broker.h"

#include <protocol/frame.h>

namespace bc::domain::server {

auto MessageBroker::ProcessPush(bc::protocol::Frame&& frame) -> void
{
    auto mid = frame.GetMailboxID();
    queues[mid].push(std::move(frame));
}

auto MessageBroker::ProcessPoll(const bc::protocol::MailboxID& mid)
    -> std::optional<bc::protocol::Frame>
{
    auto iter = queues.find(mid);

    if (iter == queues.end() || iter->second.empty()) {
        return std::nullopt;
    }

    auto frame = std::move(iter->second.front());
    iter->second.pop();

    if (iter->second.empty()) {
        queues.erase(iter);
    }

    return frame;
}

} // namespace bc::domain::server
