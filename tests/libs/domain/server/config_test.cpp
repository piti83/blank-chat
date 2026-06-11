#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <server/config.h>

namespace bc::domain::server::test {

class ServerConfigTest : public ::testing::Test
{
protected:
    std::string testConfigPath = "test_server_config.toml";

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

TEST_F(ServerConfigTest, SuccessfullyParsesValidConfiguration)
{
    WriteConfig(R"(
        [network]
        listen_host = "0.0.0.0"
        listen_port = 4433
        tor_control_host = "127.0.0.1"
        tor_control_port = 9051

        [security]
        memory_quota_percent = 60
        max_messages_per_mailbox = 100
    )");

    auto configOpt = LoadConfig(testConfigPath);
    ASSERT_TRUE(configOpt.has_value());

    EXPECT_EQ(configOpt->networkConfig.listenHost, "0.0.0.0");
    EXPECT_EQ(configOpt->networkConfig.listenPort, 4433);
    EXPECT_EQ(configOpt->securityConfig.memoryQuotaPercent, 60);
    EXPECT_EQ(configOpt->securityConfig.maxMessagesPerMailbox, 100);
}

TEST_F(ServerConfigTest, FailsSecurelyOnMissingFile)
{
    auto configOpt = LoadConfig("non_existent_config_file_random_name.toml");
    EXPECT_FALSE(configOpt.has_value());
}

TEST_F(ServerConfigTest, FailsSecurelyOnMalformedToml)
{
    WriteConfig("[network]\nlisten_host = \"127.0.0.1\"\nINVALID_SYNTAX_HERE");
    auto configOpt = LoadConfig(testConfigPath);
    EXPECT_FALSE(configOpt.has_value());
}

TEST_F(ServerConfigTest, FailsSecurelyOnMissingRequiredNetworkFields)
{
    WriteConfig(R"(
        [network]
        listen_host = "127.0.0.1"
        tor_control_host = "127.0.0.1"
        tor_control_port = 9051

        [security]
        memory_quota_percent = 80
        max_messages_per_mailbox = 50
    )");

    auto configOpt = LoadConfig(testConfigPath);
    EXPECT_FALSE(configOpt.has_value());
}

TEST_F(ServerConfigTest, FailsSecurelyOnTypeMismatch)
{
    WriteConfig(R"(
        [network]
        listen_host = "127.0.0.1"
        listen_port = 8080
        tor_control_host = "127.0.0.1"
        tor_control_port = 9051

        [security]
        memory_quota_percent = "Eighty"
        max_messages_per_mailbox = 50
    )");

    auto configOpt = LoadConfig(testConfigPath);
    EXPECT_FALSE(configOpt.has_value());
}

} // namespace bc::domain::server::test
