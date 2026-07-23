// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.

#include <gtest/gtest.h>

#include "ui/responsive.h"

namespace {

TEST(ResponsiveTest, ColumnOffsetsDistributeByWeights) {
    const int weights[] = {1, 2, 1};
    int out[] = {-1, -1, -1};

    sigurdos::responsive::column_offsets(weights, 400, out);

    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[1], 100);
    EXPECT_EQ(out[2], 300);
}

TEST(ResponsiveTest, ColumnOffsetsApplyStartOffset) {
    const int weights[] = {1, 1, 2};
    int out[] = {-1, -1, -1};

    sigurdos::responsive::column_offsets(weights, 200, out, 12);

    EXPECT_EQ(out[0], 12);
    EXPECT_EQ(out[1], 62);
    EXPECT_EQ(out[2], 112);
}

TEST(ResponsiveTest, ColumnOffsetsZeroWidthUsesStartOffset) {
    const int weights[] = {1, 2, 3};
    int out[] = {-1, -1, -1};

    sigurdos::responsive::column_offsets(weights, 0, out, 7);

    EXPECT_EQ(out[0], 7);
    EXPECT_EQ(out[1], 7);
    EXPECT_EQ(out[2], 7);
}

TEST(ResponsiveTest, ColumnOffsetsZeroWeightsUseStartOffset) {
    const int weights[] = {0, 0, 0};
    int out[] = {-1, -1, -1};

    sigurdos::responsive::column_offsets(weights, 120, out, 5);

    EXPECT_EQ(out[0], 5);
    EXPECT_EQ(out[1], 5);
    EXPECT_EQ(out[2], 5);
}

TEST(ResponsiveTest, ColumnOffsetsTreatNegativeWeightsAsZero) {
    const int weights[] = {1, -2, 1};
    int out[] = {-1, -1, -1};

    sigurdos::responsive::column_offsets(weights, 200, out);

    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[1], 100);
    EXPECT_EQ(out[2], 100);
}

TEST(ResponsiveTest, GeometrySupportsPortraitRotation)
{
    const auto geometry = sigurdos::responsive::geometry_for(240, 320);
    EXPECT_TRUE(geometry.portrait);
    EXPECT_EQ(geometry.width, 240);
    EXPECT_EQ(geometry.height, 320);
    EXPECT_GT(geometry.content_h, geometry.content_w);
}

TEST(ResponsiveTest, GeometryKeepsReducedResolutionContentPositive)
{
    const auto geometry = sigurdos::responsive::geometry_for(160, 128);
    EXPECT_FALSE(geometry.portrait);
    EXPECT_EQ(geometry.top_bar_h, 12);
    EXPECT_GT(geometry.content_w, 0);
    EXPECT_GT(geometry.content_h, 0);
}

} // namespace
