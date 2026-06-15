#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <client/config.h>

namespace bc::domain::client::test {

class ClientConfigTest : public ::testing::Test
{
protected:
    std::string testConfigPath = "test_client_config.toml";

    void TearDown() override
    {
        if (std::filesystem::exists(testConfigPath)) {
            std::filesystem::remove(testConfigPath);
        }
    }

    auto WriteConfig(const std::string& content) -> void
    {
        std::ofstream out(testConfigPath, std::ios::trunc);
        out << content;
    }
};

TEST_F(ClientConfigTest, SuccessfullyParsesValidConfiguration)
{
    WriteConfig(R"(
        [network]
        tor_socks_host = "192.168.1.10"
        tor_socks_port = 9052

        [obfuscation]
        mode = "poisson"
        cbr_interval_ms = 1000
        poisson_lambda = 3.14

        [storage]
        contacts_file_path = "/secure/contacts.json"
    )");

    auto configOpt = LoadConfig(testConfigPath);
    ASSERT_TRUE(configOpt.has_value());

    EXPECT_EQ(configOpt->networkConfig.torSocksHost, "192.168.1.10");
    EXPECT_EQ(configOpt->networkConfig.torSocksPort, 9052);
    EXPECT_EQ(configOpt->obfuscationConfig.mode, "poisson");
    EXPECT_EQ(configOpt->obfuscationConfig.cbr_interval_ms, 1000);
    EXPECT_FLOAT_EQ(configOpt->obfuscationConfig.poissonLambda, 3.14F);
    EXPECT_EQ(configOpt->storageConfig.contactsFilePath, "/secure/contacts.json");
}

TEST_F(ClientConfigTest, FailsSecurelyOnMissingStorageConfig)
{
    WriteConfig(R"(
        [network]
        tor_socks_host = "127.0.0.1"
        tor_socks_port = 9050

        [obfuscation]
        mode = "cbr"
        cbr_interval_ms = 5000
        poisson_lambda = 5.0
    )");

    auto configOpt = LoadConfig(testConfigPath);
    EXPECT_FALSE(configOpt.has_value());
}

TEST_F(ClientConfigTest, FailsSecurelyOnMissingNetworkFields)
{
    WriteConfig(R"(
        [network]
        tor_socks_host = "127.0.0.1"

        [obfuscation]
        mode = "cbr"
        cbr_interval_ms = 5000
        poisson_lambda = 5.0

        [storage]
        contacts_file_path = "contacts.json"
    )");

    auto configOpt = LoadConfig(testConfigPath);
    EXPECT_FALSE(configOpt.has_value()) << "Must fail if tor_socks_port is omitted";
}

TEST_F(ClientConfigTest, FailsSecurelyOnMissingObfuscationFields)
{
    WriteConfig(R"(
        [network]
        tor_socks_host = "127.0.0.1"
        tor_socks_port = 9050

        [obfuscation]
        mode = "cbr"
        cbr_interval_ms = 5000

        [storage]
        contacts_file_path = "contacts.json"
    )");

    auto configOpt = LoadConfig(testConfigPath);
    EXPECT_FALSE(configOpt.has_value()) << "Must fail if obfuscation parameters are omitted";
}

} // namespace bc::domain::client::test
