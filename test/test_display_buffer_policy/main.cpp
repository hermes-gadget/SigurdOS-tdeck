#include <gtest/gtest.h>

#include "hal/display_buffer_policy.h"

using sigurdos::hal::DisplayBufferMode;
using sigurdos::hal::select_display_buffer_mode;

TEST(DisplayBufferPolicy, CoversAllocationFailureMatrix) {
    EXPECT_EQ(select_display_buffer_mode(true, true, true, true),
              DisplayBufferMode::FullDouble);
    EXPECT_EQ(select_display_buffer_mode(true, false, true, true),
              DisplayBufferMode::FullSingle);
    EXPECT_EQ(select_display_buffer_mode(false, false, true, true),
              DisplayBufferMode::Partial);
    EXPECT_EQ(select_display_buffer_mode(false, false, false, true),
              DisplayBufferMode::Emergency);
    EXPECT_EQ(select_display_buffer_mode(false, false, false, false),
              DisplayBufferMode::Failed);
}

TEST(DisplayBufferPolicy, SecondOnlyAllocationCannotClaimFullMode) {
    EXPECT_EQ(select_display_buffer_mode(false, true, false, false),
              DisplayBufferMode::Failed);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
