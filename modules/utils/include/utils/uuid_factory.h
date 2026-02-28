#pragma once

#include "utils/uuid_constants.h"

#include <cstdint>
#include <spdlog/spdlog.h>
#include <sodium.h>

namespace blank_chat::utils {

class UuidFactory
{
public:
    static auto GenerateUuidV4() -> std::array<uint8_t, kUuidSize>;
};

} // namespace blank_chat::utils
