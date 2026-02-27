#include "protocol/placeholder.h"

#include <spdlog/spdlog.h>

namespace blank_chat::protocol {

auto GetProtocolStatus() -> std::string
{
    spdlog::info("Module [Protocol] is parsing dummy packet...");
    return "Protocol OK";
}

} // namespace blank_chat::protocol
