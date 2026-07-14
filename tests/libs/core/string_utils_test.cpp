#include <vector>

#include <gtest/gtest.h>

#include <core/string_utils.h>

namespace bc::core::test {

TEST(StringUtilsTest, DecodeHexToArraySucceedsOnValidInput)
{
    std::string_view hex = "deadbeef42";
    std::vector<std::uint8_t> out(5);

    EXPECT_TRUE(DecodeHexToArray(hex, out));
    EXPECT_EQ(out, (std::vector<std::uint8_t>{0xDE, 0xAD, 0xBE, 0xEF, 0x42}));
}

TEST(StringUtilsTest, DecodeHexToArrayFailsOnInvalidLength)
{
    std::string_view hex = "123";
    std::vector<std::uint8_t> out(2);

    EXPECT_FALSE(DecodeHexToArray(hex, out)) << "Should fail on odd length string.";
}

TEST(StringUtilsTest, DecodeHexToArrayFailsOnInvalidCharacters)
{
    std::string_view hex = "123x";
    std::vector<std::uint8_t> out(2);

    EXPECT_FALSE(DecodeHexToArray(hex, out)) << "Should fail on non-hex characters.";
}

TEST(StringUtilsTest, EncodeHexProducesCorrectString)
{
    std::vector<std::uint8_t> data = {0x01, 0x10, 0xAA, 0xFF};
    EXPECT_EQ(EncodeHex(data), "0110aaff");
}

TEST(StringUtilsTest, EscapeJsonStringSafelyEscapesMaliciousInput)
{
    std::string_view malicious = "Alice says:\n\t\"Hello \\ World\"\b\f\r";
    std::string escaped = EscapeJsonString(malicious);

    EXPECT_EQ(escaped, "Alice says:\\n\\t\\\"Hello \\\\ World\\\"\\b\\f\\r");
}

TEST(StringUtilsTest, HashPayloadGeneratesConsistentBlake2bHashes)
{
    std::vector<std::uint8_t> payload = {'S', 'e', 'c', 'r', 'e', 't'};

    std::string hash1 = HashPayload(payload);
    std::string hash2 = HashPayload(payload);

    EXPECT_EQ(hash1.length(), 32) << "Hash output should be exactly 32 hex characters (16 bytes).";
    EXPECT_EQ(hash1, hash2);

    std::vector<std::uint8_t> diffPayload = {'s', 'e', 'c', 'r', 'e', 't'};
    EXPECT_NE(hash1, HashPayload(diffPayload));
}

} // namespace bc::core::test
