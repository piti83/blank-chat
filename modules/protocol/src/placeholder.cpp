#include "protocol/placeholder.h"

#include <spdlog/spdlog.h>

namespace bc::protocol {

auto GetProtocolStatus() -> std::string
{
    spdlog::info("Module [Protocol] is parsing dummy packet...");
    return "Protocol OK";
}

} // namespace bc::protocol
