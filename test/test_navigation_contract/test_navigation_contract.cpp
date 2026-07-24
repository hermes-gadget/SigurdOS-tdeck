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

#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "ui/navigation.h"

namespace {

using sigurdos::ui::Screen;
using sigurdos::ui::navigation_screen_valid;

TEST(NavigationContractTest, RejectsSentinelAndOutOfRangeRoutes) {
    EXPECT_TRUE(navigation_screen_valid(Screen::Home));
    EXPECT_TRUE(navigation_screen_valid(Screen::CustomRadioSetup));
    EXPECT_TRUE(navigation_screen_valid(Screen::MessageSearch));
    EXPECT_FALSE(navigation_screen_valid(Screen::COUNT));
    EXPECT_FALSE(navigation_screen_valid(static_cast<Screen>(-1)));
    EXPECT_FALSE(navigation_screen_valid(static_cast<Screen>(999)));
}

std::string read_project_file(const char* path)
{
    const char* prefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    for (const char* prefix : prefixes) {
        std::ifstream in(std::string(prefix) + path);
        if (in.good()) {
            return std::string(std::istreambuf_iterator<char>(in), {});
        }
    }
    return {};
}

constexpr std::array<Screen, 31> kScreens = {
    Screen::Home,
    Screen::Chat,
    Screen::Contacts,
    Screen::Channels,
    Screen::Network,
    Screen::Heard,
    Screen::Map,
    Screen::Advertise,
    Screen::Settings,
    Screen::Trace,
    Screen::Terminal,
    Screen::Signal,
    Screen::RadioSetup,
    Screen::Repeaters,
    Screen::Onboarding,
    Screen::ContactDetail,
    Screen::SettingsRadio,
    Screen::SettingsGPS,
    Screen::SettingsDisplay,
    Screen::SettingsSystem,
    Screen::NodeStats,
    Screen::Telemetry,
    Screen::NodeStatus,
    Screen::WiFiNetworks,
    Screen::Bluetooth,
    Screen::Regions,
    Screen::RepeaterDetail,
    Screen::CustomRadioSetup,
    Screen::MessageSearch,
    Screen::MeshDashboard,
    Screen::FileBrowser,
};

TEST(NavigationContractTest, ScreenEnumCountMatchesInventory) {
    EXPECT_EQ(static_cast<int>(Screen::COUNT), static_cast<int>(kScreens.size()));
}

TEST(NavigationContractTest, ScreenEnumValuesAreContiguous) {
    for (std::size_t i = 0; i < kScreens.size(); ++i) {
        EXPECT_EQ(static_cast<int>(kScreens[i]), static_cast<int>(i))
            << "Screen enum changed at index " << i;
    }
}

TEST(NavigationContractTest, CoreScreensKeepStablePositions) {
    EXPECT_EQ(static_cast<int>(Screen::Home), 0);
    EXPECT_EQ(static_cast<int>(Screen::Chat), 1);
    EXPECT_EQ(static_cast<int>(Screen::Contacts), 2);
    EXPECT_EQ(static_cast<int>(Screen::Channels), 3);
    EXPECT_EQ(static_cast<int>(Screen::Network), 4);
    EXPECT_EQ(static_cast<int>(Screen::Heard), 5);
}

TEST(NavigationContractTest, MeshParityFeatureScreensRemainInInventory) {
    EXPECT_EQ(static_cast<int>(Screen::Map), 6);
    EXPECT_EQ(static_cast<int>(Screen::Advertise), 7);
    EXPECT_EQ(static_cast<int>(Screen::Settings), 8);
    EXPECT_EQ(static_cast<int>(Screen::Trace), 9);
    EXPECT_EQ(static_cast<int>(Screen::Terminal), 10);
    EXPECT_EQ(static_cast<int>(Screen::Signal), 11);
    EXPECT_EQ(static_cast<int>(Screen::RadioSetup), 12);
    EXPECT_EQ(static_cast<int>(Screen::Repeaters), 13);
    EXPECT_EQ(static_cast<int>(Screen::Onboarding), 14);
}

TEST(NavigationContractTest, SettingsAndDetailScreensRemainInInventory) {
    EXPECT_EQ(static_cast<int>(Screen::ContactDetail), 15);
    EXPECT_EQ(static_cast<int>(Screen::SettingsRadio), 16);
    EXPECT_EQ(static_cast<int>(Screen::SettingsGPS), 17);
    EXPECT_EQ(static_cast<int>(Screen::SettingsDisplay), 18);
    EXPECT_EQ(static_cast<int>(Screen::SettingsSystem), 19);
}

TEST(NavigationContractTest, DiagnosticsAndConnectivityScreensRemainInInventory) {
    EXPECT_EQ(static_cast<int>(Screen::NodeStats), 20);
    EXPECT_EQ(static_cast<int>(Screen::Telemetry), 21);
    EXPECT_EQ(static_cast<int>(Screen::NodeStatus), 22);
    EXPECT_EQ(static_cast<int>(Screen::WiFiNetworks), 23);
    EXPECT_EQ(static_cast<int>(Screen::Bluetooth), 24);
    EXPECT_EQ(static_cast<int>(Screen::Regions), 25);
}

TEST(NavigationContractTest, NestedRoutesRemainInInventory) {
    EXPECT_EQ(static_cast<int>(Screen::RepeaterDetail), 26);
    EXPECT_EQ(static_cast<int>(Screen::CustomRadioSetup), 27);
    EXPECT_EQ(static_cast<int>(Screen::MessageSearch), 28);
    EXPECT_EQ(static_cast<int>(Screen::MeshDashboard), 29);
    EXPECT_EQ(static_cast<int>(Screen::FileBrowser), 30);
}

TEST(NavigationContractTest, ParameterizedRouteAPIsExist) {
    using contact_route_fn = void (*)(const char*);
    using repeater_route_fn = void (*)(const char*, bool);
    using custom_route_fn = void (*)();

    (void)static_cast<contact_route_fn>(sigurdos::ui::navigate_to_contact_detail);
    (void)static_cast<repeater_route_fn>(sigurdos::ui::navigate_to_repeater_detail);
    (void)static_cast<custom_route_fn>(sigurdos::ui::navigate_to_custom_radio_setup);
    SUCCEED();
}

TEST(NavigationContractTest, DeferredRefreshRequiresMatchingScreenGeneration) {
    EXPECT_TRUE(sigurdos::ui::screen_refresh_matches(
        Screen::NodeStats, Screen::NodeStats));
    EXPECT_FALSE(sigurdos::ui::screen_refresh_matches(
        Screen::NodeStats, Screen::Home));

    using guarded_refresh_fn = bool (*)(Screen);
    (void)static_cast<guarded_refresh_fn>(
        sigurdos::ui::refresh_current_screen_if);
}

TEST(NavigationContractTest, NodeStatsResetDefersGuardedRefreshWithoutDeletingRoot) {
    const std::string source = read_project_file(
        "src/ui/screens/screen_node_stats.cpp");
    ASSERT_FALSE(source.empty());

    const size_t reset_pos = source.find(
        "sigurdos::mesh::resetPacketStats();");
    const size_t async_pos = source.find("lv_async_call(", reset_pos);
    const size_t guarded_pos = source.find(
        "refresh_current_screen_if(Screen::NodeStats)", async_pos);

    ASSERT_NE(reset_pos, std::string::npos);
    ASSERT_NE(async_pos, std::string::npos);
    ASSERT_NE(guarded_pos, std::string::npos);
    EXPECT_LT(reset_pos, async_pos);
    EXPECT_LT(async_pos, guarded_pos);

    const std::string reset_callback = source.substr(reset_pos,
        guarded_pos + std::string("refresh_current_screen_if(Screen::NodeStats)").size()
            - reset_pos);
    EXPECT_EQ(reset_callback.find("lv_obj_del_async"), std::string::npos);
    EXPECT_EQ(reset_callback.find("navigate_to(Screen::NodeStats)"),
              std::string::npos);
}

} // namespace
