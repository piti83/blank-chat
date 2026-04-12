#include "utils/uuid_constants.h"
#include "utils/uuid_factory.h"

#include <gtest/gtest.h>
#include <unordered_set>

using namespace bc::utils;

TEST(UuidFactoryTest, GeneratesCorrectV4VersionAndVariant)
{
    auto uuid = UuidFactory::GenerateUuidV4();

    EXPECT_EQ(uuid[kVersionByteIndex] & 0xF0, 0x40);
    EXPECT_EQ(uuid[kVariantByteIndex] & 0xC0, 0x80);
}

TEST(UuidFactoryTest, GeneratesUniqueUuids)
{
    std::unordered_set<std::string> generatedUuids;
    constexpr int kIterations = 10000;

    for (int i = 0; i < kIterations; ++i) {
        auto uuid = UuidFactory::GenerateUuidV4();
        std::string uuidStr(uuid.begin(), uuid.end());

        auto [iter, inserted] = generatedUuids.insert(uuidStr);
        EXPECT_TRUE(inserted);
    }
}
