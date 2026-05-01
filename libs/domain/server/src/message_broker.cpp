#include "frame.h"

#include <message_broker.h>

namespace bc::domain::server {

auto MessageBroker::ProcessPush(bc::protocol::frame::Frame&& frame) -> void
{
    auto mid = frame.GetMailboxID();
    queues[mid].push(std::move(frame).ExtractPayload());
}

auto MessageBroker::ProcessPoll(const bc::protocol::frame::MailboxID& mid)
    -> std::optional<bc::protocol::frame::Frame>
{
    auto iter = queues.find(mid);

    if (iter == queues.end() || iter->second.empty()) {
        return std::nullopt;
    }

    auto payload = std::move(iter->second.front());
    iter->second.pop();

    if (iter->second.empty()) {
        queues.erase(iter);
    }

    return bc::protocol::frame::Frame::CreatePush(mid, std::move(payload));
}

} // namespace bc::domain::server
