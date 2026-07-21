#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// Remote test controller — allows controlling the T-Deck over serial for
// automated and manual testing. Enabled by SIGURDOS_REMOTE_TEST=1 build flag.
//
// SAFETY: SIGURDOS_REMOTE_TEST without SIGURDOS_REMOTE_TEST_RADIO initializes
// the shared SPI bus but does not create the LoRa radio driver. RF transmission
// is only available in radio-enabled test profiles such as
// SigurdOS_TDeck_remote_test_radio.

#include <cstdint>
#include <cstdio>

// Remote diagnostics run on the same cooperative loop as the UI and mesh.
// Keep their resource policies visible here so native tests can lock the
// bounds without linking the hardware-only controller implementation.
static constexpr uint16_t SIGURDOS_TEST_EMOJI_PAGE_SIZE = 24;
static constexpr uint16_t SIGURDOS_TEST_EMOJI_FIXED_OBJECTS = 10;
static constexpr uint16_t SIGURDOS_TEST_EMOJI_OBJECT_LIMIT = 64;
static constexpr uint8_t SIGURDOS_TEST_TREE_MAX_DEPTH = 8;
static constexpr uint16_t SIGURDOS_TEST_TREE_MAX_NODES = 128;
static constexpr uint32_t SIGURDOS_TEST_CAPTURE_MAX_WIDTH = 320;
static constexpr uint32_t SIGURDOS_TEST_CAPTURE_MAX_HEIGHT = 240;
static constexpr uint32_t SIGURDOS_TEST_CAPTURE_MAX_BYTES =
    SIGURDOS_TEST_CAPTURE_MAX_WIDTH * SIGURDOS_TEST_CAPTURE_MAX_HEIGHT * 2U;

inline uint16_t sigurdos_test_controller_emoji_page_count(uint16_t item_count) {
    return item_count == 0
               ? 0
               : static_cast<uint16_t>((item_count + SIGURDOS_TEST_EMOJI_PAGE_SIZE - 1U) /
                                       SIGURDOS_TEST_EMOJI_PAGE_SIZE);
}

inline uint16_t sigurdos_test_controller_emoji_page_items(uint16_t item_count,
                                                           uint16_t page) {
    const uint32_t first = static_cast<uint32_t>(page) * SIGURDOS_TEST_EMOJI_PAGE_SIZE;
    if (first >= item_count) return 0;
    const uint32_t remaining = item_count - first;
    return static_cast<uint16_t>(remaining > SIGURDOS_TEST_EMOJI_PAGE_SIZE
                                     ? SIGURDOS_TEST_EMOJI_PAGE_SIZE
                                     : remaining);
}

inline bool sigurdos_test_controller_emoji_object_count_allowed(uint16_t page_items) {
    const uint32_t objects = SIGURDOS_TEST_EMOJI_FIXED_OBJECTS +
                             static_cast<uint32_t>(page_items) * 2U;
    return page_items <= SIGURDOS_TEST_EMOJI_PAGE_SIZE &&
           objects <= SIGURDOS_TEST_EMOJI_OBJECT_LIMIT;
}

inline bool sigurdos_test_controller_tree_can_descend(uint8_t depth) {
    return depth < SIGURDOS_TEST_TREE_MAX_DEPTH;
}

inline bool sigurdos_test_controller_capture_size_allowed(uint32_t width,
                                                           uint32_t height,
                                                           uint32_t stride) {
    if (width == 0 || height == 0 || stride < width * 2U) return false;
    if (width > SIGURDOS_TEST_CAPTURE_MAX_WIDTH ||
        height > SIGURDOS_TEST_CAPTURE_MAX_HEIGHT) {
        return false;
    }
    const uint64_t total = static_cast<uint64_t>(stride) * height;
    return total <= SIGURDOS_TEST_CAPTURE_MAX_BYTES;
}

inline bool sigurdos_test_controller_timeout_elapsed(uint32_t now,
                                                      uint32_t started,
                                                      uint32_t timeout) {
    // A command can record its start after the cooperative loop sampled `now`.
    // Treat that small negative delta as not elapsed, while retaining normal
    // uint32_t millis() wrap handling for positive intervals under 2^31 ms.
    const int32_t elapsed = static_cast<int32_t>(now - started);
    return elapsed >= 0 && static_cast<uint32_t>(elapsed) > timeout;
}

struct SigurdOSTestRfParams {
    float freq;
    uint8_t sf;
    float bw;
    uint8_t cr;
    int8_t tx_power_dbm;
    bool rx_boosted_gain;  // 6th optional arg (0 or 1), defaults to false when absent
};

enum class SigurdOSTestRfParseResult : uint8_t {
    Ok,
    MissingArgs,
    BadArgumentCount,
    FrequencyOutOfRange,
    SpreadingFactorOutOfRange,
    BandwidthOutOfRange,
    CodingRateOutOfRange,
    TxPowerOutOfRange,
};

inline SigurdOSTestRfParseResult
sigurdos_test_controller_parse_rf_params(const char* arg,
                                         SigurdOSTestRfParams* out,
                                         int* parsed_fields = nullptr) {
    if (parsed_fields) *parsed_fields = 0;
    if (!arg || !out) return SigurdOSTestRfParseResult::MissingArgs;

    float freq = 0.0f;
    int sf = 0;
    float bw = 0.0f;
    int cr = 0;
    int tx_pwr = 0;
    int rx_boost = 0;  // optional 6th: 0 or 1, absent = 0
    const int n = sscanf(arg, "%f %d %f %d %d %d", &freq, &sf, &bw, &cr, &tx_pwr, &rx_boost);
    if (parsed_fields) *parsed_fields = n;
    // Accept 5 or 6 args; 6th is optional
    if (n < 5 || n > 6) return SigurdOSTestRfParseResult::BadArgumentCount;

    if (freq < 400.0f || freq > 1000.0f) {
        return SigurdOSTestRfParseResult::FrequencyOutOfRange;
    }
    if (sf < 6 || sf > 12) {
        return SigurdOSTestRfParseResult::SpreadingFactorOutOfRange;
    }
    if (bw < 7.8f || bw > 500.0f) {
        return SigurdOSTestRfParseResult::BandwidthOutOfRange;
    }
    if (cr < 5 || cr > 8) {
        return SigurdOSTestRfParseResult::CodingRateOutOfRange;
    }
    if (tx_pwr < 2 || tx_pwr > 22) {
        return SigurdOSTestRfParseResult::TxPowerOutOfRange;
    }

    out->freq = freq;
    out->sf = static_cast<uint8_t>(sf);
    out->bw = bw;
    out->cr = static_cast<uint8_t>(cr);
    out->tx_power_dbm = static_cast<int8_t>(tx_pwr);
    out->rx_boosted_gain = (rx_boost != 0);
    return SigurdOSTestRfParseResult::Ok;
}

void sigurdos_test_controller_init();
void sigurdos_test_controller_loop();

// Handle a single command string (for programmatic use or parsing).
// Returns true only when the command name was recognised. Null, empty,
// comment-only, and unknown command lines return false.
bool sigurdos_test_controller_exec(const char* cmd);

// ── New test controller API functions ──────────────────────
void sigurdos_test_controller_tap(int x, int y);
void sigurdos_test_controller_scroll(int x, int y, int dx, int dy);
