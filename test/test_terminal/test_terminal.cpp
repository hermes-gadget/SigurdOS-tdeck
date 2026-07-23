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

#include "ui/identity_command_guard.h"
#include "ui/pin_gate_policy.h"
#include "mesh/cmd_response_queue.h"
#include "ui/repeater_transcript.h"
#include "ui/terminal_line_cap.h"
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>

namespace {

// ── Replicate term line-capping logic for pure testing ──
// Mirrors screens.cpp term_add_line() behavior after the fix

static constexpr int MAX_TERM_LINES =
    static_cast<int>(sigurdos::ui::MAX_TERMINAL_LINES);

// Simulated "log container" — tracks line count
struct LogContainer {
    std::vector<std::string> lines;
    bool deleted_first = false;

    int child_count() const { return (int)lines.size(); }

    void add_line(const char* text) {
        const bool ready = sigurdos::ui::terminal_prune_oldest_for_append(
            this,
            [](LogContainer* log) { return log->child_count(); },
            [](LogContainer* log) -> LogContainer* {
                return log->lines.empty() ? nullptr : log;
            },
            [](LogContainer* log) {
                log->lines.erase(log->lines.begin());
                log->deleted_first = true;
            });
        if (!ready) return;
        lines.push_back(text);
    }
};

struct DeferredDeleteContainer {
    int children = MAX_TERM_LINES;
    int delete_requests = 0;
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

TEST(TermLineCapTest, DeferredDeletionCannotLivelockOrQueueSameRowTwice) {
    DeferredDeleteContainer log;

    EXPECT_TRUE(sigurdos::ui::terminal_prune_oldest_for_append(
        &log,
        [](DeferredDeleteContainer* value) { return value->children; },
        [](DeferredDeleteContainer* value) { return value; },
        [](DeferredDeleteContainer* value) {
            ++value->delete_requests;
            // Deliberately leave child count unchanged, matching
            // lv_obj_del_async() until a later LVGL tick.
        }));

    EXPECT_EQ(log.children, MAX_TERM_LINES);
    EXPECT_EQ(log.delete_requests, 1);
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
    EXPECT_STREQ(out, "[--:--:--] > [CMD] get name\n");
}

TEST(RepeaterTranscriptTest, HistoryPreservesTypeContactAndTimestamp) {
    sigurdos::ui::RepeaterTranscript<2> history;
    ASSERT_TRUE(history.append(
        "alpha", sigurdos::ui::RepeaterTranscriptType::Command,
        101, "ver"));
    ASSERT_TRUE(history.append(
        "alpha", sigurdos::ui::RepeaterTranscriptType::CliReply,
        202, "v1.2"));

    ASSERT_EQ(history.size(), 2U);
    const auto* command = history.at(0);
    const auto* reply = history.at(1);
    ASSERT_NE(command, nullptr);
    ASSERT_NE(reply, nullptr);
    EXPECT_STREQ(command->contact, "alpha");
    EXPECT_EQ(command->type, sigurdos::ui::RepeaterTranscriptType::Command);
    EXPECT_EQ(command->timestamp, 101U);
    EXPECT_STREQ(reply->text, "v1.2");
    EXPECT_EQ(reply->type, sigurdos::ui::RepeaterTranscriptType::CliReply);
    EXPECT_EQ(reply->timestamp, 202U);
}

TEST(RepeaterTranscriptTest, HistoryEvictsOldestAtFixedCapacity) {
    sigurdos::ui::RepeaterTranscript<2> history;
    ASSERT_TRUE(history.append(
        "alpha", sigurdos::ui::RepeaterTranscriptType::Command, 1, "one"));
    ASSERT_TRUE(history.append(
        "alpha", sigurdos::ui::RepeaterTranscriptType::Command, 2, "two"));
    ASSERT_TRUE(history.append(
        "beta", sigurdos::ui::RepeaterTranscriptType::CliReply, 3, "three"));

    ASSERT_EQ(history.size(), 2U);
    ASSERT_NE(history.at(0), nullptr);
    ASSERT_NE(history.at(1), nullptr);
    EXPECT_STREQ(history.at(0)->text, "two");
    EXPECT_STREQ(history.at(1)->contact, "beta");
    EXPECT_EQ(history.at(1)->timestamp, 3U);
    EXPECT_EQ(history.at(2), nullptr);
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

TEST(PinGatePolicyTest, FailuresEscalateAcrossScreenInstancesAndCapDelay) {
    sigurdos::ui::PinGateState state{};
    const uint32_t expected[] = {0, 1000, 5000, 30000, 60000, 300000, 300000};
    for (uint32_t delay : expected) {
        sigurdos::ui::pinGateRecordFailure(state, 100);
        EXPECT_EQ(state.lockout_ms, delay);
    }
    EXPECT_EQ(state.failures, 7);
}

TEST(PinGatePolicyTest, LockoutAndGraceAreWrapSafeAndClearable) {
    sigurdos::ui::PinGateState state{};
    state.failures = 3;
    state.failure_at_ms = 0xFFFFFFF0u;
    state.lockout_ms = 5000;
    EXPECT_TRUE(sigurdos::ui::pinGateLocked(state, 0x20u));
    EXPECT_EQ(sigurdos::ui::pinGateRemaining(state, 0x20u), 4952U);

    sigurdos::ui::pinGateRecordSuccess(state, 0xFFFFFFF0u);
    EXPECT_TRUE(sigurdos::ui::pinGateGraceActive(state, 0x20u, 300000));
    sigurdos::ui::pinGateClearGrace(state);
    EXPECT_FALSE(sigurdos::ui::pinGateGraceActive(state, 0x20u, 300000));
}

TEST(IdentityCommandGuardTest, PinlessCommandsRequireMatchingFreshConfirmation) {
    using namespace sigurdos::ui;
    IdentityCommandGuard guard{};
    EXPECT_EQ(authorizeIdentityCommand(guard, 0, IdentityOperation::Export,
                                       false, 100),
              IdentityGuardResult::ConfirmationRequired);
    EXPECT_EQ(authorizeIdentityCommand(guard, 0, IdentityOperation::Import,
                                       true, 101),
              IdentityGuardResult::ConfirmationRejected);
    EXPECT_EQ(authorizeIdentityCommand(guard, 0, IdentityOperation::Export,
                                       true, 102),
              IdentityGuardResult::ConfirmationRejected);

    EXPECT_EQ(authorizeIdentityCommand(guard, 0, IdentityOperation::Export,
                                       false, 200),
              IdentityGuardResult::ConfirmationRequired);
    EXPECT_EQ(authorizeIdentityCommand(guard, 0, IdentityOperation::Export,
                                       true, 15200),
              IdentityGuardResult::ConfirmationRejected);
}

TEST(IdentityCommandGuardTest, FreshConfirmationIsSingleUseAndPinUnlockBypassesIt) {
    using namespace sigurdos::ui;
    IdentityCommandGuard guard{};
    EXPECT_EQ(authorizeIdentityCommand(guard, 0, IdentityOperation::Import,
                                       false, 100),
              IdentityGuardResult::ConfirmationRequired);
    EXPECT_EQ(authorizeIdentityCommand(guard, 0, IdentityOperation::Import,
                                       true, 101),
              IdentityGuardResult::Allowed);
    EXPECT_EQ(authorizeIdentityCommand(guard, 0, IdentityOperation::Import,
                                       true, 102),
              IdentityGuardResult::ConfirmationRejected);
    EXPECT_EQ(authorizeIdentityCommand(guard, 123456, IdentityOperation::Export,
                                       false, 500),
              IdentityGuardResult::Allowed);
    EXPECT_EQ(authorizeIdentityCommand(guard, 0, IdentityOperation::Import,
                                       true, 501),
              IdentityGuardResult::ConfirmationRejected);
    EXPECT_EQ(authorizeIdentityCommand(guard, 0, IdentityOperation::None,
                                       false, 502),
              IdentityGuardResult::ConfirmationRejected);
}

} // anonymous namespace
