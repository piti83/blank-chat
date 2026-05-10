#ifndef BC_LIBS_PROTOCOL_INCLUDE_IFRAMEHANDLER_H_
#define BC_LIBS_PROTOCOL_INCLUDE_IFRAMEHANDLER_H_

#include <optional>

#include <protocol/frame.h>
#include <protocol/mailbox_id.h>

namespace bc::protocol {

class IFrameHandler
{
public:
    IFrameHandler() = default;
    virtual ~IFrameHandler() = default;

    IFrameHandler(const IFrameHandler&) = delete;
    auto operator=(const IFrameHandler&) -> IFrameHandler& = delete;
    IFrameHandler(IFrameHandler&&) = delete;
    auto operator=(IFrameHandler&&) -> IFrameHandler& = delete;

    virtual auto ProcessPush(Frame&& frame) -> void = 0;
    [[nodiscard]] virtual auto ProcessPoll(const MailboxID& mid) -> std::optional<Frame> = 0;
};

} // namespace bc::protocol

#endif // BC_LIBS_PROTOCOL_INCLUDE_IFRAMEHANDLER_H_
