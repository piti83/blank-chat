#ifndef BC_LIBS_NETWORK_INCLUDE_TOR_CONTROL_H_
#define BC_LIBS_NETWORK_INCLUDE_TOR_CONTROL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio.hpp>

namespace bc::network {

struct HiddenServiceConfig
{
    std::string_view controlHost;
    std::uint16_t controlPort;
    std::uint16_t localListenPort;
};

class TorControl
{
public:
    [[nodiscard]] static auto CreateEphemeralHiddenService(boost::asio::io_context& ioContext,
                                                           const HiddenServiceConfig& config)
        -> std::optional<std::string>;
};

} // namespace bc::network

#endif // BC_LIBS_NETWORK_INCLUDE_TOR_CONTROL_H_
