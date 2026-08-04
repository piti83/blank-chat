#include <iostream>
#include <optional>

#include <sodium.h>

#include <client/address_book.h>
#include <client/config.h>
#include <client/identity_storage.h>
#include <core/logger.h>
#include <crypto/identity_key.h>

#include <cli/repl.h>
#include <fcntl.h>
#include <seccomp.h>
#include <sys/prctl.h>
#include <unistd.h>

auto SafetyCheck() -> bool
{
    if (fcntl(STDIN_FILENO, F_GETFD) == -1 || fcntl(STDOUT_FILENO, F_GETFD) == -1 ||
        fcntl(STDERR_FILENO, F_GETFD) == -1) {
        std::cerr << "[ERROR] Standard file descriptors tampered with. Halting.\n";
        return false;
    }

    if (prctl(PR_SET_DUMPABLE, 0) == -1) {
        std::cerr << "[ERROR] Failed to set PR_SET_DUMPABLE. Halting.\n";
        return false;
    }

    return true;
}

auto EnableSeccompSandbox() -> bool
{
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (ctx == nullptr) {
        BC_CRITICAL("seccomp_init failed.");
        return false;
    }

    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(execve), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(execveat), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(execveat), 0);

    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(ptrace), 0);

    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(mount), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(umount2), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(chroot), 0);

    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(init_module), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(delete_module), 0);
    seccomp_rule_add(ctx, SCMP_ACT_KILL_PROCESS, SCMP_SYS(reboot), 0);

    if (seccomp_load(ctx) < 0) {
        BC_CRITICAL("seccomp_load failed.");
        seccomp_release(ctx);
        return false;
    }

    seccomp_release(ctx);
    BC_INFO("Robust blacklist Seccomp sandbox engaged successfully.");
    return true;
}

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
    if (!SafetyCheck()) {
        std::cerr << "Error during safety check. Aborting.\n";
        return 1;
    }

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

    if (!EnableSeccompSandbox()) {
        BC_CRITICAL("Failed to engage OS-level sandbox. Halting for security reasons.");
        return 1;
    }

    bc::cli::Repl repl(addressBook, cache, *identity, *config);
    repl.Run();
    bc::cli::Repl::WipeTerminal();

    return 0;
}
