#include "client/identity_storage.h"

#include <fstream>
#include <string_view>

#include <sodium.h>

#include <core/logger.h>
#include <core/string_utils.h>

#include <simdjson.h>

namespace bc::domain::client {

auto LoadIdentity(const std::filesystem::path& identityPath)
    -> std::optional<bc::crypto::IdentityKey>
{
    if (!std::filesystem::exists(identityPath)) {
        return std::nullopt;
    }

    simdjson::ondemand::parser parser;
    simdjson::padded_string jsonContent;
    auto ioError = simdjson::padded_string::load(identityPath.string()).get(jsonContent);

    if (static_cast<bool>(ioError)) {
        BC_ERROR("Failed to read identity file.");
        return std::nullopt;
    }

    simdjson::ondemand::document doc;
    if (static_cast<bool>(parser.iterate(jsonContent).get(doc))) {
        BC_ERROR("Failed to parse identity JSON.");
        return std::nullopt;
    }

    std::string_view pkHex;
    std::string_view skHex;
    if (static_cast<bool>(doc.find_field("publicKey").get(pkHex)) ||
        static_cast<bool>(doc.find_field("secretKey").get(skHex))) {
        BC_ERROR("Malformed identity JSON.");
        return std::nullopt;
    }

    bc::crypto::PublicKeyType pk{};
    if (!bc::core::DecodeHexToArray(pkHex, pk)) {
        return std::nullopt;
    }

    bc::core::SecureBuffer sk(crypto_sign_SECRETKEYBYTES);
    if (!bc::core::DecodeHexToArray(skHex, sk.AsMutableSpan())) {
        return std::nullopt;
    }

    sodium_memzero(jsonContent.data(), jsonContent.size());

    return bc::crypto::IdentityKey::Restore(pk, std::move(sk));
}

auto SaveIdentity(const std::filesystem::path& identityPath,
                  const bc::crypto::IdentityKey& identity) -> bool
{
    std::ofstream out(identityPath, std::ios::trunc);
    if (!out) {
        BC_ERROR("Failed to open identity file for writing.");
        return false;
    }

    std::string pkHex = bc::core::EncodeHex(identity.GetPublicKey());
    std::string skHex = bc::core::EncodeHex(identity.GetSecretKeySpan());

    out << "{\n";
    out << "  \"publicKey\": \"" << pkHex << "\",\n";
    out << "  \"secretKey\": \"" << skHex << "\"\n";
    out << "}\n";

    sodium_memzero(skHex.data(), skHex.size());

    return out.good();
}

} // namespace bc::domain::client
