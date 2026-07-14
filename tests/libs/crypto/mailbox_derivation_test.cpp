#include <gtest/gtest.h>

#include <crypto/mailbox_derivation.h>

namespace bc::crypto::test {

TEST(MailboxDerivationTest, DerivesSymmetricMailboxesProperly)
{
    auto alice = IdentityKey::Generate();
    auto bob = IdentityKey::Generate();

    auto aliceDerived = DerivePairwiseMailboxes(alice, bob.GetPublicKey());
    auto bobDerived = DerivePairwiseMailboxes(bob, alice.GetPublicKey());

    ASSERT_TRUE(aliceDerived.has_value());
    ASSERT_TRUE(bobDerived.has_value());

    EXPECT_EQ(aliceDerived->txId, bobDerived->rxId);
    EXPECT_EQ(aliceDerived->rxId, bobDerived->txId);

    EXPECT_NE(aliceDerived->txId, aliceDerived->rxId);
}

TEST(MailboxDerivationTest, FailsSecurelyOnInvalidPeerKey)
{
    auto alice = IdentityKey::Generate();

    PublicKeyType maliciousBobKey{};
    maliciousBobKey.fill(0xFF);

    auto aliceDerived = DerivePairwiseMailboxes(alice, maliciousBobKey);
    EXPECT_FALSE(aliceDerived.has_value());
}

} // namespace bc::crypto::test
