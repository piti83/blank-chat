#include "client/address_book.h"

#include <unordered_map>

#include <client/contact.h>
#include <client/contact_storage.h>
#include <protocol/mailbox_id.h>

namespace bc::domain::client {

auto AddressBook::Initialize() -> void
{
}

auto AddressBook::AddContact(std::string alias, const PublicKeyType& publicKey,
                             std::optional<std::string> note) -> void
{
    SaveContact(alias, publicKey, note);

    auto newContact = Contact{.alias = alias,
                              .publicKey = publicKey,
                              .note = std::move(note),
                              // TODO
                              // Do some magic with public key here
                              // to get rx and tx mailbox ids.
                              // For now just temporary empty object
                              .rxMailboxId = protocol::MailboxID(),
                              .txMailboxId = protocol::MailboxID()};

    contacts.insert_or_assign(std::move(alias), std::move(newContact));
}

auto AddressBook::GetContact(std::string_view alias) -> const Contact*
{
    if (auto it = contacts.find(alias); it != contacts.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace bc::domain::client
