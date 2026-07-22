// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// Compile the real production router in this test translation unit.  Screen
// renderers are replaced with recorders so authorization and history changes
// are exercised without constructing an LVGL display tree.
#define SIGURDOS_NAVIGATION_TEST 1

#include <array>

#include <gtest/gtest.h>

#include "hal/prefs.h"
#include "ui/navigation.h"

namespace {

using sigurdos::ui::Screen;

bool g_pin_grace = false;
int g_pin_prompt_count = 0;
Screen g_pin_prompt_target = Screen::COUNT;
int g_dispatch_count = 0;
Screen g_last_dispatched = Screen::COUNT;

void record_dispatch(Screen screen)
{
    ++g_dispatch_count;
    g_last_dispatched = screen;
}

} // namespace

namespace sigurdos::ui {

bool pin_grace_active() { return g_pin_grace; }

void pin_entry_show(Screen target)
{
    ++g_pin_prompt_count;
    g_pin_prompt_target = target;
}

void highlight_back_button(bool) {}

void home_screen_show() { record_dispatch(Screen::Home); }
void chat_screen_show() { record_dispatch(Screen::Chat); }
void contacts_screen_show() { record_dispatch(Screen::Contacts); }
void channels_screen_show() { record_dispatch(Screen::Channels); }
void finder_screen_show() { record_dispatch(Screen::Network); }
void heard_screen_show() { record_dispatch(Screen::Heard); }
void map_screen_show() { record_dispatch(Screen::Map); }
void advertise_screen_show() { record_dispatch(Screen::Advertise); }
void settings_screen_show() { record_dispatch(Screen::Settings); }
void trace_screen_show() { record_dispatch(Screen::Trace); }
void terminal_screen_show() { record_dispatch(Screen::Terminal); }
void signal_screen_show() { record_dispatch(Screen::Signal); }
void radio_setup_screen_show() { record_dispatch(Screen::RadioSetup); }
void repeaters_screen_show() { record_dispatch(Screen::Repeaters); }
void onboarding_screen_show() { record_dispatch(Screen::Onboarding); }
void settings_radio_show() { record_dispatch(Screen::SettingsRadio); }
void settings_gps_show() { record_dispatch(Screen::SettingsGPS); }
void settings_display_show() { record_dispatch(Screen::SettingsDisplay); }
void settings_system_show() { record_dispatch(Screen::SettingsSystem); }
void node_stats_screen_show() { record_dispatch(Screen::NodeStats); }
void telemetry_screen_show() { record_dispatch(Screen::Telemetry); }
void node_status_screen_show() { record_dispatch(Screen::NodeStatus); }
void wifi_networks_screen_show() { record_dispatch(Screen::WiFiNetworks); }
void bluetooth_screen_show() { record_dispatch(Screen::Bluetooth); }
void regions_screen_show() { record_dispatch(Screen::Regions); }
void custom_rf_screen_show() { record_dispatch(Screen::CustomRadioSetup); }

void contact_detail_screen_show(const char*)
{
    record_dispatch(Screen::ContactDetail);
}

void repeater_detail_screen_show(const char*, bool)
{
    record_dispatch(Screen::RepeaterDetail);
}

} // namespace sigurdos::ui

#include "../../src/ui/navigation.cpp"

namespace {

constexpr std::array<Screen, 12> PROTECTED_ROUTES = {
    Screen::Settings,
    Screen::Terminal,
    Screen::RadioSetup,
    Screen::SettingsRadio,
    Screen::SettingsGPS,
    Screen::SettingsDisplay,
    Screen::SettingsSystem,
    Screen::NodeStats,
    Screen::WiFiNetworks,
    Screen::Bluetooth,
    Screen::Regions,
    Screen::CustomRadioSetup,
};

bool expected_protected(Screen screen)
{
    for (const Screen protected_screen : PROTECTED_ROUTES) {
        if (screen == protected_screen) return true;
    }
    return false;
}

class NavigationPinGateTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        sigurdos::NodePrefs prefs{};
        prefs.set_defaults();
        prefs.device_pin = 2468;
        ASSERT_TRUE(sigurdos::prefs_set(prefs));

        g_pin_grace = false;
        g_pin_prompt_count = 0;
        g_pin_prompt_target = Screen::COUNT;
        g_dispatch_count = 0;
        g_last_dispatched = Screen::COUNT;
        sigurdos::ui::navigation_reset_for_test();
    }
};

TEST_F(NavigationPinGateTest, RouteTableClassifiesEveryScreen)
{
    for (int value = 0; value < static_cast<int>(Screen::COUNT); ++value) {
        const Screen screen = static_cast<Screen>(value);
        EXPECT_EQ(sigurdos::ui::is_pin_protected_route(screen),
                  expected_protected(screen))
            << "screen " << value;
    }
}

