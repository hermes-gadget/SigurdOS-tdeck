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
// Returns true if the command was recognised.
bool sigurdos_test_controller_exec(const char* cmd);

// ── New test controller API functions ──────────────────────
void sigurdos_test_controller_tap(int x, int y);
void sigurdos_test_controller_scroll(int x, int y, int dx, int dy);
