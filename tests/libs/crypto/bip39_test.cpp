#include <gtest/gtest.h>

#include <crypto/bip39.h>
#include <crypto/identity_key.h>

namespace bc::crypto::test {

TEST(Bip39Test, EncodeAndDecodeAreSymmetric)
{
    auto identity = IdentityKey::Generate();
    auto originalPubKey = identity.GetPublicKey();

    auto mnemonic = bip39::Encode(originalPubKey);
    ASSERT_FALSE(mnemonic.StringView().empty());

    auto decodedPubKeyOpt = bip39::Decode(mnemonic.StringView());
    ASSERT_TRUE(decodedPubKeyOpt.has_value());

    EXPECT_EQ(originalPubKey, *decodedPubKeyOpt);
}

TEST(Bip39Test, DecodeFailsOnInvalidChecksum)
{
    auto identity = IdentityKey::Generate();
    auto mnemonic = bip39::Encode(identity.GetPublicKey());

    std::string tamperedMnemonic(mnemonic.StringView());
    auto firstDash = tamperedMnemonic.find('-');

    if (mnemonic.StringView().starts_with("abandon-")) {
        tamperedMnemonic.replace(0, firstDash, "ability");
    } else {
        tamperedMnemonic.replace(0, firstDash, "abandon");
    }

    auto decodedOpt = bip39::Decode(tamperedMnemonic);
    EXPECT_FALSE(decodedOpt.has_value()) << "Mnemonic with tampered checksum must be rejected.";
}

TEST(Bip39Test, DecodeFailsOnWordsNotInDictionary)
{
    auto identity = IdentityKey::Generate();
    auto mnemonic = bip39::Encode(identity.GetPublicKey());

    std::string tamperedMnemonic(mnemonic.StringView());
    auto firstDash = tamperedMnemonic.find('-');
    tamperedMnemonic.replace(0, firstDash, "invalidword");

    auto decodedOpt = bip39::Decode(tamperedMnemonic);
    EXPECT_FALSE(decodedOpt.has_value());
}

TEST(Bip39Test, DecodeFailsOnIncorrectWordCount)
{
    std::string shortMnemonic = "abandon-ability-able-about-above";
    auto decodedOpt = bip39::Decode(shortMnemonic);
    EXPECT_FALSE(decodedOpt.has_value());
}

} // namespace bc::crypto::test
