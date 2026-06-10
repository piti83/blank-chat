#include "client/address_book.h"

#include <unordered_map>

#include <client/contact.h>
#include <client/contact_storage.h>
#include <core/logger.h>
#include <protocol/mailbox_id.h>

#include "client/raw_contact.h"

namespace bc::domain::client {

auto AddressBook::Initialize(const std::filesystem::path& pathToContactsFile) -> void
{
    contactsFilePath = pathToContactsFile;
    std::vector<RawContact> contactsVec = ParseContacts(contactsFilePath);

    for (const auto& c : contactsVec) {
        contacts[c.alias] = {.alias = c.alias,
                             .publicKey = c.publicKey,
                             .note = c.note,
                             // TODO
                             // Do some magic with public key here
                             // to get rx and tx mailbox ids.
                             // For now just temporary empty object
                             .rxMailboxId = protocol::MailboxID(),
                             .txMailboxId = protocol::MailboxID()};
    }
    BC_INFO("Address book initialized!");
}

auto AddressBook::AddContact(std::string alias, const PublicKeyType& publicKey,
                             std::optional<std::string> note) -> void
{
    SaveContact(contactsFilePath, alias, publicKey, note);
    BC_INFO("{} succesfully saved to contacts file.", alias);

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
    BC_INFO("Contact succesfully loaded");
}

auto AddressBook::GetContact(std::string_view alias) -> const Contact*
{
    if (auto it = contacts.find(alias); it != contacts.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace bc::domain::client
