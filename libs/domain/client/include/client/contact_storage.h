#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_STORAGE_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_STORAGE_H_

#include <filesystem>
#include <optional>
#include <vector>

#include "client/raw_contact.h"

namespace bc::domain::client {

auto ParseContacts(const std::filesystem::path& contactsPath) -> std::vector<RawContact>;
auto SaveContact(const std::filesystem::path& contactsPath, std::string_view alias,
                 const PublicKeyType& publicKey, std::optional<std::string_view> note) -> void;

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_STORAGE_H_
