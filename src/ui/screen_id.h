#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

namespace sigurdos::ui {

// UI destinations are kept independent of LVGL so routing and navigation
// policy can be exercised by native tests using the production definitions.
enum class Screen {
    Home,
    Chat,
    Contacts,
    Channels,
    Network,
    Heard,
    Map,
    Advertise,
    Settings,
    Trace,
    Terminal,
    Signal,
    RadioSetup,
    Repeaters,
    Onboarding,
    ContactDetail,
    SettingsRadio,
    SettingsGPS,
    SettingsDisplay,
    SettingsSystem,
    NodeStats,
    Telemetry,
    NodeStatus,
    WiFiNetworks,
    Bluetooth,
    Regions,
    COUNT
};

} // namespace sigurdos::ui
