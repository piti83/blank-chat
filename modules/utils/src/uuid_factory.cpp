#include "utils/uuid_factory.h"

#include <array>
#include <cstdint>
#include <sodium/randombytes.h>

namespace blank_chat::utils {

auto UuidFactory::GenerateUuidV4() -> std::array<uint8_t, kUuidSize>
{
    std::array<uint8_t, kUuidSize> uuidBuffer{};
    randombytes_buf(uuidBuffer.data(), uuidBuffer.size());

    uuidBuffer[kVersionByteIndex] =
        (uuidBuffer[kVersionByteIndex] & kVersionClearMask) | kVersionSetMask;
    uuidBuffer[kVariantByteIndex] =
        (uuidBuffer[kVariantByteIndex] & kVariantClearMask) | kVariantSetMask;

    return uuidBuffer;
}

} // namespace blank_chat::utils
