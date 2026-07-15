// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Layout regression tests for fixed 320x240 T-Deck screens.

#include <gtest/gtest.h>
#include "ui/responsive.h"

namespace {

using namespace sigurdos::responsive;

constexpr int kStackGap = 4;
constexpr int kFooterBase = BOT_BAR_H + DIVIDER_H;

constexpr int top_for(int row_h, int offset) {
    return bottom_aligned_top(row_h, offset);
}

constexpr int bottom_for(int row_h, int offset) {
    return top_for(row_h, offset) + row_h;
}

static void expect_separated(int lower_h, int lower_offset, int upper_h, int upper_offset) {
    EXPECT_LE(bottom_for(upper_h, upper_offset) + kStackGap, top_for(lower_h, lower_offset));
}

TEST(LayoutTest, ContentAreaFitsInsideDisplay) {
    EXPECT_EQ(DISPLAY_W, 320);
    EXPECT_EQ(DISPLAY_H, 240);
    EXPECT_TRUE(IS_LANDSCAPE);
    EXPECT_FALSE(IS_PORTRAIT);
    EXPECT_FALSE(IS_SQUARE);

    EXPECT_GE(CONTENT_X, 0);
    EXPECT_GE(CONTENT_Y, TOP_BAR_H);
    EXPECT_GT(CONTENT_W, 0);
    EXPECT_GT(CONTENT_H, 0);
    EXPECT_LE(CONTENT_X + CONTENT_W, DISPLAY_W);
    EXPECT_LE(CONTENT_Y + CONTENT_H + BOT_BAR_H + DIVIDER_H, DISPLAY_H);
}

TEST(LayoutTest, BarHeightsStayReadableAndCompact) {
    EXPECT_GE(TOP_BAR_H, 12);
    EXPECT_LE(TOP_BAR_H, 28);
    EXPECT_EQ(BOT_BAR_H, TOP_BAR_H - 2);
    EXPECT_GT(BOT_BAR_H, 0);
}

TEST(LayoutTest, TDeckGridUsesFourColumns) {
    GridLayout layout = compute_grid();
    EXPECT_EQ(layout.cols, 4);
}

TEST(LayoutTest, HashtagLabelLeavesRoomForControls) {
    EXPECT_EQ(HASHTAG_LABEL_W(), DISPLAY_W - 60);
    EXPECT_GT(HASHTAG_LABEL_W(), CONTENT_W / 2);
    EXPECT_LT(HASHTAG_LABEL_W(), DISPLAY_W);
}

TEST(LayoutTest, DialogSizeCapsToDisplayMargins) {
    DialogSize capped = dialog_size(1000, 1000);
    EXPECT_EQ(capped.w, DISPLAY_W - 32);
    EXPECT_EQ(capped.h, DISPLAY_H - 32);

    DialogSize unchanged = dialog_size(120, 80);
    EXPECT_EQ(unchanged.w, 120);
    EXPECT_EQ(unchanged.h, 80);
}

TEST(LayoutTest, CappedWidthUsesPercentLimit) {
    EXPECT_EQ(capped_width(1000), (DISPLAY_W * 90) / 100);
    EXPECT_EQ(capped_width(120), 120);
    EXPECT_EQ(capped_width(1000, 50), DISPLAY_W / 2);
}

TEST(LayoutTest, BottomStackOffsetUsesRowHeightAndGap) {
    EXPECT_EQ(bottom_stack_offset(0), 0);
    EXPECT_EQ(bottom_stack_offset(1), 34);
    EXPECT_EQ(bottom_stack_offset(2), 68);
    EXPECT_EQ(bottom_stack_offset(2, 26, 6), 64);
}

TEST(LayoutTest, BottomAlignedRowsStayAboveFooter) {
    constexpr int row_h = 30;
    constexpr int offset = bottom_stack_offset(1);
    constexpr int row_top = bottom_aligned_top(row_h, offset);
    constexpr int row_bottom = row_top + row_h;

    EXPECT_GT(row_top, CONTENT_Y);
    EXPECT_LE(row_bottom + offset + kFooterBase, DISPLAY_H);
}

TEST(LayoutTest, BarWidgetHeightScalesFromContentArea) {
    EXPECT_EQ(bar_widget_h(50), CONTENT_H / 2);
    EXPECT_EQ(bar_widget_h(100), CONTENT_H);
    EXPECT_GT(bar_widget_h(), 0);
    EXPECT_LT(bar_widget_h(), CONTENT_H);
}

TEST(LayoutTest, ColumnOffsetsDistributeWeightedColumns) {
    int weights[3] = {1, 1, 2};
    int offsets[3] = {};

    column_offsets(weights, 100, offsets, 10);

    EXPECT_EQ(offsets[0], 10);
    EXPECT_EQ(offsets[1], 35);
    EXPECT_EQ(offsets[2], 60);
}

TEST(LayoutTest, ContactDetailNonRoomRowsDoNotOverlap) {
    constexpr bool is_room = false;
    constexpr bool logged_in = false;

    constexpr int list_bottom = CONTENT_Y + CONTENT_H - contact_bottom_reserved(is_room, logged_in);
    EXPECT_LE(list_bottom + 2, top_for(22, contact_acl_offset(is_room, logged_in)));

    expect_separated(26, contact_qr_offset(is_room, logged_in), 22, contact_acl_offset(is_room, logged_in));
    expect_separated(30, 64, 26, contact_qr_offset(is_room, logged_in));      // reset/discover row
    expect_separated(26, 34, 30, 64);                                         // telemetry row
    expect_separated(30, 0, 26, 34);                                          // main action row
}

TEST(LayoutTest, ContactDetailRoomLoggedOutRowsDoNotOverlap) {
    constexpr bool is_room = true;
    constexpr bool logged_in = false;

    constexpr int list_bottom = CONTENT_Y + CONTENT_H - contact_bottom_reserved(is_room, logged_in);
    EXPECT_LE(list_bottom + 2, top_for(22, contact_acl_offset(is_room, logged_in)));

    expect_separated(26, contact_qr_offset(is_room, logged_in), 22, contact_acl_offset(is_room, logged_in));
    expect_separated(30, 34, 26, contact_qr_offset(is_room, logged_in));       // login row
    expect_separated(30, 0, 30, 34);                                          // main action row
}

TEST(LayoutTest, ContactDetailRoomLoggedInRowsDoNotOverlap) {
    constexpr bool is_room = true;
    constexpr bool logged_in = true;

    constexpr int list_bottom = CONTENT_Y + CONTENT_H - contact_bottom_reserved(is_room, logged_in);
    EXPECT_LE(list_bottom + 2, top_for(22, contact_acl_offset(is_room, logged_in)));

    expect_separated(26, contact_qr_offset(is_room, logged_in), 22, contact_acl_offset(is_room, logged_in));
    expect_separated(30, 68, 26, contact_qr_offset(is_room, logged_in));       // fetch row
    expect_separated(30, 34, 30, 68);                                         // login/admin row
    expect_separated(30, 0, 30, 34);                                          // main action row
}

TEST(LayoutTest, ConfiguredSignalChartDoesNotOverlapMetrics) {
    constexpr int metrics_y = CONTENT_Y + 4;
    constexpr int metrics_pad = 4;
    constexpr int font10_line_h = 11;
    constexpr int metrics_lines = 6;
    constexpr int metrics_bottom = metrics_y + metrics_pad * 2 + font10_line_h * metrics_lines;

    constexpr int chart_y = CONTENT_Y + 4 + 112;
    constexpr int chart_h = 60;
    constexpr int label_h = 15;  // lv_font_montserrat_12 line_height
    constexpr int chart_label_top = chart_y - 2 - label_h;

    EXPECT_LE(metrics_bottom + kStackGap, chart_label_top);
    EXPECT_LE(chart_y + chart_h, DISPLAY_H - kFooterBase);
}

} // namespace
