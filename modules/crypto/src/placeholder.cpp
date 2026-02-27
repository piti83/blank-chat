#include "crypto/placeholder.h"

#include <spdlog/spdlog.h>

namespace blank_chat::crypto {

auto GetCryptoStatus() -> std::string
{
    spdlog::info("Module [Crypto] is doing some dummy work...");
    return "Crypto OK";
}

} // namespace blank_chat::crypto
