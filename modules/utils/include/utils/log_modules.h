#pragma once

#include <cstdint>

namespace bc::utils {

enum class LogModule : uint8_t { Crypto, Network, Protocol, Utils, Server, Client, Count };

}
