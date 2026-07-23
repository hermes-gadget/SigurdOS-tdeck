// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// Compile the production router with renderer recorders. The state machine,
// history capacity, route validation, and back-swipe behavior are not copied.
#define SIGURDOS_NAVIGATION_TEST 1

#include <array>
#include <vector>

#include <gtest/gtest.h>

#include "hal/prefs.h"
#include "ui/navigation.h"

namespace {

using sigurdos::ui::Screen;

int g_dispatch_count = 0;
Screen g_last_dispatched = Screen::COUNT;
bool g_back_highlighted = false;

void record_dispatch(Screen screen)
{
    ++g_dispatch_count;
    g_last_dispatched = screen;
}

} // namespace

namespace sigurdos::ui {

bool pin_grace_active() { return false; }
void pin_entry_show(Screen) { FAIL() << "PIN prompt with PIN disabled"; }
void highlight_back_button(bool highlighted) { g_back_highlighted = highlighted; }

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
void message_search_screen_show() { record_dispatch(Screen::MessageSearch); }
void mesh_dashboard_screen_show() { record_dispatch(Screen::MeshDashboard); }
void file_browser_screen_show() { record_dispatch(Screen::FileBrowser); }
void contact_detail_screen_show(const char*) { record_dispatch(Screen::ContactDetail); }
void repeater_detail_screen_show(const char*, bool) { record_dispatch(Screen::RepeaterDetail); }

} // namespace sigurdos::ui

#include "../../src/ui/navigation.cpp"

namespace {

class ProductionNavigationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        sigurdos::NodePrefs prefs{};
        prefs.set_defaults();
        prefs.device_pin = 0;
        ASSERT_TRUE(sigurdos::prefs_set(prefs));
        sigurdos::ui::navigation_reset_for_test();
        g_dispatch_count = 0;
        g_last_dispatched = Screen::COUNT;
        g_back_highlighted = false;
    }
};

TEST_F(ProductionNavigationTest, StartsAtHomeWithEmptyHistory)
{
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Home);
    EXPECT_FALSE(sigurdos::ui::can_go_back());
}

TEST_F(ProductionNavigationTest, SameScreenNavigationIsANoop)
{
    sigurdos::ui::navigate_to(Screen::Home);
    EXPECT_EQ(g_dispatch_count, 0);
    EXPECT_FALSE(sigurdos::ui::can_go_back());
}

TEST_F(ProductionNavigationTest, MessageSearchDispatchesAndReturnsToChat)
{
    sigurdos::ui::navigate_to(Screen::Chat);
    sigurdos::ui::navigate_to(Screen::MessageSearch);
    EXPECT_EQ(g_last_dispatched, Screen::MessageSearch);
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::MessageSearch);

    sigurdos::ui::go_back();
    EXPECT_EQ(g_last_dispatched, Screen::Chat);
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Chat);
}

TEST_F(ProductionNavigationTest, DeepNavigationReturnsThroughProductionHistory)
{
    sigurdos::ui::navigate_to(Screen::Chat);
    sigurdos::ui::navigate_to(Screen::Settings);
    sigurdos::ui::navigate_to(Screen::Terminal);

    sigurdos::ui::go_back();
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Settings);
    sigurdos::ui::go_back();
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Chat);
    sigurdos::ui::go_back();
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Home);
    EXPECT_FALSE(sigurdos::ui::can_go_back());
}

TEST_F(ProductionNavigationTest, OverflowRetainsTheNewestSixteenEntries)
{
    constexpr std::array<Screen, 20> targets = {
        Screen::Chat, Screen::Contacts, Screen::Channels, Screen::Network,
        Screen::Heard, Screen::Map, Screen::Advertise, Screen::Settings,
        Screen::Trace, Screen::Terminal, Screen::Signal, Screen::RadioSetup,
        Screen::Repeaters, Screen::Onboarding, Screen::SettingsRadio,
        Screen::SettingsGPS, Screen::SettingsDisplay, Screen::SettingsSystem,
        Screen::NodeStats, Screen::Telemetry,
    };
    for (Screen target : targets) sigurdos::ui::navigate_to(target);

    int back_count = 0;
    for (int index = 18; index >= 3; --index) {
        ASSERT_TRUE(sigurdos::ui::can_go_back());
        sigurdos::ui::go_back();
        EXPECT_EQ(sigurdos::ui::current_screen(), targets[index]) << index;
        ++back_count;
    }
    EXPECT_EQ(back_count, 16);
    EXPECT_FALSE(sigurdos::ui::can_go_back());
}

TEST_F(ProductionNavigationTest, TwoLeftSwipesNavigateBack)
{
    sigurdos::ui::navigate_to(Screen::Chat);
    EXPECT_TRUE(sigurdos::ui::handle_back_swipe(SigurdOSTrackballEvent::Left));
    EXPECT_TRUE(g_back_highlighted);
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Chat);

    EXPECT_TRUE(sigurdos::ui::handle_back_swipe(SigurdOSTrackballEvent::Left));
    EXPECT_FALSE(g_back_highlighted);
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Home);
}

TEST_F(ProductionNavigationTest, NonLeftInputResetsBackSwipeCommit)
{
    sigurdos::ui::navigate_to(Screen::Chat);
    sigurdos::ui::handle_back_swipe(SigurdOSTrackballEvent::Left);
    EXPECT_FALSE(sigurdos::ui::handle_back_swipe(SigurdOSTrackballEvent::Up));
    EXPECT_FALSE(g_back_highlighted);
    sigurdos::ui::handle_back_swipe(SigurdOSTrackballEvent::Left);
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Chat);
}

TEST_F(ProductionNavigationTest, ParameterizedDetailRouteReturnsToCaller)
{
    sigurdos::ui::navigate_to(Screen::Contacts);
    sigurdos::ui::navigate_to_contact_detail("Alice");
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::ContactDetail);
    EXPECT_EQ(g_last_dispatched, Screen::ContactDetail);

    sigurdos::ui::go_back();
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Contacts);
}

TEST_F(ProductionNavigationTest, InvalidRoutesCannotMutateState)
{
    sigurdos::ui::navigate_to(Screen::COUNT);
    sigurdos::ui::navigate_to(static_cast<Screen>(999));
    EXPECT_EQ(sigurdos::ui::current_screen(), Screen::Home);
    EXPECT_FALSE(sigurdos::ui::can_go_back());
    EXPECT_EQ(g_dispatch_count, 0);
}

} // namespace
