#include "crypto/placeholder.h"

#include <spdlog/spdlog.h>

namespace bc::crypto {

auto GetCryptoStatus() -> std::string
{
    spdlog::info("Module [Crypto] is doing some dummy work...");
    return "Crypto OK";
}

} // namespace bc::crypto
