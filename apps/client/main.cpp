#include <sodium.h>

#include <client/config.h>
#include <core/logger.h>

#include <cli/repl.h>

auto main() -> int
{
    bc::core::Logger::Init();

    bc::domain::client::ClientConfig config;
    std::filesystem::path configPath = "/etc/blank-chat/bc_client_config.toml";

    if (auto hasVal = bc::domain::client::LoadConfig(configPath)) {
        config = *hasVal;
    } else {
        BC_ERROR("Failed to parse client config file: {}", configPath.string());
        return 1;
    }

    (void)config;

    // TODO: utilize config in client
    bc::cli::Repl repl;
    repl.Run();

    return 0;
}
