#include "crypto/placeholder.h"
#include "network/placeholder.h"
#include "protocol/placeholder.h"

#include <gtest/gtest.h>

TEST(CryptoModuleTest, ReturnsCorrectStatus)
{
    EXPECT_EQ(blank_chat::crypto::GetCryptoStatus(), "Crypto OK");
}

TEST(NetworkModuleTest, ReturnsCorrectStatus)
{
    EXPECT_EQ(blank_chat::network::GetNetworkStatus(), "Network OK");
}

TEST(ProtocolModuleTest, ReturnsCorrectStatus)
{
    EXPECT_EQ(blank_chat::protocol::GetProtocolStatus(), "Protocol OK");
}
