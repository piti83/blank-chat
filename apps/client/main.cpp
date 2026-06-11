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
    std::filesystem::path configPath = "client_config.toml";

    if (auto hasVal = bc::domain::client::LoadConfig(configPath)) {
        config = *hasVal;
    } else {
        BC_ERROR("Failed to parse client config file: {}", configPath.string());
        BC_INFO("Falling back to default configuration.");
    }

    auto myIdentity = bc::crypto::IdentityKey::Generate();
    BC_INFO("Successfully generated new ephemeral IdentityKey.");

    bc::domain::client::AddressBook addressBook;
    addressBook.Initialize(config.storageConfig.contactsFilePath, myIdentity);

    bc::cli::Repl repl(addressBook, myIdentity, config.networkConfig.torSocksHost,
                       config.networkConfig.torSocksPort);
    repl.Run();

    return 0;
}
