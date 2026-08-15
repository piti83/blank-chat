#include <gtest/gtest.h>
#include <sodium.h>

#include <client/obfuscation_timer.h>

namespace bc::domain::client::test {

class ObfuscationTimerTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (sodium_init() < 0) {
            std::abort();
        }
    }
};

TEST_F(ObfuscationTimerTest, CbrTimerReturnsConstantValue)
{
    CbrTimer timer(5000);
    EXPECT_EQ(timer.GetNextInterval().count(), 5000);
    EXPECT_EQ(timer.GetNextInterval().count(), 5000);
}

TEST_F(ObfuscationTimerTest, PoissonTimerRespectsLowerBoundClamp)
{
    PoissonTimer timer(100.0F);
    for (int i = 0; i < 1000; ++i) {
        auto interval = timer.GetNextInterval();
        EXPECT_GE(interval.count(), 100);
    }
}

TEST_F(ObfuscationTimerTest, FactoryCreatesCorrectInstance)
{
    auto t1 = CreateObfuscationTimer("poisson", 1000, 2.0F);
    ASSERT_NE(t1, nullptr);

    EXPECT_GE(t1->GetNextInterval().count(), 100);

    auto t2 = CreateObfuscationTimer("cbr", 1000, 2.0F);
    ASSERT_NE(t2, nullptr);

    EXPECT_EQ(t2->GetNextInterval().count(), 1000);
}

} // namespace bc::domain::client::test
