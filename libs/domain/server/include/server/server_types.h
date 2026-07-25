#ifndef BC_LIBS_DOMAIN_SERVER_INCLUDE_SERVERTYPES_H_
#define BC_LIBS_DOMAIN_SERVER_INCLUDE_SERVERTYPES_H_

#include <cstdint>

namespace bc::domain::server {

constexpr std::uint8_t defaultMemoryQuotaPercent = 80;
constexpr std::uint32_t defaultMaxMessagesPerMailbox = 50;

} // namespace bc::domain::server

#endif // BC_LIBS_DOMAIN_SERVER_INCLUDE_SERVERTYPES_H_
