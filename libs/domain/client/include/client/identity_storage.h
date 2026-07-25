#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_IDENTITY_STORAGE_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_IDENTITY_STORAGE_H_

#include <filesystem>
#include <optional>

#include <crypto/identity_key.h>

namespace bc::domain::client {

[[nodiscard]] auto LoadIdentity(const std::filesystem::path& identityPath)
    -> std::optional<bc::crypto::IdentityKey>;

auto SaveIdentity(const std::filesystem::path& identityPath,
                  const bc::crypto::IdentityKey& identity) -> bool;

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_IDENTITY_STORAGE_H_
