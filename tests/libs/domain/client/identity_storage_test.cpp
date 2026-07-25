#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <client/identity_storage.h>
#include <crypto/identity_key.h>

namespace bc::domain::client::test {

class IdentityStorageTest : public ::testing::Test
{
protected:
    std::string testPath = "test_identity.json";

    void TearDown() override
    {
        if (std::filesystem::exists(testPath)) {
            std::filesystem::remove(testPath);
        }
    }

    void WriteRaw(const std::string& content)
    {
        std::ofstream out(testPath, std::ios::trunc);
        out << content;
    }
};

TEST_F(IdentityStorageTest, SaveAndLoadIdentitySucceeds)
{
    auto originalIdentity = bc::crypto::IdentityKey::Generate();

    EXPECT_TRUE(SaveIdentity(testPath, originalIdentity));
    ASSERT_TRUE(std::filesystem::exists(testPath));

    auto loadedIdentityOpt = LoadIdentity(testPath);
    ASSERT_TRUE(loadedIdentityOpt.has_value());

    EXPECT_EQ(loadedIdentityOpt->GetPublicKey(), originalIdentity.GetPublicKey());

    auto origSk = originalIdentity.GetSecretKeySpan();
    auto loadedSk = loadedIdentityOpt->GetSecretKeySpan();
    EXPECT_TRUE(std::equal(origSk.begin(), origSk.end(), loadedSk.begin()));
}

TEST_F(IdentityStorageTest, LoadReturnsNulloptWhenFileDoesNotExist)
{
    auto loadedIdentityOpt = LoadIdentity("does_not_exist_at_all.json");
    EXPECT_FALSE(loadedIdentityOpt.has_value());
}

TEST_F(IdentityStorageTest, FailsSecurelyOnMalformedJson)
{
    WriteRaw("{ \"publicKey\": \"010203\", \"secretKey\": ");
    auto loadedIdentityOpt = LoadIdentity(testPath);
    EXPECT_FALSE(loadedIdentityOpt.has_value());
}

TEST_F(IdentityStorageTest, FailsSecurelyOnMissingFields)
{
    WriteRaw(R"({ "publicKey": "01020304" })");
    auto loadedIdentityOpt = LoadIdentity(testPath);
    EXPECT_FALSE(loadedIdentityOpt.has_value());
}

TEST_F(IdentityStorageTest, FailsSecurelyOnInvalidHexEncoding)
{
    WriteRaw(R"({
        "publicKey": "invalid_hex_string_here!",
        "secretKey": "another_invalid_hex_string"
    })");
    auto loadedIdentityOpt = LoadIdentity(testPath);
    EXPECT_FALSE(loadedIdentityOpt.has_value());
}

} // namespace bc::domain::client::test
