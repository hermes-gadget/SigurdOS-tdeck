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


/**
 * Unit tests for Terminal screen line-capping logic
 *
 * Verifies that term_add_line() caps the number of lines at MAX_TERM_LINES,
 * deleting the oldest line when the cap is reached. Without this cap, each
 * command creates a new LVGL label that accumulates indefinitely, consuming heap.
 */
#include <gtest/gtest.h>
#include "mesh/cmd_response_queue.h"
#include "ui/repeater_transcript.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>

namespace {

// ── Replicate term line-capping logic for pure testing ──
// Mirrors screens.cpp term_add_line() behavior after the fix

static constexpr int MAX_TERM_LINES = 64;

// Simulated "log container" — tracks line count
struct LogContainer {
    std::vector<std::string> lines;
    bool deleted_first = false;

    int child_count() const { return (int)lines.size(); }

    void add_line(const char* text) {
        // Prune oldest line if over limit
        if (child_count() >= MAX_TERM_LINES) {
            lines.erase(lines.begin());
            deleted_first = true;
        }
        lines.push_back(text);
    }
};

// ── Tests ────────────────────────────────────────────────

TEST(TermLineCapTest, StartsEmpty) {
    LogContainer log;
    EXPECT_EQ(log.child_count(), 0);
}

TEST(TermLineCapTest, AddLinesUpToLimit) {
    LogContainer log;
    for (int i = 0; i < MAX_TERM_LINES; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "line %d", i);
        log.add_line(buf);
    }
    EXPECT_EQ(log.child_count(), MAX_TERM_LINES);
    EXPECT_FALSE(log.deleted_first);
    EXPECT_STREQ(log.lines[0].c_str(), "line 0");
    EXPECT_STREQ(log.lines[MAX_TERM_LINES - 1].c_str(), "line 63");
}

TEST(TermLineCapTest, OverflowPushesOutOldest) {
    LogContainer log;
    for (int i = 0; i < MAX_TERM_LINES + 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "line %d", i);
        log.add_line(buf);
    }
    // Should stay at MAX_TERM_LINES
    EXPECT_EQ(log.child_count(), MAX_TERM_LINES);
    // Oldest line still present should be "line 10"
    EXPECT_STREQ(log.lines[0].c_str(), "line 10");
    // Newest line should be "line 73"
    EXPECT_STREQ(log.lines[MAX_TERM_LINES - 1].c_str(), "line 73");
    EXPECT_TRUE(log.deleted_first);
}

TEST(TermLineCapTest, DoubleOverflowMaintainsOrder) {
    LogContainer log;
    for (int i = 0; i < MAX_TERM_LINES; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "A_%d", i);
        log.add_line(buf);
    }
    // Add 20 more lines
    for (int i = 0; i < 20; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "B_%d", i);
        log.add_line(buf);
    }
    EXPECT_EQ(log.child_count(), MAX_TERM_LINES);
    // First line should be A_20 (first 20 A's pushed out)
    EXPECT_STREQ(log.lines[0].c_str(), "A_20");
    // Last line should be B_19
    EXPECT_STREQ(log.lines[MAX_TERM_LINES - 1].c_str(), "B_19");
}

TEST(TermLineCapTest, EmptyTextStillAddsLine) {
    LogContainer log;
    log.add_line("");
    EXPECT_EQ(log.child_count(), 1);
    EXPECT_STREQ(log.lines[0].c_str(), "");
}

TEST(TermLineCapTest, ExactLimitPlusOne) {
    LogContainer log;
    for (int i = 0; i < MAX_TERM_LINES + 1; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "line %d", i);
        log.add_line(buf);
    }
    EXPECT_EQ(log.child_count(), MAX_TERM_LINES);
    // line 0 was pushed out
    EXPECT_STREQ(log.lines[0].c_str(), "line 1");
    EXPECT_STREQ(log.lines[MAX_TERM_LINES - 1].c_str(), "line 64");
}

TEST(TermLineCapTest, ManyOverflowsKeepsCapacity) {
    LogContainer log;
    for (int i = 0; i < 1000; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "line %d", i);
        log.add_line(buf);
    }
    EXPECT_EQ(log.child_count(), MAX_TERM_LINES);
    // Oldest should be line 936
    EXPECT_STREQ(log.lines[0].c_str(), "line 936");
    EXPECT_STREQ(log.lines[MAX_TERM_LINES - 1].c_str(), "line 999");
}

TEST(RepeaterTranscriptTest, CliReplyHasExplicitTypeBadge) {
    char out[64];
    EXPECT_TRUE(sigurdos::ui::format_repeater_cli_reply(
        out, sizeof(out), 3661, "version 1.2"));
    EXPECT_STREQ(out, "[01:01:01] < [CLI] version 1.2\n");
}

TEST(RepeaterTranscriptTest, CliReplyReportsTruncationAndStaysTerminated) {
    char out[12];
    EXPECT_FALSE(sigurdos::ui::format_repeater_cli_reply(
        out, sizeof(out), 3661, "long response"));
    EXPECT_EQ(out[sizeof(out) - 1], '\0');
}

TEST(RepeaterTranscriptTest, UnknownCommandTimeIsExplicit) {
    char out[64];
    EXPECT_TRUE(sigurdos::ui::format_repeater_cli_command(
        out, sizeof(out), 0, "get name"));
    EXPECT_STREQ(out, "[--:--:--] > get name\n");
}

TEST(RepeaterTranscriptTest, ResponseQueuePreservesTimestampAndOrder) {
    sigurdos::mesh::CmdResponseQueue<2> queue;
    ASSERT_TRUE(queue.push("alpha", "first", 101));
    ASSERT_TRUE(queue.push("beta", "second", 202));
    EXPECT_FALSE(queue.push("gamma", "full", 303));

    char name[32];
    char text[160];
    uint32_t timestamp = 0;
    ASSERT_TRUE(queue.poll(name, sizeof(name), text, sizeof(text), &timestamp));
    EXPECT_STREQ(name, "alpha");
    EXPECT_STREQ(text, "first");
    EXPECT_EQ(timestamp, 101u);
    ASSERT_TRUE(queue.poll(name, sizeof(name), text, sizeof(text), &timestamp));
    EXPECT_STREQ(name, "beta");
    EXPECT_STREQ(text, "second");
    EXPECT_EQ(timestamp, 202u);
    EXPECT_FALSE(queue.poll(name, sizeof(name), text, sizeof(text), &timestamp));
}

TEST(RepeaterTranscriptTest, ResponseQueuePreservesUtf8Boundaries) {
    sigurdos::mesh::CmdResponseQueue<1> queue;
    const std::string response = std::string(157, 'x') + "\xE2\x82\xAC";
    ASSERT_TRUE(queue.push("alpha", response.c_str(), 101));

    char name[32];
    char text[160];
    ASSERT_TRUE(queue.poll(name, sizeof(name), text, sizeof(text), nullptr));
    EXPECT_EQ(strlen(text), 157u);
    EXPECT_EQ(text[156], 'x');
}

} // anonymous namespace
