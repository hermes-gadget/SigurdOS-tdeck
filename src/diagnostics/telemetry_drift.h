// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#ifndef SIGURDOS_TELEMETRY_DRIFT_H
#define SIGURDOS_TELEMETRY_DRIFT_H

#include <cstdint>

namespace sigurdos {
namespace telemetry {

// Fixed-window endpoint drift detector. A window compares the first sample
// with the latest sample at the boundary; transient in-window dips recover
// without producing a leak alert.
struct DriftDetector {
    int32_t baseline = 0;
    int32_t current = 0;
    uint32_t window_start_ms = 0;
    uint32_t window_ms = 0;
    int32_t threshold = 0;
    bool active = false;

    void begin(int32_t value, uint32_t now_ms, uint32_t duration_ms,
               int32_t report_threshold) {
        baseline = current = value;
        window_start_ms = now_ms;
        window_ms = duration_ms;
        threshold = report_threshold;
        active = true;
    }

    void update(int32_t value) {
        current = value;
    }

    bool window_elapsed(uint32_t now_ms) const {
        return active && window_ms > 0 &&
               static_cast<uint32_t>(now_ms - window_start_ms) >= window_ms;
    }

    int32_t delta() const {
        return baseline - current;
    }

    bool should_report(uint32_t now_ms) const {
        return window_elapsed(now_ms) && threshold > 0 && delta() > threshold;
    }

    const char* trend_str() const {
        const int32_t change = current - baseline;
        if (change > threshold / 2) return "rising";
        if (change < -threshold / 2) return "falling";
        return "stable";
    }

    void rotate(uint32_t now_ms) {
        baseline = current;
        window_start_ms = now_ms;
    }
};

}  // namespace telemetry
}  // namespace sigurdos

#endif  // SIGURDOS_TELEMETRY_DRIFT_H
