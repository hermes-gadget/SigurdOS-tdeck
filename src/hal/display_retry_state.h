// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#pragma once

#include <cstdint>

namespace sigurdos {
namespace hal {

constexpr uint32_t DISPLAY_RETRY_MAGIC = 0x44525331u;  // "DRS1"
constexpr uint8_t DISPLAY_RETRY_VERSION = 1;
constexpr uint8_t DISPLAY_RETRY_PENDING = 0xA5;
constexpr uint8_t MAX_DISPLAY_FAILURES = 3;

struct DisplayRetryState {
    uint32_t magic;
    uint8_t version;
    uint8_t failures;
    uint8_t retry_pending;
    uint8_t reserved;
};

inline void display_retry_clear(DisplayRetryState& state) {
    state = DisplayRetryState{
        DISPLAY_RETRY_MAGIC,
        DISPLAY_RETRY_VERSION,
        0,
        0,
        0,
    };
}

inline bool display_retry_valid(const DisplayRetryState& state) {
    return state.magic == DISPLAY_RETRY_MAGIC &&
           state.version == DISPLAY_RETRY_VERSION &&
           state.failures <= MAX_DISPLAY_FAILURES &&
           (state.retry_pending == 0 ||
            state.retry_pending == DISPLAY_RETRY_PENDING) &&
           state.reserved == 0;
}

// Consume a retry marker only when the previous failed display attempt issued
// ESP.restart(), whose next boot reports ESP_RST_SW. Power-on, brownout, crash,
// deep-sleep, and unrelated resets all begin a fresh retry sequence.
inline bool display_retry_begin(DisplayRetryState& state,
                                bool software_reset) {
    const bool continue_retry = display_retry_valid(state) &&
                                software_reset &&
                                state.retry_pending == DISPLAY_RETRY_PENDING;
    const uint8_t retained_failures = continue_retry ? state.failures : 0;
    display_retry_clear(state);
    state.failures = retained_failures;
    return continue_retry;
}

inline uint8_t display_retry_note_failure(DisplayRetryState& state) {
    if (!display_retry_valid(state)) display_retry_clear(state);
    if (state.failures < MAX_DISPLAY_FAILURES) ++state.failures;
    state.retry_pending = DISPLAY_RETRY_PENDING;
    return state.failures;
}

inline bool display_retry_should_halt(const DisplayRetryState& state) {
    return display_retry_valid(state) &&
           state.failures >= MAX_DISPLAY_FAILURES;
}

}  // namespace hal
}  // namespace sigurdos
