#include <gtest/gtest.h>

#include <core/secure_string.h>

namespace bc::core::test {

class SecureStringTest : public ::testing::Test
{
};

TEST_F(SecureStringTest, ConstructorAllocatesAndZeroInitializesSafely)
{
    SecureString str(10);
    EXPECT_TRUE(str.StringView().empty());
    EXPECT_EQ(str.StringView().size(), 0);
}

TEST_F(SecureStringTest, PushBackEnforcesCapacityAndNullTerminates)
{
    SecureString str(5);

    str.PushBack('T');
    str.PushBack('e');
    str.PushBack('s');
    str.PushBack('t');

    EXPECT_EQ(str.StringView(), "Test");
    EXPECT_EQ(str.StringView().size(), 4);

    str.PushBack('X');
    EXPECT_EQ(str.StringView(), "Test");
}

TEST_F(SecureStringTest, MoveConstructorTransfersOwnershipAndClearsSource)
{
    SecureString source(256);
    source.PushBack('A');
    source.PushBack('B');

    SecureString destination(std::move(source));

    EXPECT_EQ(destination.StringView(), "AB");

    EXPECT_TRUE(source.StringView().empty());
}

TEST_F(SecureStringTest, MoveAssignmentCleansUpTargetAndTransfersSource)
{
    SecureString source(10);
    source.PushBack('S');

    SecureString target(10);
    target.PushBack('T');

    target = std::move(source);

    EXPECT_EQ(target.StringView(), "S");

    EXPECT_TRUE(source.StringView().empty());
}

TEST_F(SecureStringTest, SelfAssignmentIsHandledSafely)
{
    SecureString str(10);
    str.PushBack('X');

    SecureString& ref = str;
    str = std::move(ref);

    EXPECT_EQ(str.StringView(), "X");
}

} // namespace bc::core::test
