#include "network/placeholder.h"

#include <spdlog/spdlog.h>

namespace bc::network {

auto GetNetworkStatus() -> std::string
{
    spdlog::info("Module [Network] is connecting to dummy socket...");
    return "Network OK";
}

} // namespace bc::network
