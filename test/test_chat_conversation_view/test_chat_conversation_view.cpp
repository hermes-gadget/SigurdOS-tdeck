// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdio>
#include <cstring>

#include <gtest/gtest.h>

#include "ui/chat_conversation_view.h"
#include "ui/chat_message_buffer.h"

namespace {

using sigurdos::ui::ChatConversationView;
using sigurdos::ui::ChatMessageBuffer;

constexpr int CAPACITY = sigurdos::ui::CHAT_CONVERSATION_CAPACITY;
constexpr int MESH_CAPACITY = sigurdos::ui::CHAT_MESH_CONVERSATION_CAPACITY;
constexpr int DM_CAPACITY = sigurdos::ui::CHAT_DM_CONVERSATION_CAPACITY;
constexpr int NAME_CAPACITY = 37;

void set_name(char (&names)[CAPACITY][NAME_CAPACITY], int index, const char* name)
{
    std::strncpy(names[index], name, NAME_CAPACITY - 1);
    names[index][NAME_CAPACITY - 1] = '\0';
}

void append(ChatMessageBuffer& buffer, const char* sender, const char* text,
            uint32_t timestamp, uint32_t store_id = 0)
{
    ASSERT_NE(buffer.append(sender, text, timestamp, false,
                            8, 8, 4, store_id), nullptr);
}

TEST(ChatConversationViewTest, FilterRoundTripsNeverMoveCanonicalState)
{
    char names[CAPACITY][NAME_CAPACITY] = {};
    int unread[CAPACITY] = {};
    ChatMessageBuffer buffers[CAPACITY];
    set_name(names, 0, "Public");
    set_name(names, 1, "DM: Alice");
    set_name(names, 2, "#ops");
    unread[1] = 7;
    append(buffers[1], "Alice", "still here", 42);

    ChatConversationView<CAPACITY> view;
    for (int cycle = 0; cycle < 50; ++cycle) {
        view.rebuild(names, 3, 1);
        ASSERT_EQ(view.count(), 2);
        EXPECT_EQ(view.canonicalIndex(0), 0);
        EXPECT_EQ(view.canonicalIndex(1), 2);
        EXPECT_EQ(view.visibleIndex(1), -1);

        view.rebuild(names, 3, 2);
        ASSERT_EQ(view.count(), 1);
        EXPECT_EQ(view.canonicalIndex(0), 1);
        EXPECT_EQ(view.visibleIndex(1), 0);

        view.rebuild(names, 3, 0);
        EXPECT_EQ(view.count(), 3);
    }

    EXPECT_STREQ(names[1], "DM: Alice");
    EXPECT_EQ(unread[1], 7);
    ASSERT_EQ(buffers[1].count(), 1);
    EXPECT_STREQ(buffers[1].at(0).text, "still here");
    EXPECT_EQ(buffers[1].at(0).timestamp, 42u);
}

TEST(ChatConversationViewTest, FullMeshTableStillHasRoomForMaximumSyntheticDms)
{
    char names[CAPACITY][NAME_CAPACITY] = {};
    ChatMessageBuffer buffers[CAPACITY];
    for (int i = 0; i < MESH_CAPACITY; ++i) {
        char name[NAME_CAPACITY];
        std::snprintf(name, sizeof(name), "#channel-%02d", i);
        set_name(names, i, name);
    }
    for (int i = 0; i < DM_CAPACITY; ++i) {
        char name[NAME_CAPACITY];
        std::snprintf(name, sizeof(name), "DM: Contact-%02d", i);
        set_name(names, MESH_CAPACITY + i, name);
    }
    append(buffers[31], "Contact-15", "newest private message", 99);

    ChatConversationView<CAPACITY> view;
    view.rebuild(names, CAPACITY, 0);
    EXPECT_EQ(view.count(), CAPACITY);

    view.rebuild(names, CAPACITY, 1);
    ASSERT_EQ(view.count(), MESH_CAPACITY);
    EXPECT_EQ(view.canonicalIndex(MESH_CAPACITY - 1), MESH_CAPACITY - 1);

    view.rebuild(names, CAPACITY, 2);
    ASSERT_EQ(view.count(), DM_CAPACITY);
    EXPECT_EQ(view.canonicalIndex(0), MESH_CAPACITY);
    EXPECT_EQ(view.canonicalIndex(DM_CAPACITY - 1), CAPACITY - 1);
    ASSERT_EQ(buffers[31].count(), 1);
    EXPECT_STREQ(buffers[31].at(0).text, "newest private message");
}

TEST(ChatConversationViewTest, IncomingDmWhileHiddenAppearsWhenDmViewReturns)
{
    char names[CAPACITY][NAME_CAPACITY] = {};
    int unread[CAPACITY] = {};
    ChatMessageBuffer buffers[CAPACITY];
    set_name(names, 0, "Public");
    set_name(names, 1, "DM: Alice");

    ChatConversationView<CAPACITY> view;
    view.rebuild(names, 2, 1);
    ASSERT_EQ(view.count(), 1);

    set_name(names, 2, "DM: Bob");
    unread[2] = 1;
    append(buffers[2], "Bob", "arrived while hidden", 1234);
    view.rebuild(names, 3, 1);
    EXPECT_EQ(view.count(), 1);
    EXPECT_EQ(view.visibleIndex(2), -1);
    EXPECT_EQ(unread[2], 1);
    EXPECT_EQ(buffers[2].count(), 1);

    view.rebuild(names, 3, 2);
    ASSERT_EQ(view.count(), 2);
    EXPECT_EQ(view.canonicalIndex(1), 2);
    EXPECT_STREQ(names[view.canonicalIndex(1)], "DM: Bob");
    EXPECT_STREQ(buffers[2].at(0).text, "arrived while hidden");
}

TEST(ChatConversationViewTest, PersistedHistoryStateSurvivesPresentationFilters)
{
    char names[CAPACITY][NAME_CAPACITY] = {};
    ChatMessageBuffer buffers[CAPACITY];
    set_name(names, 0, "Public");
    set_name(names, 1, "DM: Stored");
    append(buffers[1], "Stored", "loaded from disk", 777, 55);

    ChatConversationView<CAPACITY> view;
    view.rebuild(names, 2, 1);
    EXPECT_EQ(view.visibleIndex(1), -1);
    ASSERT_EQ(buffers[1].count(), 1);

    view.rebuild(names, 2, 2);
    ASSERT_EQ(view.canonicalIndex(0), 1);
    EXPECT_EQ(buffers[1].at(0).store_id, 55u);
    EXPECT_STREQ(buffers[1].at(0).text, "loaded from disk");
}

TEST(ChatConversationViewTest, InvalidCountsAndIndicesAreBounded)
{
    char names[CAPACITY][NAME_CAPACITY] = {};
    for (int i = 0; i < CAPACITY; ++i) set_name(names, i, "Public");

    ChatConversationView<CAPACITY> view;
    view.rebuild(names, -1, 0);
    EXPECT_EQ(view.count(), 0);
    EXPECT_EQ(view.canonicalIndex(0), -1);

    view.rebuild(names, CAPACITY + 10, 0);
    EXPECT_EQ(view.count(), CAPACITY);
    EXPECT_EQ(view.canonicalIndex(-1), -1);
    EXPECT_EQ(view.canonicalIndex(CAPACITY), -1);
    EXPECT_EQ(view.visibleIndex(CAPACITY), -1);
}

} // namespace
