#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <client/raw_contact.h>
#include <core/logger.h>

#include "client/contact_storage.h"
#include <simdjson.h>

namespace {

// NOLINTBEGIN
constexpr auto HexCharToInt(char c) -> int
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

constexpr char HexIntToChar(int v)
{
    return "0123456789abcdef"[v & 0x0F];
}

[[nodiscard]] auto DecodeHexToArray(std::string_view hexStr, std::span<uint8_t> outArray) -> bool
{
    if (hexStr.length() != outArray.size() * 2) {
        return false;
    }

    for (size_t i = 0; i < outArray.size(); ++i) {
        int high = HexCharToInt(hexStr[i * 2]);
        int low = HexCharToInt(hexStr[(i * 2) + 1]);

        if (high == -1 || low == -1) {
            return false;
        }

        outArray[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

[[nodiscard]] auto EncodeHex(std::span<const uint8_t> data) -> std::string
{
    std::string hex;
    hex.reserve(data.size() * 2);
    for (uint8_t b : data) {
        hex.push_back(HexIntToChar(b >> 4));
        hex.push_back(HexIntToChar(b & 0x0F));
    }
    return hex;
}
// NOLINTEND

[[nodiscard]] auto EscapeJsonString(std::string_view input) -> std::string
{
    std::string output;
    output.reserve(input.length() + 4);
    for (char c : input) {
        if (c == '"')
            output += "\\\"";
        else if (c == '\\')
            output += "\\\\";
        else if (c == '\b')
            output += "\\b";
        else if (c == '\f')
            output += "\\f";
        else if (c == '\n')
            output += "\\n";
        else if (c == '\r')
            output += "\\r";
        else if (c == '\t')
            output += "\\t";
        else
            output += c;
    }
    return output;
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

        RawContact contact;
        std::string_view tempView;

        if (static_cast<bool>(contactObj.find_field("alias").get(tempView))) {
            BC_ERROR("Contact missing required 'alias' field");
            continue;
        }
        contact.alias = std::string(tempView);

        if (static_cast<bool>(contactObj.find_field("publicKey").get(tempView))) {
            BC_ERROR("Contact missing required 'publicKey' field");
            continue;
        }

        if (!DecodeHexToArray(tempView, contact.publicKey)) {
            BC_ERROR("Contact '{}' has malformed or incorrect length hex in 'publicKey', skipping.",
                     contact.alias);
            continue;
        }

        if (!static_cast<bool>(contactObj.find_field("note").get(tempView))) {
            contact.note = std::string(tempView);
        }

        parsedContacts.push_back(std::move(contact));
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
        outFile << "      \"alias\": \"" << EscapeJsonString(c.alias) << "\",\n";
        outFile << "      \"publicKey\": \"" << EncodeHex(c.publicKey) << "\"";

        if (c.note) {
            outFile << ",\n      \"note\": \"" << EscapeJsonString(*c.note) << "\"\n";
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
