#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_ADDRESS_BOOK_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_ADDRESS_BOOK_H_

#include <filesystem>
#include <unordered_map>

#include <client/contact.h>
#include <core/string_hash.h>

namespace bc::domain::client {

class AddressBook
{
public:
    AddressBook() = default;
    AddressBook(const AddressBook&) = delete;
    AddressBook(AddressBook&&) = delete;

    auto operator=(const AddressBook&) -> AddressBook& = delete;
    auto operator=(AddressBook&&) -> AddressBook& = delete;

    ~AddressBook() = default;

    auto Initialize(const std::filesystem::path& pathToContactsFile,
                    const bc::crypto::IdentityKey& myIdentity) -> void;

    auto AddContact(std::string alias, const PublicKeyType& publicKey,
                    std::optional<std::string> note) -> bool;
    auto GetContact(std::string_view alias) -> const Contact*;

    [[nodiscard]] auto GetAllAliases() const -> std::vector<std::string>;

    [[nodiscard]] auto GetAliasByRxMailboxId(const bc::protocol::MailboxID& rxId) const
        -> std::string;

private:
    std::unordered_map<std::string, Contact, bc::core::StringHash, std::equal_to<>> contacts;
    std::filesystem::path contactsFilePath;

    const bc::crypto::IdentityKey* identity{nullptr};
};

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_ADDRESS_BOOK_H_
