#include "client/address_book.h"

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
        Contact newContact{.alias = c.alias,
                           .publicKey = c.publicKey,
                           .note = c.note,
                           .rxMailboxId = {},
                           .txMailboxId = {},
                           .rxKey = bc::core::SecureBuffer(bc::crypto::symmetricKeySize),
                           .txKey = bc::core::SecureBuffer(bc::crypto::symmetricKeySize)};

        if (c.rxKey && c.txKey && c.rxMailboxId && c.txMailboxId) {
            std::array<std::uint8_t, bc::protocol::mailboxIdSize> rxArr{};
            std::ranges::copy(*c.rxMailboxId, rxArr.begin());
            newContact.rxMailboxId = bc::protocol::MailboxID(rxArr);

            std::array<std::uint8_t, bc::protocol::mailboxIdSize> txArr{};
            std::ranges::copy(*c.txMailboxId, txArr.begin());
            newContact.txMailboxId = bc::protocol::MailboxID(txArr);

            std::ranges::copy(*c.rxKey, newContact.rxKey.AsMutableSpan().begin());
            std::ranges::copy(*c.txKey, newContact.txKey.AsMutableSpan().begin());

            BC_INFO("Restored existing keys and mailboxes for contact '{}'.", c.alias);
        } else {
            auto derivedOpt = bc::crypto::DerivePairwiseMailboxes(*identity, c.publicKey);
            if (!derivedOpt) {
                BC_ERROR("Failed to derive mailbox IDs for contact '{}'.", c.alias);
                continue;
            }
            newContact.rxMailboxId = bc::protocol::MailboxID(derivedOpt->rxId);
            newContact.txMailboxId = bc::protocol::MailboxID(derivedOpt->txId);
            newContact.rxKey = std::move(derivedOpt->rxKey);
            newContact.txKey = std::move(derivedOpt->txKey);
            BC_INFO("Derived initial keys for contact '{}'.", c.alias);
        }

        contacts.insert_or_assign(c.alias, std::move(newContact));
    }

    BC_INFO("Address book initialized!");
}

auto AddressBook::AddContact(const std::string& alias, const PublicKeyType& publicKey,
                             const std::optional<std::string>& note) -> bool
{
    if (!static_cast<bool>(identity)) {
        BC_ERROR("Cannot add contact: AddressBook is not initialized with IdentityKey.");
        return false;
    }

    auto derivedOpt = bc::crypto::DerivePairwiseMailboxes(*identity, publicKey);
    if (!derivedOpt) {
        BC_ERROR("Failed to derive E2EE keys for new contact '{}'.", alias);
        return false;
    }

    Contact newContact{.alias = alias,
                       .publicKey = publicKey,
                       .note = note,
                       .rxMailboxId = bc::protocol::MailboxID(derivedOpt->rxId),
                       .txMailboxId = bc::protocol::MailboxID(derivedOpt->txId),
                       .rxKey = std::move(derivedOpt->rxKey),
                       .txKey = std::move(derivedOpt->txKey)};

    contacts.insert_or_assign(alias, std::move(newContact));
    SaveToDisk();

    BC_INFO("Contact succesfully loaded and keys derived in RAM & Disk.");
    return true;
}

auto AddressBook::GetContact(std::string_view alias) -> const Contact*
{
    if (auto it = contacts.find(alias); it != contacts.end()) {
        return &it->second;
    }
    return nullptr;
}

auto AddressBook::GetAllAliases() const -> std::vector<std::string>
{
    std::vector<std::string> aliases;
    aliases.reserve(contacts.size());
    for (const auto& [alias, contactObj] : contacts) {
        aliases.push_back(alias);
    }
    return aliases;
}

auto AddressBook::GetAliasByRxMailboxId(const bc::protocol::MailboxID& rxId) const -> std::string
{
    for (const auto& [alias, contactObj] : contacts) {
        if (contactObj.rxMailboxId == rxId) {
            return alias;
        }
        if (contactObj.oldRxMailboxId.has_value() && *contactObj.oldRxMailboxId == rxId) {
            return alias;
        }
    }
    return "";
}

auto AddressBook::GetMutableContact(std::string_view alias) -> Contact*
{
    if (auto it = contacts.find(alias); it != contacts.end()) {
        return &it->second;
    }
    return nullptr;
}

auto AddressBook::SaveToDisk() const -> bool
{
    std::vector<const Contact*> activeContacts;
    activeContacts.reserve(contacts.size());
    for (const auto& [a, contactObj] : contacts) {
        activeContacts.push_back(&contactObj);
    }
    return SyncContactsToDisk(contactsFilePath, activeContacts);
}

} // namespace bc::domain::client
