#include "client/contact_storage.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <client/raw_contact.h>
#include <core/logger.h>
#include <core/string_utils.h>

#include <simdjson.h>

namespace {

[[nodiscard]] auto ParseSingleContact(simdjson::ondemand::object& contactObj)
    -> std::optional<bc::domain::client::RawContact>
{
    bc::domain::client::RawContact contact;
    std::string_view tempView;

    if (static_cast<bool>(contactObj.find_field("alias").get(tempView))) {
        BC_ERROR("Contact missing required 'alias' field");
        return std::nullopt;
    }
    contact.alias = std::string(tempView);

    if (static_cast<bool>(contactObj.find_field("publicKey").get(tempView))) {
        BC_ERROR("Contact missing required 'publicKey' field");
        return std::nullopt;
    }

    if (!bc::core::DecodeHexToArray(tempView, contact.publicKey)) {
        BC_ERROR("Contact '{}' has malformed or incorrect length hex in 'publicKey', skipping.",
                 contact.alias);
        return std::nullopt;
    }

    if (!static_cast<bool>(contactObj.find_field("note").get(tempView))) {
        contact.note = std::string(tempView);
    }

    return contact;
}

} // namespace

namespace bc::domain::client {

auto ParseContacts(const std::filesystem::path& contactsPath) -> std::vector<RawContact>
{
    std::vector<RawContact> parsedContacts;
    simdjson::ondemand::parser parser;

    simdjson::padded_string jsonContent;
    simdjson::error_code ioError =
        simdjson::padded_string::load(contactsPath.string()).get(jsonContent);

    if (static_cast<bool>(ioError)) {
        BC_ERROR("Failed to load contacts file: {}", simdjson::error_message(ioError));
        return parsedContacts;
    }

    simdjson::ondemand::document doc;
    simdjson::error_code parseError = parser.iterate(jsonContent).get(doc);

    if (static_cast<bool>(parseError)) {
        BC_ERROR("Failed to parse contacts JSON: {}", simdjson::error_message(parseError));
        return parsedContacts;
    }

    simdjson::ondemand::array contactsArray;
    simdjson::error_code arrayError = doc.find_field("contacts").get(contactsArray);

    if (static_cast<bool>(arrayError)) {
        BC_ERROR("Failed to find or parse 'contacts' array: {}",
                 simdjson::error_message(arrayError));
        return parsedContacts;
    }

    for (auto contactVal : contactsArray) {
        simdjson::ondemand::object contactObj;
        if (static_cast<bool>(contactVal.get(contactObj))) {
            BC_ERROR("Expected an object in the contacts array");
            continue;
        }

        if (auto contact = ParseSingleContact(contactObj)) {
            parsedContacts.push_back(std::move(*contact));
        }
    }

    return parsedContacts;
}

auto SaveContact(const std::filesystem::path& contactsPath, std::string_view alias,
                 const PublicKeyType& publicKey, std::optional<std::string_view> note) -> void
{
    std::vector<RawContact> contacts;
    if (std::filesystem::exists(contactsPath)) {
        contacts = ParseContacts(contactsPath);
    }

    RawContact newContact{.alias = std::string(alias),
                          .publicKey = publicKey,
                          .note =
                              note ? std::optional<std::string>(std::string(*note)) : std::nullopt};
    contacts.push_back(std::move(newContact));

    std::ofstream outFile(contactsPath, std::ios::trunc | std::ios::binary);
    if (!outFile) {
        BC_ERROR("Failed to open contacts file for writing.");
        return;
    }

    outFile << "{\n  \"contacts\": [\n";

    for (size_t i = 0; i < contacts.size(); ++i) {
        // NOLINTBEGIN(modernize-raw-string-literal)
        const auto& c = contacts.at(i);
        outFile << "    {\n";
        outFile << "      \"alias\": \"" << bc::core::EscapeJsonString(c.alias) << "\",\n";
        outFile << "      \"publicKey\": \"" << bc::core::EncodeHex(c.publicKey) << "\"";

        if (c.note) {
            outFile << ",\n      \"note\": \"" << bc::core::EscapeJsonString(*c.note) << "\"\n";
        } else {
            outFile << "\n";
        }

        outFile << "    }";
        // NOLINTEND(modernize-raw-string-literal)

        if (i < contacts.size() - 1) {
            outFile << ",";
        }
        outFile << "\n";
    }

    outFile << "  ]\n}\n";

    if (!outFile.good()) {
        BC_ERROR("An error occurred while writing to the contacts file.");
    }
}

} // namespace bc::domain::client
