#pragma once

#include <cstddef>
#include <cstdint>

namespace blank_chat::utils {

inline constexpr std::size_t kUuidSize = 16;

inline constexpr size_t kVersionByteIndex = 6;
inline constexpr size_t kVariantByteIndex = 8;

inline constexpr uint8_t kVersionClearMask = 0x0F;
inline constexpr uint8_t kVersionSetMask   = 0x40;

inline constexpr uint8_t kVariantClearMask = 0x3F;
inline constexpr uint8_t kVariantSetMask   = 0x80;

}
