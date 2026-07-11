#include <gtest/gtest.h>

#include "mesh/control_parser.h"

using sigurdos::mesh::parse_bounded_int;

TEST(ControlParser, ParsesExactLengthWithoutTerminator) {
    const uint8_t data[] = {'-', '1', '2', '3', '9'};
    int value = 0;
    EXPECT_TRUE(parse_bounded_int(data, sizeof(data), &value));
    EXPECT_EQ(value, -1239);
}

TEST(ControlParser, AcceptsSignsAndIntegerLimits) {
    int value = 0;
    const uint8_t plus[] = {'+', '7'};
    const uint8_t max[] = {'2','1','4','7','4','8','3','6','4','7'};
    const uint8_t min[] = {'-','2','1','4','7','4','8','3','6','4','8'};
    EXPECT_TRUE(parse_bounded_int(plus, sizeof(plus), &value));
    EXPECT_EQ(value, 7);
    EXPECT_TRUE(parse_bounded_int(max, sizeof(max), &value));
    EXPECT_EQ(value, INT_MAX);
    EXPECT_TRUE(parse_bounded_int(min, sizeof(min), &value));
    EXPECT_EQ(value, INT_MIN);
}

TEST(ControlParser, RejectsMalformedOrOverflowingFields) {
    int value = 42;
    const uint8_t sign[] = {'-'};
    const uint8_t junk[] = {'1', '2', '\0', '3'};
    const uint8_t overflow[] = {'2','1','4','7','4','8','3','6','4','8'};
    EXPECT_FALSE(parse_bounded_int(nullptr, 1, &value));
    EXPECT_FALSE(parse_bounded_int(sign, sizeof(sign), &value));
    EXPECT_FALSE(parse_bounded_int(junk, sizeof(junk), &value));
    EXPECT_FALSE(parse_bounded_int(overflow, sizeof(overflow), &value));
}

TEST(ControlParser, NeverConsumesBytesPastTheSlice) {
    const uint8_t data[] = {'9', '9', 'x'};
    int value = 0;
    EXPECT_TRUE(parse_bounded_int(data, 2, &value));
    EXPECT_EQ(value, 99);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
