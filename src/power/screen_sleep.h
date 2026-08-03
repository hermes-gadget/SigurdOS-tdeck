#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// The structure of this small policy module follows the GPL-3.0-or-later
// TouchSleep reference from ALLFATHER-BV/wadamesh
// (src/ui-touch/TouchSleep.*). SigurdOS deliberately keeps the radio and both
// ESP32-S3 cores awake: the implementation throttles the main loop with
// vTaskDelay and never enters light sleep.

#include <cstdint>

namespace sigurdos::power {

enum class WakeReason : uint8_t {
    None = 0,
    Packet,
    Touch,
    Button,
    Timer,
    Other,
};

constexpr uint32_t NO_WAKE_DEADLINE_MS = 0xFFFFFFFFu;
constexpr uint32_t DEFAULT_THROTTLE_MS = 50u;
constexpr uint8_t WAKE_LOG_CAPACITY = 8u;

// A snapshot is intentionally data-only so the predicate matrix can be
// tested on the host without pulling in Arduino, LVGL, WiFi, or MeshCore.
struct PredicateState {
    bool screen_off = false;
    bool no_companion_clients = false;
    bool wifi_off = false;
    bool ble_off = false;
    bool on_battery = false;
    bool mesh_idle = false;
    uint32_t next_wake_in_ms = NO_WAKE_DEADLINE_MS;
};

bool idle_predicates_allow(const PredicateState& state);
uint32_t throttle_delay_ms(
    uint32_t next_wake_in_ms,
    uint32_t quantum_ms = DEFAULT_THROTTLE_MS);
const char* wake_reason_name(WakeReason reason);

struct WakeEvent {
    WakeReason reason = WakeReason::None;
    uint32_t now_ms = 0;
    uint32_t epoch = 0;
};

struct Hooks {
    bool (*screenOff)() = nullptr;
    bool (*noClient)() = nullptr;
    bool (*wifiOff)() = nullptr;
    bool (*bleOff)() = nullptr;
    bool (*onBattery)() = nullptr;
    bool (*meshIdle)() = nullptr;

    // Return milliseconds until a service deadline must be serviced. Use
    // NO_WAKE_DEADLINE_MS when there is no forcing deadline.
    uint32_t (*nextWakeForcingInMs)(uint32_t now_ms) = nullptr;
    uint32_t (*epochNow)() = nullptr;

    // Return a newly observed wake source. The hook is polled once per main
    // loop, including while the loop is not throttled, so edge counters can be
    // primed before the first idle transition.
    WakeReason (*pollWakeReason)() = nullptr;

    // Called for timer/input/packet wake before the transition callback.
    void (*wakeDisplay)() = nullptr;

    // Tests can inject a deterministic recorder. Production uses the
    // vTaskDelay implementation in screen_sleep.cpp.
    void (*delayMs)(uint32_t delay_ms) = nullptr;
};

using TransitionCallback = void (*)(uint32_t epoch, bool entering);
using WakeCallback = void (*)(const WakeEvent& event);

void begin(const Hooks& hooks);
void setEnabled(bool enabled);
bool enabled();

void onTransition(TransitionCallback callback);
void onWake(WakeCallback callback);

// Call once at the true end of the firmware main loop. When all predicates
// pass, this marks the loop throttled and delays for at most 50 ms (or until
// the next forcing deadline). It does not stop MeshCore radio processing.
void loopEnd(uint32_t now_ms);

// Explicit wake notification for integrations that already have a precise
// event source. The normal firmware path uses pollWakeReason().
void notifyWake(WakeReason reason, uint32_t now_ms);

bool throttling();
uint32_t wakeCount();
WakeReason lastWakeReason();
uint8_t wakeLogSize();
bool wakeLogAt(uint8_t oldest_first_index, WakeEvent* out);
uint8_t throttlePercent();

} // namespace sigurdos::power
