#include <iostream>

#include <sodium.h>

#include <client/address_book.h>
#include <client/config.h>
#include <client/identity_storage.h>
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
            return 1;
        }

        myIdentityOpt = bc::crypto::IdentityKey::Generate();
        if (!bc::domain::client::SaveIdentity(identityPath, *myIdentityOpt)) {
            BC_CRITICAL("Failed to save the new identity to disk. Check permissions.");
            return 1;
        }
        std::cout << "[+] New identity generated and saved successfully.\n";
        BC_INFO("Successfully generated new static IdentityKey.");
    }

    bc::domain::client::AddressBook addressBook;
    addressBook.Initialize(config.storageConfig.contactsFilePath, *myIdentityOpt);

    bc::domain::client::ConversationCache cache;
    cache.Initialize("msg_history");

    bc::cli::Repl repl(addressBook, cache, *myIdentityOpt, config.networkConfig.torSocksHost,
                       config.networkConfig.torSocksPort, config.relayConfig.onionAddress,
                       config.relayConfig.onionPort);
    repl.Run();

    return 0;
}
