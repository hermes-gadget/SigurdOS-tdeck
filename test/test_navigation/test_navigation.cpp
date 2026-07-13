// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "ui/navigation_state.h"

namespace {

using sigurdos::ui::BackSwipeResult;
using sigurdos::ui::NavigationState;
using sigurdos::ui::Screen;

class NavigationStateTest : public ::testing::Test {
protected:
    NavigationState state;
};

TEST_F(NavigationStateTest, StartsAtHomeWithEmptyHistory)
{
    EXPECT_EQ(state.current(), Screen::Home);
    EXPECT_FALSE(state.canGoBack());
    EXPECT_EQ(state.historySize(), 0U);
}

TEST_F(NavigationStateTest, NavigatingToCurrentScreenIsNoop)
{
    EXPECT_FALSE(state.navigateTo(Screen::Home));
    EXPECT_FALSE(state.canGoBack());
}

TEST_F(NavigationStateTest, EveryProductionScreenCanBeADestination)
{
    for (int value = static_cast<int>(Screen::Chat);
         value < static_cast<int>(Screen::COUNT); ++value) {
        state.reset();
        const auto destination = static_cast<Screen>(value);
        ASSERT_TRUE(state.navigateTo(destination));
        EXPECT_EQ(state.current(), destination);
        EXPECT_TRUE(state.canGoBack());
    }
}

TEST_F(NavigationStateTest, BackWalksTheRealHistory)
{
    ASSERT_TRUE(state.navigateTo(Screen::Chat));
    ASSERT_TRUE(state.navigateTo(Screen::Settings));
    ASSERT_TRUE(state.navigateTo(Screen::Terminal));

    ASSERT_TRUE(state.goBack());
    EXPECT_EQ(state.current(), Screen::Settings);
    ASSERT_TRUE(state.goBack());
    EXPECT_EQ(state.current(), Screen::Chat);
    ASSERT_TRUE(state.goBack());
    EXPECT_EQ(state.current(), Screen::Home);
    EXPECT_FALSE(state.goBack());
}

TEST_F(NavigationStateTest, FullHistoryDropsOnlyTheOldestDestination)
{
    for (std::size_t value = 1; value <= NavigationState::MAX_HISTORY + 1; ++value) {
        ASSERT_TRUE(state.navigateTo(static_cast<Screen>(value)));
    }
    ASSERT_EQ(state.historySize(), NavigationState::MAX_HISTORY);

    for (std::size_t value = NavigationState::MAX_HISTORY; value > 0; --value) {
        ASSERT_TRUE(state.goBack());
        EXPECT_EQ(state.current(), static_cast<Screen>(value));
    }
    EXPECT_EQ(state.current(), Screen::Chat);
    EXPECT_FALSE(state.canGoBack());
}

TEST_F(NavigationStateTest, ResetCanStartAtAnyScreen)
{
    state.navigateTo(Screen::Chat);
    state.reset(Screen::Settings);
    EXPECT_EQ(state.current(), Screen::Settings);
    EXPECT_FALSE(state.canGoBack());
}

TEST_F(NavigationStateTest, FirstLeftSwipeIsConsumed)
{
    state.navigateTo(Screen::Chat);
    EXPECT_EQ(state.handleBackSwipe(true), BackSwipeResult::Consumed);
    EXPECT_EQ(state.current(), Screen::Chat);
}

TEST_F(NavigationStateTest, SecondLeftSwipeNavigatesBack)
{
    state.navigateTo(Screen::Chat);
    state.handleBackSwipe(true);
    EXPECT_EQ(state.handleBackSwipe(true), BackSwipeResult::Navigated);
    EXPECT_EQ(state.current(), Screen::Home);
}

TEST_F(NavigationStateTest, NonLeftInputResetsSwipeCommit)
{
    state.navigateTo(Screen::Chat);
    state.handleBackSwipe(true);
    EXPECT_EQ(state.handleBackSwipe(false), BackSwipeResult::Ignored);
    EXPECT_EQ(state.handleBackSwipe(true), BackSwipeResult::Consumed);
    EXPECT_EQ(state.current(), Screen::Chat);
}

TEST_F(NavigationStateTest, NavigationResetsSwipeCommit)
{
    state.navigateTo(Screen::Chat);
    state.handleBackSwipe(true);
    state.navigateTo(Screen::Settings);
    EXPECT_EQ(state.handleBackSwipe(true), BackSwipeResult::Consumed);
    EXPECT_EQ(state.current(), Screen::Settings);
}

TEST_F(NavigationStateTest, DoubleSwipeAtRootIsConsumedWithoutNavigation)
{
    EXPECT_EQ(state.handleBackSwipe(true), BackSwipeResult::Consumed);
    EXPECT_EQ(state.handleBackSwipe(true), BackSwipeResult::Completed);
    EXPECT_EQ(state.current(), Screen::Home);
}

// Compile contract: Screen::COUNT remains a sentinel immediately after the
// last production destination. Behaviour is covered separately above.
TEST(ScreenIdContract, CountFollowsRegions)
{
    EXPECT_EQ(static_cast<int>(Screen::COUNT),
              static_cast<int>(Screen::Regions) + 1);
}

} // namespace
