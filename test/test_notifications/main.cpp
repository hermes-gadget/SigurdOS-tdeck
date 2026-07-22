// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include "ui/notification_model.h"

using namespace sigurdos::ui;

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(NotificationPolicyTest, DetectsCaseInsensitiveWholeNameMentions)
{
    EXPECT_TRUE(notification_contains_mention("hello @Sigurd", "sigurd"));
    EXPECT_TRUE(notification_contains_mention("SIGURD, radio check", "Sigurd"));
    EXPECT_FALSE(notification_contains_mention("sigurdos is online", "sigurd"));
    EXPECT_FALSE(notification_contains_mention(nullptr, "sigurd"));
    EXPECT_FALSE(notification_contains_mention("hello", ""));
}

TEST(NotificationPolicyTest, MentionTextAddsOnlyOneChannelMarker)
{
    char text[96];
    EXPECT_TRUE(notification_mention_text(text, sizeof(text), "Alice", "general"));
    EXPECT_STREQ(text, "Mention from Alice in #general");
    EXPECT_TRUE(notification_mention_text(text, sizeof(text), "Alice", "#general"));
    EXPECT_STREQ(text, "Mention from Alice in #general");
}

TEST(NotificationPolicyTest, LowResourceThresholdsAreBounded)
{
    EXPECT_TRUE(notification_low_battery(15));
    EXPECT_FALSE(notification_low_battery(16));
    EXPECT_FALSE(notification_low_battery(-1));
    EXPECT_TRUE(notification_storage_low(1024 * 1024, 64ULL * 1024 * 1024));
    EXPECT_TRUE(notification_storage_low(2ULL * 1024 * 1024, 200ULL * 1024 * 1024));
    EXPECT_FALSE(notification_storage_low(8ULL * 1024 * 1024, 200ULL * 1024 * 1024));
    EXPECT_FALSE(notification_storage_low(0, 0));
}

TEST(NotificationPolicyTest, LoginFailuresExposeSpecificSessionReason)
{
    char text[96];
    EXPECT_TRUE(notification_login_failure_text(
        text, sizeof(text), "relay", LoginFailureReason::Rejected));
    EXPECT_STREQ(text, "relay: LOGIN_FAILED");
    EXPECT_TRUE(notification_login_failure_text(
        text, sizeof(text), "relay", LoginFailureReason::TimedOut));
    EXPECT_STREQ(text, "relay: LOGIN_FAILED (timeout)");
    EXPECT_TRUE(notification_login_failure_text(
        text, sizeof(text), "relay", LoginFailureReason::ConnectionDropped));
    EXPECT_STREQ(text, "relay: CONNECTION_DROPPED");
    EXPECT_TRUE(notification_login_failure_text(
        text, sizeof(text), "relay", LoginFailureReason::SessionInvalidated));
    EXPECT_STREQ(text, "relay: SESSION_INVALIDATED");
}

TEST(NotificationPolicyTest, LoginFailureTextReportsTruncation)
{
    char text[12];
    EXPECT_FALSE(notification_login_failure_text(
        text, sizeof(text), "long-repeater-name", LoginFailureReason::TimedOut));
    EXPECT_EQ(text[sizeof(text) - 1], '\0');
}

TEST(NotificationPolicyTest, UiErrorsUseImportantTimedPolicy)
{
    const NotificationPolicy policy = notification_policy(NotificationEvent::UiError);
    EXPECT_EQ(policy.level, NotificationLevel::Important);
    EXPECT_EQ(policy.duration_ms, 6000U);
    EXPECT_FALSE(policy.sticky);
}

