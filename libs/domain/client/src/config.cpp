#include <filesystem>
#include <optional>

#include "client/config.h"
#include <toml++/toml.hpp>

namespace bc::domain::client {

[[nodiscard]] auto LoadConfig(const std::filesystem::path& configFilePath)
    -> std::optional<ClientConfig>
{
    return std::nullopt;
}

} // namespace bc::domain::client
