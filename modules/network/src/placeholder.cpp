#include "network/placeholder.h"

#include <spdlog/spdlog.h>

namespace blank_chat::network {

auto GetNetworkStatus() -> std::string
{
    spdlog::info("Module [Network] is connecting to dummy socket...");
    return "Network OK";
}

} // namespace blank_chat::network
