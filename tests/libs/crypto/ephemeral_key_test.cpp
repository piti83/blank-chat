#include <gtest/gtest.h>
#include <sodium.h>

#include <crypto/ephemeral_key.h>

namespace bc::crypto::test {

TEST(EphemeralKeyTest, GenerateCreatesValidKeypair)
{
    auto keyOpt = EphemeralKey::Generate();
    ASSERT_TRUE(keyOpt.has_value());

    EXPECT_EQ(keyOpt->GetPublicKey().size(), crypto_kx_PUBLICKEYBYTES);
    EXPECT_EQ(keyOpt->GetSecretKeySpan().size(), crypto_kx_SECRETKEYBYTES);
}

TEST(EphemeralKeyTest, MoveConstructorTransfersOwnership)
{
    auto keyOpt1 = EphemeralKey::Generate();
    ASSERT_TRUE(keyOpt1.has_value());

    auto originalPk = keyOpt1->GetPublicKey();

    EphemeralKey key2 = std::move(*keyOpt1);

    EXPECT_EQ(key2.GetPublicKey(), originalPk);
    EXPECT_EQ(key2.GetSecretKeySpan().size(), crypto_kx_SECRETKEYBYTES);
}

TEST(EphemeralKeyTest, MoveAssignmentTransfersOwnership)
{
    auto keyOpt1 = EphemeralKey::Generate();
    auto keyOpt2 = EphemeralKey::Generate();
    ASSERT_TRUE(keyOpt1.has_value());
    ASSERT_TRUE(keyOpt2.has_value());

    auto originalPk = keyOpt1->GetPublicKey();

    *keyOpt2 = std::move(*keyOpt1);

    EXPECT_EQ(keyOpt2->GetPublicKey(), originalPk);
    EXPECT_EQ(keyOpt2->GetSecretKeySpan().size(), crypto_kx_SECRETKEYBYTES);
}

} // namespace bc::crypto::test
