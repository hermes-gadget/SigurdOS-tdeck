#include <gtest/gtest.h>

#include "mesh/path_codec.h"

namespace path = sigurdos::mesh::path;

TEST(PathCodec, DecodesAllSupportedHashModes)
{
    EXPECT_TRUE(path::encodedLengthValid(0x3F));
    EXPECT_EQ(path::hashCount(0x3F), 63);
    EXPECT_EQ(path::hashSize(0x3F), 1);
    EXPECT_EQ(path::byteCount(0x3F), 63U);

    EXPECT_TRUE(path::encodedLengthValid(0x60));
    EXPECT_EQ(path::hashCount(0x60), 32);
    EXPECT_EQ(path::hashSize(0x60), 2);
    EXPECT_EQ(path::byteCount(0x60), 64U);

    EXPECT_TRUE(path::encodedLengthValid(0x95));
    EXPECT_EQ(path::hashCount(0x95), 21);
    EXPECT_EQ(path::hashSize(0x95), 3);
    EXPECT_EQ(path::byteCount(0x95), 63U);
}

TEST(PathCodec, RejectsOversizedAndReservedEncodings)
{
    EXPECT_FALSE(path::encodedLengthValid(0x61));
    EXPECT_FALSE(path::encodedLengthValid(0x96));
    EXPECT_FALSE(path::encodedLengthValid(0xC0));
    EXPECT_FALSE(path::encodedLengthValid(0xFF));
    EXPECT_TRUE(path::storedLengthValid(path::UNKNOWN));
}
