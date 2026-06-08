#include <vector>

#include <client/raw_contact.h>

#include "client/contact_storage.h"
#include <simdjson.h>

namespace bc::domain::client {

auto ParseContacts(const std::filesystem::path& contactsPath) -> std::vector<RawContact>
{
}

} // namespace bc::domain::client
