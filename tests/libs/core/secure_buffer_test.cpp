#include <gtest/gtest.h>

#include <core/secure_buffer.h>

namespace bc::core {

class SecureBufferTest : public ::testing::Test
{
protected:
    static auto IsMemoryFilledWith(std::span<const BufferType> span, BufferType value) -> bool
    {
        for (auto byte : span) {
            if (byte != value) {
                return false;
            }
        }
        return true;
    }
};

TEST_F(SecureBufferTest, DefaultConstructorCreatesEmptyBuffer)
{
    SecureBuffer buf;
    EXPECT_TRUE(buf.IsEmpty());
    EXPECT_EQ(buf.Size(), 0);
    EXPECT_EQ(buf.Data(), nullptr);

    EXPECT_TRUE(buf.AsSpan().empty());
    EXPECT_TRUE(buf.AsMutableSpan().empty());
}

TEST_F(SecureBufferTest, SizeConstructorAllocatesZeroInitializedMemory)
{
    constexpr std::size_t testSize = 256;
    SecureBuffer buf(testSize);

    EXPECT_FALSE(buf.IsEmpty());
    EXPECT_EQ(buf.Size(), testSize);
    ASSERT_NE(buf.Data(), nullptr);

    EXPECT_TRUE(IsMemoryFilledWith(buf.AsSpan(), 0x00));
}

TEST_F(SecureBufferTest, SizeConstructorHandlesZeroSizeSafely)
{
    SecureBuffer buf(0);
    EXPECT_TRUE(buf.IsEmpty());
    EXPECT_EQ(buf.Size(), 0);
    EXPECT_EQ(buf.Data(), nullptr);
}

TEST_F(SecureBufferTest, MoveConstructorTransfersOwnershipAndNullifiesSource)
{
    constexpr std::size_t testSize = 64;
    SecureBuffer source(testSize);
    source.AsMutableSpan()[0] = 0xAA;
    source.AsMutableSpan()[testSize - 1] = 0xBB;

    const auto* originalPtr = source.Data();

    SecureBuffer destination(std::move(source));

    EXPECT_FALSE(destination.IsEmpty());
    EXPECT_EQ(destination.Size(), testSize);
    EXPECT_EQ(destination.Data(), originalPtr);
    EXPECT_EQ(destination.AsSpan()[0], 0xAA);
    EXPECT_EQ(destination.AsSpan()[testSize - 1], 0xBB);

    EXPECT_TRUE(source.IsEmpty());
    EXPECT_EQ(source.Size(), 0);
    EXPECT_EQ(source.Data(), nullptr);
}

TEST_F(SecureBufferTest, MoveAssignmentTransfersOwnershipAndCleansUpTarget)
{
    SecureBuffer source(32);
    source.AsMutableSpan()[0] = 0xFF;
    const auto* originalSourcePtr = source.Data();

    SecureBuffer target(16);
    target.AsMutableSpan()[0] = 0xEE;

    target = std::move(source);

    EXPECT_EQ(target.Size(), 32);
    EXPECT_EQ(target.Data(), originalSourcePtr);
    EXPECT_EQ(target.AsSpan()[0], 0xFF);

    EXPECT_TRUE(source.IsEmpty());
    EXPECT_EQ(source.Size(), 0);
    EXPECT_EQ(source.Data(), nullptr);
}

TEST_F(SecureBufferTest, MoveAssignmentSelfAssignmentIsSafe)
{
    constexpr std::size_t testSize = 42;
    SecureBuffer buf(testSize);
    buf.AsMutableSpan()[0] = 0xDD;

    const auto* originalPtr = buf.Data();

    SecureBuffer& ref = buf;
    buf = std::move(ref);

    EXPECT_EQ(buf.Size(), testSize);
    EXPECT_EQ(buf.Data(), originalPtr);
    EXPECT_EQ(buf.AsSpan()[0], 0xDD);
}

TEST_F(SecureBufferTest, ConstCorrectnessAndSpanAccess)
{
    SecureBuffer buf(10);

    auto mutSpan = buf.AsMutableSpan();
    EXPECT_EQ(mutSpan.size(), 10);
    mutSpan[5] = 0x42;

    const SecureBuffer& constBuf = buf;
    auto constSpan = constBuf.AsSpan();

    EXPECT_EQ(constSpan.size(), 10);
    EXPECT_EQ(constSpan[5], 0x42);
    EXPECT_EQ(constBuf.Data()[5], 0x42);

    static_assert(std::is_same_v<decltype(constSpan), std::span<const BufferType>>);
    static_assert(std::is_same_v<decltype(mutSpan), std::span<BufferType>>);
}

} // namespace bc::core
