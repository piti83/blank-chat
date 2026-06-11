#include "client/address_book.h"

#include <unordered_map>

#include <client/contact.h>
#include <client/contact_storage.h>
#include <core/logger.h>
#include <crypto/mailbox_derivation.h>
#include <protocol/mailbox_id.h>

#include "client/raw_contact.h"

namespace bc::domain::client {

auto AddressBook::Initialize(const std::filesystem::path& pathToContactsFile,
                             const bc::crypto::IdentityKey& myIdentity) -> void
{
    contactsFilePath = pathToContactsFile;
    identity = &myIdentity;

    std::vector<RawContact> contactsVec = ParseContacts(contactsFilePath);

    for (const auto& c : contactsVec) {
        auto derivedOpt = bc::crypto::DerivePairwiseMailboxes(*identity, c.publicKey);

        if (!derivedOpt) {
            BC_ERROR("Failed to derive mailbox IDs for contact '{}'.", c.alias);
            continue;
        }

        contacts[c.alias] = {.alias = c.alias,
                             .publicKey = c.publicKey,
                             .note = c.note,
                             .rxMailboxId = bc::protocol::MailboxID(derivedOpt->rxId),
                             .txMailboxId = bc::protocol::MailboxID(derivedOpt->txId)};
    }
    BC_INFO("Address book initialized!");
}

auto AddressBook::AddContact(std::string alias, const PublicKeyType& publicKey,
                             std::optional<std::string> note) -> bool
{
    if (!static_cast<bool>(identity)) {
        BC_ERROR("Cannot add contact: AddressBook is not initialized with IdentityKey.");
        return false;
    }

    auto derivedOpt = bc::crypto::DerivePairwiseMailboxes(*identity, publicKey);

    if (!derivedOpt) {
        BC_ERROR("Failed to derive mailbox IDs for new contact '{}'. The provided public key is "
                 "mathematically invalid.",
                 alias);
        return false;
    }

    SaveContact(contactsFilePath, alias, publicKey, note);
    BC_INFO("{} succesfully saved to contacts file.", alias);

    auto newContact = Contact{.alias = alias,
                              .publicKey = publicKey,
                              .note = std::move(note),
                              .rxMailboxId = bc::protocol::MailboxID(derivedOpt->rxId),
                              .txMailboxId = bc::protocol::MailboxID(derivedOpt->txId)};

    contacts.insert_or_assign(std::move(alias), std::move(newContact));
    BC_INFO("Contact succesfully loaded");
    return true;
}

auto AddressBook::GetContact(std::string_view alias) -> const Contact*
{
    if (auto it = contacts.find(alias); it != contacts.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace bc::domain::client
