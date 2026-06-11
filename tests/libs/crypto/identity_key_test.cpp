#include <gtest/gtest.h>

#include <crypto/identity_key.h>

namespace bc::crypto::test {

TEST(IdentityKeyTest, GenerateCreatesValidKeypair)
{
    auto key = IdentityKey::Generate();
    EXPECT_EQ(key.GetPublicKey().size(), 32);
    EXPECT_EQ(key.GetSecretKeySpan().size(), 64);
}

TEST(IdentityKeyTest, CanConvertToCurve25519)
{
    auto key = IdentityKey::Generate();
    auto curvePk = key.GetCurve25519PublicKey();
    auto curveSk = key.GetCurve25519SecretKey();

    ASSERT_TRUE(curvePk.has_value());
    ASSERT_TRUE(curveSk.has_value());
    EXPECT_EQ(curvePk->size(), 32);
    EXPECT_EQ(curveSk->Size(), 32);
}

TEST(IdentityKeyTest, FailsSecurelyOnInvalidEd25519Point)
{
    auto key = IdentityKey::Generate();

    // High Assurance Check: We intentionally corrupt the public key memory
    // to verify that sodium rejects points not located on the elliptic curve.
    auto* pkData = const_cast<std::uint8_t*>(key.GetPublicKey().data());
    std::fill(pkData, pkData + 32, 0xFF);

    auto curvePk = key.GetCurve25519PublicKey();
    EXPECT_FALSE(curvePk.has_value());
}

TEST(IdentityKeyTest, Curve25519SecretKeyDerivationIsDeterministic)
{
    auto key = IdentityKey::Generate();

    // High Assurance Check: Ensure the scalar derivation is deterministic
    // and doesn't rely on uninitialized memory or state.
    auto curveSk1 = key.GetCurve25519SecretKey();
    auto curveSk2 = key.GetCurve25519SecretKey();

    ASSERT_TRUE(curveSk1.has_value());
    ASSERT_TRUE(curveSk2.has_value());

    auto span1 = curveSk1->AsSpan();
    auto span2 = curveSk2->AsSpan();

    EXPECT_TRUE(std::equal(span1.begin(), span1.end(), span2.begin()));
}

} // namespace bc::crypto::test