TEST(NotificationQueueTest, HigherPriorityAlertPreemptsAndThenResumes)
{
    NotificationQueue queue;
    queue.post(NotificationEvent::DirectMessage, "DM from Alice", 100);
    queue.post(NotificationEvent::DirectMessage, "DM from Carol", 150);
    queue.post(NotificationEvent::Mention, "Mention from Bob", 200);

    ASSERT_TRUE(queue.has_current());
    EXPECT_STREQ("Mention from Bob", queue.current().text);
    EXPECT_EQ(2, queue.pending_count());

    queue.dismiss(300);
    EXPECT_STREQ("DM from Alice", queue.current().text);
    queue.dismiss(400);
    EXPECT_STREQ("DM from Carol", queue.current().text);
}

TEST(NotificationQueueTest, TimedAlertsExpireButCriticalAlertsAreSticky)
{
    NotificationQueue queue;
    queue.post(NotificationEvent::DirectMessage, "DM", 100);
    EXPECT_FALSE(queue.tick(4099));
    EXPECT_TRUE(queue.tick(4100));
    EXPECT_FALSE(queue.has_current());

    queue.post(NotificationEvent::LowBattery, "Low battery", 5000);
    EXPECT_FALSE(queue.tick(UINT32_MAX));
    EXPECT_TRUE(queue.has_current());
    queue.dismiss(6000);
    EXPECT_FALSE(queue.has_current());
}

TEST(NotificationQueueTest, ExpiryMathHandlesMillisWraparound)
{
    NotificationQueue queue;
    queue.post(NotificationEvent::DirectMessage, "DM", UINT32_MAX - 1000);
    EXPECT_FALSE(queue.tick(2000));
    EXPECT_TRUE(queue.tick(3000));
}

TEST(NotificationQueueTest, BurstCapacityRemainsFourIncludingCurrent)
{
    NotificationQueue queue;
    queue.post(NotificationEvent::DirectMessage, "A", 0);
    queue.post(NotificationEvent::DirectMessage, "B", 1);
    queue.post(NotificationEvent::DirectMessage, "C", 2);
    queue.post(NotificationEvent::DirectMessage, "D", 3);
    queue.post(NotificationEvent::DirectMessage, "E", 4);

    EXPECT_EQ(3, queue.pending_count());
    queue.dismiss(5);
    EXPECT_STREQ("C", queue.current().text);
}

TEST(NotificationQueueTest, DuplicateEventsAreCoalesced)
{
    NotificationQueue queue;
    queue.post(NotificationEvent::DirectMessage, "same", 0);
    queue.post(NotificationEvent::DirectMessage, "same", 1);
    queue.post(NotificationEvent::Mention, "queued", 2);
    queue.post(NotificationEvent::Mention, "queued", 3);
    EXPECT_EQ(1, queue.pending_count());
}

TEST(NotificationQueueTest, OverflowEvictsOldestLowestPriorityItem)
{
    NotificationQueue queue;
    queue.post(NotificationEvent::StorageFull, "critical active", 0);
    queue.post(NotificationEvent::DirectMessage, "old info", 1);
    queue.post(NotificationEvent::Mention, "important", 2);
    queue.post(NotificationEvent::DirectMessage, "new info", 3);
    queue.post(NotificationEvent::LoginFailure, "new important", 4);

    queue.dismiss(5);
    EXPECT_STREQ("important", queue.current().text);
    queue.dismiss(6);
    EXPECT_STREQ("new info", queue.current().text);
    queue.dismiss(7);
    EXPECT_STREQ("new important", queue.current().text);
}

TEST(NotificationQueueTest, LowerPriorityOverflowNeverDisplacesCritical)
{
    NotificationQueue queue;
    queue.post(NotificationEvent::LowBattery, "active", 0);
    queue.post(NotificationEvent::StorageFull, "critical one", 1);
    queue.post(NotificationEvent::OtaFailure, "critical two", 2);
    queue.post(NotificationEvent::LowBattery, "critical three", 3);
    queue.post(NotificationEvent::DirectMessage, "drop me", 4);
    EXPECT_EQ(3, queue.pending_count());
    queue.dismiss(5);
    EXPECT_STREQ("critical one", queue.current().text);
}
