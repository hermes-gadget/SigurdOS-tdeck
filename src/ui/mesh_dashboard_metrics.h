#pragma once

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

// Pure, hardware-free helpers for the Mesh Health Dashboard screen.
//
// The dashboard renderer (screen_mesh_dashboard.cpp) is a thin LVGL shell over
// these functions so the aggregation/scaling logic can be unit-tested without a
// display or radio. Keep every function here free of LVGL, Arduino, and mesh
// wrapper dependencies — callers pass in already-sampled values.

#include <cstdint>

namespace sigurdos::ui {

// Clamp an integer percentage into the 0..100 range so arc/bar widgets never
// receive an out-of-range value from noisy counters.
inline int dashboard_clamp_pct(int pct)
{
    if (pct < 0) return 0;
    if (pct > 100) return 100;
    return pct;
}

// Aggregated signal quality for packet/contact samples. `count_out` reports how
// many samples were valid so the UI can distinguish "0 dBm average" from
// "no data".
template <typename Contact>
float dashboard_average_rssi(const Contact* contacts, int count, int* count_out = nullptr)
{
    if (count_out) *count_out = 0;
    if (!contacts || count <= 0) return 0.0f;

    long sum = 0;
    int valid = 0;
    for (int i = 0; i < count; i++) {
        // rssi == 0 means "never heard" for a contact — skip it so the average
        // reflects nodes we actually have signal data for.
        if (contacts[i].rssi == 0) continue;
        sum += contacts[i].rssi;
        valid++;
    }
    if (count_out) *count_out = valid;
    if (valid == 0) return 0.0f;
    return static_cast<float>(sum) / static_cast<float>(valid);
}

template <typename Contact>
float dashboard_average_snr(const Contact* contacts, int count, int* count_out = nullptr)
{
    if (count_out) *count_out = 0;
    if (!contacts || count <= 0) return 0.0f;

    float sum = 0.0f;
    int valid = 0;
    for (int i = 0; i < count; i++) {
        if (contacts[i].rssi == 0) continue;  // same "heard" gate as RSSI
        sum += contacts[i].snr;
        valid++;
    }
    if (count_out) *count_out = valid;
    if (valid == 0) return 0.0f;
    return sum / static_cast<float>(valid);
}

// Average packet rate since a reference point. `total_packets` is the sum of all
// TX/RX counters; `elapsed_ms` is the observation window. Guards divide-by-zero
// for the first loop cycle after boot.
inline float dashboard_packet_throughput(uint32_t total_packets, uint32_t elapsed_ms)
{
    if (elapsed_ms == 0) return 0.0f;
    return static_cast<float>(total_packets) * 1000.0f / static_cast<float>(elapsed_ms);
}

// Duty-cycle budget consumed inside MeshCore's rolling window. A configured
// duty cycle of zero means enforcement is disabled, so usage is reported as
// zero instead of trying to interpret MeshCore's unlimited-budget state.
inline int dashboard_duty_budget_used_pct(
    uint8_t configured_pct, unsigned long remaining_ms,
    unsigned long window_ms = 3600000UL)
{
    if (configured_pct == 0 || window_ms == 0) return 0;
    const uint64_t max_budget =
        (static_cast<uint64_t>(window_ms) * configured_pct) / 100U;
    if (max_budget == 0 || remaining_ms >= max_budget) return 0;
    const uint64_t used = max_budget - remaining_ms;
    return dashboard_clamp_pct(
        static_cast<int>((used * 100U) / max_budget));
}

// Channel utilization: fraction of wall-clock time the channel was busy carrying
// receive traffic. A saturated channel approaches 100%.
inline int dashboard_channel_utilization_pct(unsigned long rx_airtime_ms, unsigned long elapsed_ms)
{
    if (elapsed_ms == 0) return 0;
    return dashboard_clamp_pct(static_cast<int>(
        (static_cast<uint64_t>(rx_airtime_ms) * 100U) / elapsed_ms));
}

// Map a signal-quality percentage onto an accent/warn/critical band so the arc
// colour communicates health at a glance. Higher is better.
enum class DashboardHealth { Good, Fair, Poor };

inline DashboardHealth dashboard_signal_health(float avg_rssi)
{
    if (avg_rssi == 0.0f) return DashboardHealth::Poor;  // no data
    if (avg_rssi >= -85.0f) return DashboardHealth::Good;
    if (avg_rssi >= -105.0f) return DashboardHealth::Fair;
    return DashboardHealth::Poor;
}

// Convert an RSSI in dBm to a 0..100 bar percentage using the same -120..-40
// span the signal-dots helper uses. Clamped at both ends.
inline int dashboard_rssi_to_pct(float rssi)
{
    if (rssi == 0.0f) return 0;
    const float lo = -120.0f;
    const float hi = -40.0f;
    if (rssi <= lo) return 0;
    if (rssi >= hi) return 100;
    return dashboard_clamp_pct(static_cast<int>((rssi - lo) * 100.0f / (hi - lo)));
}

// Approximate a single-cell LiPo percentage for nearby-node voltage telemetry.
// The dashboard deliberately labels this as an estimate: remote nodes expose
// voltage, not a shared chemistry-specific state-of-charge value.
inline int dashboard_battery_voltage_to_pct(float volts)
{
    if (volts <= 0.0f) return 0;
    constexpr float empty = 3.3f;
    constexpr float full = 4.2f;
    if (volts <= empty) return 0;
    if (volts >= full) return 100;
    return dashboard_clamp_pct(
        static_cast<int>(((volts - empty) * 100.0f) / (full - empty)));
}

} // namespace sigurdos::ui
