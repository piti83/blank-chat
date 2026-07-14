#include <sodium.h>

#include <client/address_book.h>
#include <client/config.h>
#include <core/logger.h>
#include <crypto/identity_key.h>

#include <cli/repl.h>

auto main() -> int
{
    if (sodium_init() < 0) {
        return 1;
    }

    bc::core::Logger::Init();

    bc::domain::client::ClientConfig config;
    std::filesystem::path configPath = "/etc/blank-chat/client_config.toml";

    if (auto hasVal = bc::domain::client::LoadConfig(configPath)) {
        config = *hasVal;
    } else {
        BC_ERROR("Failed to parse client config file: {}", configPath.string());
        BC_INFO("Falling back to default configuration.");
    }

    if (config.relayConfig.onionAddress == "CHANGE_ME.onion") {
        BC_CRITICAL("You must configure the server's .onion address before starting!");
        BC_INFO("Please edit: {}", configPath.string());
        return 1;
    }

    auto myIdentity = bc::crypto::IdentityKey::Generate();
    BC_INFO("Successfully generated new ephemeral IdentityKey.");

    bc::domain::client::AddressBook addressBook;
    addressBook.Initialize(config.storageConfig.contactsFilePath, myIdentity);

    bc::domain::client::ConversationCache cache;
    cache.Initialize("msg_history");

    bc::cli::Repl repl(addressBook, cache, myIdentity, config.networkConfig.torSocksHost,
                       config.networkConfig.torSocksPort, config.relayConfig.onionAddress,
                       config.relayConfig.onionPort);
    repl.Run();

    return 0;
}
