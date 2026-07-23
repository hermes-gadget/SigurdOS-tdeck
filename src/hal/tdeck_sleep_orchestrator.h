#pragma once

#include <cstdint>

namespace sigurdos {

enum class TDeckSleepStatus : uint8_t {
    Ready,
    Inhibited,
    WakeConfigurationFailed,
    PeripheralRailHoldFailed,
    DeepSleepReturned,
};

inline uint32_t tdeck_sleep_wake_seconds(uint32_t requested_seconds) {
    return requested_seconds > 0 ? requested_seconds : 900U;
}

inline bool tdeck_wake_configuration_succeeded(int error) {
    return error == 0;
}

inline TDeckSleepStatus tdeck_sleep_preflight(bool inhibited,
                                               int timer_error) {
    if (inhibited) return TDeckSleepStatus::Inhibited;
    if (!tdeck_wake_configuration_succeeded(timer_error)) {
        return TDeckSleepStatus::WakeConfigurationFailed;
    }
    return TDeckSleepStatus::Ready;
}

inline const char* tdeck_sleep_status_name(TDeckSleepStatus status) {
    switch (status) {
    case TDeckSleepStatus::Ready:
        return "ready";
    case TDeckSleepStatus::Inhibited:
        return "inhibited";
    case TDeckSleepStatus::WakeConfigurationFailed:
        return "wake configuration failed";
    case TDeckSleepStatus::PeripheralRailHoldFailed:
        return "peripheral rail hold failed";
    case TDeckSleepStatus::DeepSleepReturned:
        return "deep sleep returned";
    }
    return "unknown";
}

// Operations is the production T-Deck adapter in TDeckBoard, or a native fake.
// A failed mandatory wake source is detected before any peripheral mutation.
// Once quiescing begins, the only valid destination is deep sleep.
template <typename Operations>
TDeckSleepStatus tdeck_orchestrate_sleep(Operations& operations,
                                         bool inhibited,
                                         uint32_t requested_seconds) {
    if (inhibited) return TDeckSleepStatus::Inhibited;

    const uint64_t wake_us =
        static_cast<uint64_t>(tdeck_sleep_wake_seconds(requested_seconds)) *
        1000000ULL;
    if (!tdeck_wake_configuration_succeeded(
            operations.enableTimerWake(wake_us))) {
        return TDeckSleepStatus::WakeConfigurationFailed;
    }

    operations.quiescePeripheralRail();
    operations.configureOptionalRadioWake();
    operations.removePeripheralPower();
    operations.settlePeripheralRail();
    if (!tdeck_wake_configuration_succeeded(
            operations.holdPeripheralRail())) {
        return TDeckSleepStatus::PeripheralRailHoldFailed;
    }
    operations.enableDeepSleepHold();
    operations.enterDeepSleep();

    // ESP-IDF specifies deep-sleep entry as non-returning. Exposing this
    // impossible result makes platform faults and native fake behavior clear.
    return TDeckSleepStatus::DeepSleepReturned;
}

}  // namespace sigurdos
