// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "ui/home_routes.h"

namespace {

using sigurdos::ui::findHomeRoute;
using sigurdos::ui::HomeRouteFilter;
using sigurdos::ui::homeRouteAt;
using sigurdos::ui::homeRouteCount;
using sigurdos::ui::HOME_ROUTE_COUNT;
using sigurdos::ui::Screen;

TEST(HomeRoutesTest, ProductionTableHasExpectedSizeAndBounds)
{
    EXPECT_EQ(homeRouteCount(), HOME_ROUTE_COUNT);
    EXPECT_NE(homeRouteAt(0), nullptr);
    EXPECT_NE(homeRouteAt(HOME_ROUTE_COUNT - 1), nullptr);
    EXPECT_EQ(homeRouteAt(HOME_ROUTE_COUNT), nullptr);
}

TEST(HomeRoutesTest, PrimaryDestinationsUseDedicatedProductionScreens)
{
    ASSERT_NE(findHomeRoute("REPEATERS"), nullptr);
    ASSERT_NE(findHomeRoute("PACKETS"), nullptr);
    EXPECT_EQ(findHomeRoute("REPEATERS")->target, Screen::Repeaters);
    EXPECT_EQ(findHomeRoute("PACKETS")->target, Screen::Heard);
    EXPECT_NE(findHomeRoute("REPEATERS")->target,
              findHomeRoute("PACKETS")->target);
}

TEST(HomeRoutesTest, ConversationTilesShareScreensButSelectDifferentFilters)
{
    const auto* channels = findHomeRoute("CHATS");
    const auto* dms = findHomeRoute("DMs");
    const auto* rooms = findHomeRoute("ROOMS");
    const auto* contacts = findHomeRoute("CONTACTS");
    ASSERT_NE(channels, nullptr);
    ASSERT_NE(dms, nullptr);
    ASSERT_NE(rooms, nullptr);
    ASSERT_NE(contacts, nullptr);

    EXPECT_EQ(channels->target, Screen::Chat);
    EXPECT_EQ(dms->target, Screen::Chat);
    EXPECT_EQ(channels->filter, HomeRouteFilter::Channels);
    EXPECT_EQ(dms->filter, HomeRouteFilter::DirectMessages);
    EXPECT_EQ(rooms->target, Screen::Contacts);
    EXPECT_EQ(contacts->target, Screen::Contacts);
    EXPECT_EQ(rooms->filter, HomeRouteFilter::Rooms);
    EXPECT_EQ(contacts->filter, HomeRouteFilter::Default);
}

TEST(HomeRoutesTest, OnlyChatsOwnsTheUnreadBadge)
{
    std::size_t badges = 0;
    for (std::size_t i = 0; i < homeRouteCount(); ++i) {
        if (homeRouteAt(i)->badge) ++badges;
    }
    EXPECT_EQ(badges, 1U);
    EXPECT_TRUE(findHomeRoute("CHATS")->badge);
}

TEST(HomeRoutesTest, LabelsAreUniqueAndNonEmpty)
{
    for (std::size_t i = 0; i < homeRouteCount(); ++i) {
        const auto* route = homeRouteAt(i);
        ASSERT_NE(route, nullptr);
        ASSERT_NE(route->label, nullptr);
        EXPECT_NE(route->label[0], '\0');
        EXPECT_EQ(findHomeRoute(route->label), route);
    }
    EXPECT_EQ(findHomeRoute(nullptr), nullptr);
    EXPECT_EQ(findHomeRoute("FINDER"), nullptr);
}

} // namespace