TEST_F(NavigationPinGateTest, DirectNavigationDeniesEveryProtectedRouteUntilUnlock)
{
    for (const Screen target : PROTECTED_ROUTES) {
        sigurdos::ui::navigation_reset_for_test();
        g_pin_prompt_count = 0;
        g_dispatch_count = 0;

        sigurdos::ui::navigate_to(target);

        EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Home) << static_cast<int>(target);
        EXPECT_FALSE(sigurdos::ui::can_go_back()) << static_cast<int>(target);
        EXPECT_EQ(g_pin_prompt_count, 1) << static_cast<int>(target);
        EXPECT_EQ(g_pin_prompt_target, target);
        EXPECT_EQ(g_dispatch_count, 0);

        sigurdos::ui::navigation_pin_unlocked(target);

        EXPECT_EQ(sigurdos::ui::current_screen(), target) << static_cast<int>(target);
        EXPECT_TRUE(sigurdos::ui::can_go_back()) << static_cast<int>(target);
        EXPECT_EQ(g_dispatch_count, 1) << static_cast<int>(target);
        EXPECT_EQ(g_last_dispatched, target);
    }
}

TEST_F(NavigationPinGateTest, UnprotectedDirectRoutesRemainAvailable)
{
    for (int value = 1; value < static_cast<int>(Screen::COUNT); ++value) {
        const Screen target = static_cast<Screen>(value);
        if (expected_protected(target)) continue;
        if (target == Screen::ContactDetail || target == Screen::RepeaterDetail) continue;

        sigurdos::ui::navigation_reset_for_test();
        g_pin_prompt_count = 0;
        g_dispatch_count = 0;
        sigurdos::ui::navigate_to(target);

        EXPECT_EQ(sigurdos::ui::current_screen(), target) << value;
        EXPECT_EQ(g_pin_prompt_count, 0) << value;
        EXPECT_EQ(g_dispatch_count, 1) << value;
        EXPECT_EQ(g_last_dispatched, target);
    }
}

TEST_F(NavigationPinGateTest, ParameterizedDetailRoutesRemainAvailable)
{
    sigurdos::ui::navigate_to_contact_detail("Alice");
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::ContactDetail);
    EXPECT_EQ(g_pin_prompt_count, 0);
    EXPECT_EQ(g_last_dispatched, Screen::ContactDetail);

    sigurdos::ui::navigation_reset_for_test();
    g_dispatch_count = 0;
    g_last_dispatched = Screen::COUNT;
    sigurdos::ui::navigate_to_repeater_detail("Repeater", true);
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::RepeaterDetail);
    EXPECT_EQ(g_pin_prompt_count, 0);
    EXPECT_EQ(g_dispatch_count, 1);
    EXPECT_EQ(g_last_dispatched, Screen::RepeaterDetail);
}

TEST_F(NavigationPinGateTest, BackToProtectedRouteDoesNotPopHistoryBeforeUnlock)
{
    g_pin_grace = true;
    sigurdos::ui::navigate_to(Screen::Settings);
    sigurdos::ui::navigate_to(Screen::Chat);
    ASSERT_EQ(sigurdos::ui::current_screen(), Screen::Chat);

    g_pin_grace = false;
    g_dispatch_count = 0;
    sigurdos::ui::go_back();

    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Chat);
    EXPECT_TRUE(sigurdos::ui::can_go_back());
    EXPECT_EQ(g_pin_prompt_target, Screen::Settings);
    EXPECT_EQ(g_dispatch_count, 0);

    sigurdos::ui::navigation_pin_unlocked(Screen::Settings);
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Settings);
    EXPECT_EQ(g_last_dispatched, Screen::Settings);

    sigurdos::ui::go_back();
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Home);
}

TEST_F(NavigationPinGateTest, ProtectedRefreshWaitsForUnlockWithoutChangingHistory)
{
    g_pin_grace = true;
    sigurdos::ui::navigate_to(Screen::SettingsSystem);
    ASSERT_TRUE(sigurdos::ui::can_go_back());

    g_pin_grace = false;
    g_dispatch_count = 0;
    sigurdos::ui::refresh_current_screen();

    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::SettingsSystem);
    EXPECT_TRUE(sigurdos::ui::can_go_back());
    EXPECT_EQ(g_pin_prompt_target, Screen::SettingsSystem);
    EXPECT_EQ(g_dispatch_count, 0);

    sigurdos::ui::navigation_pin_unlocked(Screen::SettingsSystem);
    EXPECT_EQ(g_dispatch_count, 1);
    EXPECT_EQ(g_last_dispatched, Screen::SettingsSystem);
    EXPECT_TRUE(sigurdos::ui::can_go_back());
}

TEST_F(NavigationPinGateTest, CancelClearsPendingRouteWithoutGrantingAccess)
{
    sigurdos::ui::navigate_to(Screen::SettingsGPS);
    ASSERT_EQ(g_pin_prompt_target, Screen::SettingsGPS);

    sigurdos::ui::navigation_pin_cancelled();
    sigurdos::ui::navigate_to(Screen::Chat);

    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Chat);
    EXPECT_EQ(g_last_dispatched, Screen::Chat);
}

TEST_F(NavigationPinGateTest, StalePinScreenCannotResumeDifferentPendingTarget)
{
    sigurdos::ui::navigate_to(Screen::SettingsRadio);
    ASSERT_EQ(g_pin_prompt_target, Screen::SettingsRadio);

    sigurdos::ui::navigation_pin_unlocked(Screen::Terminal);

    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Home);
    EXPECT_FALSE(sigurdos::ui::can_go_back());
    EXPECT_EQ(g_dispatch_count, 0);
}

} // namespace
