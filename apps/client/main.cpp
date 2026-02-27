#include "crypto/placeholder.h"
#include "network/placeholder.h"
#include "protocol/placeholder.h"

#include <spdlog/spdlog.h>

auto main() -> int
{
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Starting BlankChat Terminal Client...");

    auto cryptoStatus = blank_chat::crypto::GetCryptoStatus();
    auto networkStatus = blank_chat::network::GetNetworkStatus();
    auto protocolStatus = blank_chat::protocol::GetProtocolStatus();

    spdlog::info("Client Boot Results: {}, {}, {}", cryptoStatus, networkStatus, protocolStatus);

    return 0;
}
