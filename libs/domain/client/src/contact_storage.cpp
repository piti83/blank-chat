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

    if (!static_cast<bool>(contactObj.find_field("rxMailboxId").get(tempView))) {
        std::vector<std::uint8_t> tmp(bc::protocol::mailboxIdSize);
        if (bc::core::DecodeHexToArray(tempView, tmp))
            contact.rxMailboxId = std::move(tmp);
    }
    if (!static_cast<bool>(contactObj.find_field("txMailboxId").get(tempView))) {
        std::vector<std::uint8_t> tmp(bc::protocol::mailboxIdSize);
        if (bc::core::DecodeHexToArray(tempView, tmp))
            contact.txMailboxId = std::move(tmp);
    }
    if (!static_cast<bool>(contactObj.find_field("rxKey").get(tempView))) {
        std::vector<std::uint8_t> tmp(bc::crypto::symmetricKeySize);
        if (bc::core::DecodeHexToArray(tempView, tmp))
            contact.rxKey = std::move(tmp);
    }
    if (!static_cast<bool>(contactObj.find_field("txKey").get(tempView))) {
        std::vector<std::uint8_t> tmp(bc::crypto::symmetricKeySize);
        if (bc::core::DecodeHexToArray(tempView, tmp))
            contact.txKey = std::move(tmp);
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

auto SyncContactsToDisk(const std::filesystem::path& contactsPath,
                        const std::vector<const Contact*>& activeContacts) -> bool
{
    std::ofstream outFile(contactsPath, std::ios::trunc | std::ios::binary);
    if (!outFile) {
        BC_ERROR("Failed to open contacts file for syncing.");
        return false;
    }

    outFile << "{\n  \"contacts\": [\n";
    for (size_t i = 0; i < activeContacts.size(); ++i) {
        // NOLINTBEGIN(modernize-raw-string-literal)
        const auto* c = activeContacts.at(i);
        outFile << "    {\n";
        outFile << "      \"alias\": \"" << bc::core::EscapeJsonString(c->alias) << "\",\n";
        outFile << "      \"publicKey\": \"" << bc::core::EncodeHex(c->publicKey) << "\"";

        if (c->note) {
            outFile << ",\n      \"note\": \"" << bc::core::EscapeJsonString(*c->note) << "\"";
        }

        outFile << ",\n      \"rxMailboxId\": \"" << bc::core::EncodeHex(c->rxMailboxId.AsSpan())
                << "\",\n";
        outFile << "      \"txMailboxId\": \"" << bc::core::EncodeHex(c->txMailboxId.AsSpan())
                << "\",\n";
        outFile << "      \"rxKey\": \"" << bc::core::EncodeHex(c->rxKey.AsSpan()) << "\",\n";
        outFile << "      \"txKey\": \"" << bc::core::EncodeHex(c->txKey.AsSpan()) << "\"\n";

        outFile << "    }";
        // NOLINTEND(modernize-raw-string-literal)

        if (i < activeContacts.size() - 1) {
            outFile << ",";
        }
        outFile << "\n";
    }
    outFile << "  ]\n}\n";

    if (!outFile.good()) {
        BC_ERROR("An error occurred while writing to the contacts file.");
        return false;
    }
    return true;
}

} // namespace bc::domain::client
