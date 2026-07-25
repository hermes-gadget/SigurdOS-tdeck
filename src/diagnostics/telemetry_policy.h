#pragma once

#include "../ui/navigation.h"

#include <cstddef>
#include <cstdint>

namespace sigurdos::telemetry {

inline const char* screen_name(sigurdos::ui::Screen screen) {
    using sigurdos::ui::Screen;
    switch (screen) {
        case Screen::Home: return "Home";
        case Screen::Chat: return "Chat";
        case Screen::Contacts: return "Contacts";
        case Screen::Channels: return "Channels";
        case Screen::Network: return "Network";
        case Screen::Heard: return "Heard";
        case Screen::Map: return "Map";
        case Screen::Advertise: return "Advertise";
        case Screen::Settings: return "Settings";
        case Screen::Trace: return "Trace";
        case Screen::Terminal: return "Terminal";
        case Screen::Signal: return "Signal";
        case Screen::RadioSetup: return "RadioSetup";
        case Screen::Repeaters: return "Repeaters";
        case Screen::Onboarding: return "Onboarding";
        case Screen::ContactDetail: return "ContactDetail";
        case Screen::SettingsRadio: return "SettingsRadio";
        case Screen::SettingsGPS: return "SettingsGPS";
        case Screen::SettingsDisplay: return "SettingsDisplay";
        case Screen::SettingsSystem: return "SettingsSystem";
        case Screen::NodeStats: return "NodeStats";
        case Screen::Telemetry: return "Telemetry";
        case Screen::NodeStatus: return "NodeStatus";
        case Screen::WiFiNetworks: return "WiFiNetworks";
        case Screen::Bluetooth: return "Bluetooth";
        case Screen::Regions: return "Regions";
        case Screen::RepeaterDetail: return "RepeaterDetail";
        case Screen::CustomRadioSetup: return "CustomRadioSetup";
        case Screen::MessageSearch: return "MessageSearch";
        case Screen::MeshDashboard: return "MeshDashboard";
        case Screen::FileBrowser: return "FileBrowser";
        case Screen::COUNT: break;
    }
    return "?";
}

struct WidgetTreeCount {
    uint16_t count;
    bool truncated;
};

template <typename Node, typename ChildCountFn, typename ChildFn>
WidgetTreeCount count_widget_tree(Node root, ChildCountFn child_count,
                                  ChildFn child_at) {
    static constexpr std::size_t MAX_PENDING = 64;
    static constexpr uint32_t MAX_NODES = 4096;
    static constexpr uint8_t MAX_DEPTH = 32;
    struct Pending { Node node; uint8_t depth; };
    Pending pending[MAX_PENDING];
    std::size_t pending_count = 0;
    uint32_t count = 0;
    bool truncated = false;

    if (!root) return {0, false};
    pending[pending_count++] = {root, 0};
    while (pending_count > 0 && count < MAX_NODES) {
        const Pending current = pending[--pending_count];
        if (!current.node) continue;
        ++count;
        const uint32_t children = child_count(current.node);
        if (children > 0 && current.depth >= MAX_DEPTH) {
            truncated = true;
            continue;
        }
        for (uint32_t i = children; i > 0; --i) {
            Node child = child_at(current.node, i - 1);
            if (!child) continue;
            if (pending_count == MAX_PENDING) {
                truncated = true;
                continue;
            }
            pending[pending_count++] = {
                child, static_cast<uint8_t>(current.depth + 1)};
        }
    }
    if (pending_count > 0) truncated = true;
    return {static_cast<uint16_t>(count), truncated};
}

}  // namespace sigurdos::telemetry
