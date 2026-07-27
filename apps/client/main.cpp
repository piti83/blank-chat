#include <iostream>
#include <optional>

#include <sodium.h>

#include <client/address_book.h>
#include <client/config.h>
#include <client/identity_storage.h>
#include <core/logger.h>
#include <crypto/identity_key.h>

#include <cli/repl.h>

auto InitializeConfigs() -> std::optional<bc::domain::client::ClientConfig>
{
    bc::domain::client::ClientConfig config;
    std::filesystem::path configPath = "/etc/blank-chat/client_config.toml";

    if (auto hasVal = bc::domain::client::LoadConfig(configPath)) {
        config = *hasVal;
    } else {
        BC_CRITICAL("Failed to parse client config file: {}. Halting.", configPath.string());
        return std::nullopt;
    }

    if (config.relayConfig.onionAddress == "CHANGE_ME.onion") {
        BC_CRITICAL("You must configure the server's .onion address before starting!");
        return std::nullopt;
    }

    return config;
}

auto InitializeIdentity() -> std::optional<bc::crypto::IdentityKey>
{
    std::filesystem::path identityPath = "/etc/blank-chat/identity.json";
    std::optional<bc::crypto::IdentityKey> myIdentityOpt =
        bc::domain::client::LoadIdentity(identityPath);

    if (myIdentityOpt) {
        std::cout << "[+] Found existing identity in " << identityPath << ".\n";
        BC_INFO("Identity loaded from JSON.");
    } else {
        std::cout << "[!] No identity found at " << identityPath << ".\n";
        std::cout << "Do you want to generate a new Identity Key now? (y/n): ";
        char answer = 0;
        std::cin >> answer;

        if (answer != 'y' && answer != 'Y') {
            std::cout << "Exiting...\n";
            return std::nullopt;
        }

        myIdentityOpt = bc::crypto::IdentityKey::Generate();
        if (!bc::domain::client::SaveIdentity(identityPath, *myIdentityOpt)) {
            BC_CRITICAL("Failed to save the new identity to disk. Check permissions.");
            return std::nullopt;
        }
        std::cout << "New identity generated and saved successfully.\n";
        BC_INFO("Successfully generated new static IdentityKey.");
    }
    return myIdentityOpt;
}

auto main() -> int
{
    if (sodium_init() < 0) {
        return 1;
    }
    bc::core::Logger::Init();

    std::optional<bc::domain::client::ClientConfig> config = InitializeConfigs();
    if (!config) {
        return 1;
    }

    std::optional<bc::crypto::IdentityKey> identity = InitializeIdentity();
    if (!identity) {
        return 1;
    };

    bc::domain::client::AddressBook addressBook;
    addressBook.Initialize(config->storageConfig.contactsFilePath, *identity);

    bc::domain::client::ConversationCache cache;
    cache.Initialize("msg_history");

    bc::cli::Repl repl(addressBook, cache, *identity, *config);
    repl.Run();

    return 0;
}
