// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "screen_sleep.h"

#include <algorithm>

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace sigurdos::power {
namespace {

Hooks g_hooks{};
TransitionCallback g_transition_callback = nullptr;
WakeCallback g_wake_callback = nullptr;
bool g_enabled = false;
bool g_throttling = false;

bool g_have_loop_time = false;
uint32_t g_last_loop_ms = 0;
uint64_t g_observed_ms = 0;
uint64_t g_throttled_ms = 0;

uint32_t g_wake_count = 0;
WakeReason g_last_wake_reason = WakeReason::None;
WakeEvent g_wake_log[WAKE_LOG_CAPACITY]{};
uint8_t g_wake_log_head = 0;
uint8_t g_wake_log_count = 0;

void default_delay(uint32_t delay_ms)
{
    if (delay_ms == 0) return;

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
    TickType_t ticks = pdMS_TO_TICKS(delay_ms);
    if (ticks == 0) ticks = 1;
    vTaskDelay(ticks);
#else
    // The host state-machine tests must not sleep the test runner. The
    // injected delay hook is available when a test needs to observe it.
    (void)delay_ms;
#endif
}

void emit_transition(bool entering)
{
    if (!g_transition_callback) return;
    const uint32_t epoch = g_hooks.epochNow ? g_hooks.epochNow() : 0;
    g_transition_callback(epoch, entering);
}

PredicateState read_predicates(uint32_t now_ms)
{
    PredicateState state{};
    state.screen_off = g_hooks.screenOff && g_hooks.screenOff();
    state.no_companion_clients = g_hooks.noClient && g_hooks.noClient();
    state.wifi_off = g_hooks.wifiOff && g_hooks.wifiOff();
    state.ble_off = g_hooks.bleOff && g_hooks.bleOff();
    state.on_battery = g_hooks.onBattery && g_hooks.onBattery();
    state.mesh_idle = g_hooks.meshIdle && g_hooks.meshIdle();
    state.next_wake_in_ms = g_hooks.nextWakeForcingInMs
        ? g_hooks.nextWakeForcingInMs(now_ms)
        : NO_WAKE_DEADLINE_MS;
    return state;
}

void record_wake(WakeReason reason, uint32_t now_ms)
{
    WakeEvent event{};
    event.reason = reason;
    event.now_ms = now_ms;
    event.epoch = g_hooks.epochNow ? g_hooks.epochNow() : 0;

    g_wake_log[g_wake_log_head] = event;
    g_wake_log_head = static_cast<uint8_t>(
        (g_wake_log_head + 1u) % WAKE_LOG_CAPACITY);
    if (g_wake_log_count < WAKE_LOG_CAPACITY) ++g_wake_log_count;

    ++g_wake_count;
    g_last_wake_reason = reason;
}

void leave_throttle()
{
    if (!g_throttling) return;
    g_throttling = false;
    emit_transition(false);
}

void wake_internal(WakeReason reason, uint32_t now_ms)
{
    if (!g_throttling) return;
    if (reason == WakeReason::None) reason = WakeReason::Other;

    record_wake(reason, now_ms);
    if (g_hooks.wakeDisplay) g_hooks.wakeDisplay();
    leave_throttle();

    if (g_wake_callback) {
        WakeEvent event = g_wake_log[
            static_cast<uint8_t>((g_wake_log_head + WAKE_LOG_CAPACITY - 1u) %
                                 WAKE_LOG_CAPACITY)];
        g_wake_callback(event);
    }
}

void reset_runtime_state()
{
    g_throttling = false;
    g_have_loop_time = false;
    g_last_loop_ms = 0;
    g_observed_ms = 0;
    g_throttled_ms = 0;
    g_wake_count = 0;
    g_last_wake_reason = WakeReason::None;
    g_wake_log_head = 0;
    g_wake_log_count = 0;
    for (auto& event : g_wake_log) event = WakeEvent{};
}

} // namespace

bool idle_predicates_allow(const PredicateState& state)
{
    return state.screen_off && state.no_companion_clients && state.wifi_off &&
           state.ble_off && state.on_battery && state.mesh_idle;
}

uint32_t throttle_delay_ms(uint32_t next_wake_in_ms, uint32_t quantum_ms)
{
    if (quantum_ms == 0 || next_wake_in_ms == 0) return 0;
    if (next_wake_in_ms == NO_WAKE_DEADLINE_MS) return quantum_ms;
    return std::min(next_wake_in_ms, quantum_ms);
}

const char* wake_reason_name(WakeReason reason)
{
    switch (reason) {
    case WakeReason::Packet: return "packet";
    case WakeReason::Touch:  return "touch";
    case WakeReason::Button: return "button";
    case WakeReason::Timer:  return "timer";
    case WakeReason::Other:  return "other";
    case WakeReason::None:
    default:                 return "none";
    }
}

void begin(const Hooks& hooks)
{
    g_hooks = hooks;
    g_transition_callback = nullptr;
    g_wake_callback = nullptr;
    g_enabled = true;
    reset_runtime_state();
}

void setEnabled(bool enabled_value)
{
    if (!enabled_value && g_throttling) leave_throttle();
    g_enabled = enabled_value;
}

bool enabled() { return g_enabled; }

void onTransition(TransitionCallback callback)
{
    g_transition_callback = callback;
}

void onWake(WakeCallback callback)
{
    g_wake_callback = callback;
}

void notifyWake(WakeReason reason, uint32_t now_ms)
{
    wake_internal(reason, now_ms);
}

void loopEnd(uint32_t now_ms)
{
    if (g_have_loop_time) {
        const uint32_t elapsed = static_cast<uint32_t>(now_ms - g_last_loop_ms);
        g_observed_ms += elapsed;
        if (g_throttling) g_throttled_ms += elapsed;
    } else {
        g_have_loop_time = true;
    }
    g_last_loop_ms = now_ms;

    if (!g_enabled) return;

    const WakeReason polled_reason = g_hooks.pollWakeReason
        ? g_hooks.pollWakeReason()
        : WakeReason::None;
    const PredicateState state = read_predicates(now_ms);

    if (g_throttling) {
        if (polled_reason != WakeReason::None) {
            wake_internal(polled_reason, now_ms);
            return;
        }
        if (state.next_wake_in_ms == 0) {
            wake_internal(WakeReason::Timer, now_ms);
            return;
        }
        if (!idle_predicates_allow(state)) {
            leave_throttle();
            return;
        }
    } else {
        if (!idle_predicates_allow(state)) return;
        g_throttling = true;
        emit_transition(true);
    }

    const uint32_t delay_ms = throttle_delay_ms(state.next_wake_in_ms);
    if (delay_ms == 0) return;
    if (g_hooks.delayMs) g_hooks.delayMs(delay_ms);
    else default_delay(delay_ms);
}

bool throttling() { return g_throttling; }

uint32_t wakeCount() { return g_wake_count; }

WakeReason lastWakeReason() { return g_last_wake_reason; }

uint8_t wakeLogSize() { return g_wake_log_count; }

bool wakeLogAt(uint8_t oldest_first_index, WakeEvent* out)
{
    if (!out || oldest_first_index >= g_wake_log_count) return false;
    const uint8_t oldest = static_cast<uint8_t>(
        (g_wake_log_head + WAKE_LOG_CAPACITY - g_wake_log_count) %
        WAKE_LOG_CAPACITY);
    const uint8_t index = static_cast<uint8_t>(
        (oldest + oldest_first_index) % WAKE_LOG_CAPACITY);
    *out = g_wake_log[index];
    return true;
}

uint8_t throttlePercent()
{
    if (g_observed_ms == 0) return 0;
    const uint64_t percentage = (g_throttled_ms * 100u) / g_observed_ms;
    return static_cast<uint8_t>(percentage > 100u ? 100u : percentage);
}

} // namespace sigurdos::power
