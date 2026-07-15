// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Heartbeat ring buffer — stores the last 48 heartbeat
// snapshots in PSRAM (DRAM fallback) for agent query.
//
// Gated behind SIGURDOS_TELEMETRY.

#ifndef SIGURDOS_TELEMETRY_HB_RING_H
#define SIGURDOS_TELEMETRY_HB_RING_H

#include <cstdint>

namespace sigurdos {
namespace telemetry {

static constexpr uint32_t HB_RING_SIZE = 48;

struct HbRingEntry {
    uint32_t t_s;        // uptime seconds
    uint32_t h;          // free heap
    uint32_t hm;         // min free heap
    uint32_t p;          // free PSRAM
    uint8_t  b;          // battery %
    int16_t  rssi;       // RSSI × 4 (quarter-dB precision)
    uint16_t wt;         // widget count
    uint8_t  flags;      // bit 0: 0=full, 1=diff
};

// ── Ring buffer public API ────────────────────────────

// Allocate the ring buffer (PSRAM preferred, DRAM fallback).
// Returns false when neither allocation succeeds.
bool hb_ring_init();

// Push a new entry into the ring buffer.
void hb_ring_push(const HbRingEntry& entry);

// Query: emit last N entries as @hb-ring records.
// Returns the number of entries emitted.
uint32_t hb_ring_query(uint32_t n);

// Get total number of entries pushed since init.
uint32_t hb_ring_count();

// Get a specific entry by logical index (0-based, oldest first).
bool hb_ring_get(uint32_t index, HbRingEntry* out);

// Clear the ring buffer (reset head and count, zero memory).
void hb_ring_clear();

}  // namespace telemetry
}  // namespace sigurdos

#endif // SIGURDOS_TELEMETRY_HB_RING_H
