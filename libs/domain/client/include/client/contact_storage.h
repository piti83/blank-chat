#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_STORAGE_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_STORAGE_H_

#include <filesystem>
#include <vector>

#include "client/contact.h"
#include "client/raw_contact.h"

namespace bc::domain::client {

auto ParseContacts(const std::filesystem::path& contactsPath) -> std::vector<RawContact>;

auto SyncContactsToDisk(const std::filesystem::path& contactsPath,
                        const std::vector<const Contact*>& activeContacts) -> bool;

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONTACT_STORAGE_H_
