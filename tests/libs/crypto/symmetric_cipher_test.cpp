#include <gtest/gtest.h>
#include <sodium.h>

#include <crypto/crypto_types.h>
#include <crypto/symmetric_cipher.h>

namespace bc::crypto::test {

class SymmetricCipherTest : public ::testing::Test
{
protected:
    std::vector<std::uint8_t> validKey;
    std::vector<std::uint8_t> validPlaintext;

    void SetUp() override
    {
        validKey.resize(keySize, 0xAA);
        validPlaintext = {'S', 'e', 'c', 'r', 'e', 't', ' ', 'D', 'a', 't', 'a'};
    }
};

TEST_F(SymmetricCipherTest, EncryptAndDecryptRoundtripSucceeds)
{
    auto ciphertextOpt = SymmetricCipher::EncryptWithPadding(validKey, validPlaintext);
    ASSERT_TRUE(ciphertextOpt.has_value());
    EXPECT_GT(ciphertextOpt->size(), validPlaintext.size());

    auto plaintextOpt = SymmetricCipher::DecryptAndUnpad(validKey, *ciphertextOpt);
    ASSERT_TRUE(plaintextOpt.has_value());
    EXPECT_EQ(*plaintextOpt, validPlaintext);
}

TEST_F(SymmetricCipherTest, FailsSecurelyOnInvalidKeySize)
{
    std::vector<std::uint8_t> invalidKey(16, 0xBB);

    auto ciphertextOpt = SymmetricCipher::EncryptWithPadding(invalidKey, validPlaintext);
    EXPECT_FALSE(ciphertextOpt.has_value());

    std::vector<std::uint8_t> dummyCiphertext(100, 0xCC);
    auto plaintextOpt = SymmetricCipher::DecryptAndUnpad(invalidKey, dummyCiphertext);
    EXPECT_FALSE(plaintextOpt.has_value());
}

TEST_F(SymmetricCipherTest, RejectsTamperedCiphertext)
{
    auto ciphertextOpt = SymmetricCipher::EncryptWithPadding(validKey, validPlaintext);
    ASSERT_TRUE(ciphertextOpt.has_value());

    (*ciphertextOpt)[ciphertextOpt->size() / 2] ^= 0x01;

    auto plaintextOpt = SymmetricCipher::DecryptAndUnpad(validKey, *ciphertextOpt);
    EXPECT_FALSE(plaintextOpt.has_value()) << "Poly1305 MAC should reject tampered data";
}

TEST_F(SymmetricCipherTest, RejectsInvalidPaddingStructure)
{
    std::vector<std::uint8_t> fakePadded(torCellPayloadSize - totalFixedOverhead, 0x00);

    std::vector<std::uint8_t> outBuffer(nonceSize + fakePadded.size() + macSize);
    randombytes_buf(outBuffer.data(), nonceSize);
    unsigned long long ciphertextLen = 0;

    crypto_aead_xchacha20poly1305_ietf_encrypt(outBuffer.data() + nonceSize, &ciphertextLen,
                                               fakePadded.data(), fakePadded.size(), nullptr, 0,
                                               nullptr, outBuffer.data(), validKey.data());

    auto plaintextOpt = SymmetricCipher::DecryptAndUnpad(validKey, outBuffer);
    EXPECT_FALSE(plaintextOpt.has_value()) << "Decryption must fail if padding marker is missing";
}

} // namespace bc::crypto::test
