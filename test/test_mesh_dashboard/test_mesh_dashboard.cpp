// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
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

#include <gtest/gtest.h>

#include "ui/mesh_dashboard_metrics.h"

using namespace sigurdos::ui;

namespace {

// Mirror of the fields dashboard_average_* reads from mesh::ContactInfo.
struct FakeContact {
    int rssi;
    float snr;
};

TEST(MeshDashboardMetrics, ClampBoundsPercentage) {
    EXPECT_EQ(dashboard_clamp_pct(-5), 0);
    EXPECT_EQ(dashboard_clamp_pct(0), 0);
    EXPECT_EQ(dashboard_clamp_pct(55), 55);
    EXPECT_EQ(dashboard_clamp_pct(100), 100);
    EXPECT_EQ(dashboard_clamp_pct(140), 100);
}

TEST(MeshDashboardMetrics, AverageRssiSkipsUnheardContacts) {
    FakeContact contacts[] = {
        {-80, 5.0f},  // heard
        {0, 0.0f},    // never heard — must be excluded
        {-100, -2.0f} // heard
    };
    int heard = -1;
    float avg = dashboard_average_rssi(contacts, 3, &heard);
    EXPECT_EQ(heard, 2);
    EXPECT_FLOAT_EQ(avg, -90.0f);
}

TEST(MeshDashboardMetrics, AverageRssiEmptyIsZeroData) {
    int heard = 7;
    EXPECT_FLOAT_EQ(dashboard_average_rssi<FakeContact>(nullptr, 0, &heard), 0.0f);
    EXPECT_EQ(heard, 0);

    FakeContact none[] = {{0, 0.0f}, {0, 0.0f}};
    EXPECT_FLOAT_EQ(dashboard_average_rssi(none, 2, &heard), 0.0f);
    EXPECT_EQ(heard, 0);
}

TEST(MeshDashboardMetrics, AverageSnrUsesSameHeardGate) {
    FakeContact contacts[] = {
        {-70, 8.0f},
        {0, 99.0f},   // unheard — its bogus SNR must not pollute the average
        {-90, 2.0f}
    };
    int heard = 0;
    float avg = dashboard_average_snr(contacts, 3, &heard);
    EXPECT_EQ(heard, 2);
    EXPECT_FLOAT_EQ(avg, 5.0f);
}

TEST(MeshDashboardMetrics, ThroughputIsPacketsPerSecond) {
    EXPECT_FLOAT_EQ(dashboard_packet_throughput(100, 10000), 10.0f);
    EXPECT_FLOAT_EQ(dashboard_packet_throughput(5, 2000), 2.5f);
}

TEST(MeshDashboardMetrics, ThroughputGuardsZeroWindow) {
    EXPECT_FLOAT_EQ(dashboard_packet_throughput(50, 0), 0.0f);
}

TEST(MeshDashboardMetrics, DutyBudgetUsageUsesConfiguredWindow) {
    EXPECT_EQ(dashboard_duty_budget_used_pct(1, 36000), 0);
    EXPECT_EQ(dashboard_duty_budget_used_pct(1, 18000), 50);
    EXPECT_EQ(dashboard_duty_budget_used_pct(1, 0), 100);
    EXPECT_EQ(dashboard_duty_budget_used_pct(0, 0), 0);
    EXPECT_EQ(dashboard_duty_budget_used_pct(1, 0, 0), 0);
}

TEST(MeshDashboardMetrics, ChannelUtilizationIsBusyFraction) {
    EXPECT_EQ(dashboard_channel_utilization_pct(2500, 10000), 25);
    EXPECT_EQ(dashboard_channel_utilization_pct(20000, 10000), 100);
    EXPECT_EQ(dashboard_channel_utilization_pct(500, 0), 0);
}

TEST(MeshDashboardMetrics, SignalHealthBands) {
    EXPECT_EQ(dashboard_signal_health(0.0f), DashboardHealth::Poor);   // no data
    EXPECT_EQ(dashboard_signal_health(-70.0f), DashboardHealth::Good);
    EXPECT_EQ(dashboard_signal_health(-85.0f), DashboardHealth::Good);
    EXPECT_EQ(dashboard_signal_health(-95.0f), DashboardHealth::Fair);
    EXPECT_EQ(dashboard_signal_health(-110.0f), DashboardHealth::Poor);
}

TEST(MeshDashboardMetrics, RssiToPercentScalesAndClamps) {
    EXPECT_EQ(dashboard_rssi_to_pct(0.0f), 0);      // no data sentinel
    EXPECT_EQ(dashboard_rssi_to_pct(-40.0f), 100);  // top of span
    EXPECT_EQ(dashboard_rssi_to_pct(-120.0f), 0);   // bottom of span
    EXPECT_EQ(dashboard_rssi_to_pct(-130.0f), 0);   // below span clamps
    EXPECT_EQ(dashboard_rssi_to_pct(-80.0f), 50);   // midpoint
}

TEST(MeshDashboardMetrics, NearbyBatteryVoltageMapsToEstimate) {
    EXPECT_EQ(dashboard_battery_voltage_to_pct(0.0f), 0);
    EXPECT_EQ(dashboard_battery_voltage_to_pct(3.3f), 0);
    EXPECT_EQ(dashboard_battery_voltage_to_pct(3.75f), 50);
    EXPECT_EQ(dashboard_battery_voltage_to_pct(4.2f), 100);
    EXPECT_EQ(dashboard_battery_voltage_to_pct(4.5f), 100);
}

} // namespace
