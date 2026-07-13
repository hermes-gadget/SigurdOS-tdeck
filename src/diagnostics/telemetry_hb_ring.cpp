// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Heartbeat ring buffer — 48-entry circular buffer allocated
// in PSRAM with DRAM fallback. Used to replay heartbeat history
// on demand via the `query hb-ring` command.

#include "telemetry_hb_ring.h"
#include "telemetry_protocol.h"
#include "debug_cfg.h"

#if SIGURDOS_TELEMETRY

#include <Arduino.h>
#include <cstring>
#include <esp_heap_caps.h>

namespace sigurdos {
namespace telemetry {

// ── Static state ───────────────────────────────────────
static HbRingEntry* s_ring          = nullptr;
static uint32_t     s_ring_head     = 0;   // next write position
static uint32_t     s_ring_count    = 0;   // total entries ever written
static bool         s_psram         = false;

static uint32_t hb_ring_retained_count() {
    return (s_ring_count < HB_RING_SIZE) ? s_ring_count : HB_RING_SIZE;
}

static uint32_t hb_ring_oldest_index() {
    return s_ring_count - hb_ring_retained_count();
}

// ── Initialisation ─────────────────────────────────────

bool hb_ring_init() {
    if (s_ring) return true;  // already initialised

    size_t total_bytes = HB_RING_SIZE * sizeof(HbRingEntry);

    // Try PSRAM first
    if (psramFound()) {
        s_ring = (HbRingEntry*)heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM);
        if (s_ring) {
            s_psram = true;
        }
    }

    // Fall back to DRAM
    if (!s_ring) {
        s_ring = (HbRingEntry*)malloc(total_bytes);
    }

    if (s_ring) {
        memset(s_ring, 0, total_bytes);
    }

    s_ring_head  = 0;
    s_ring_count = 0;
    return s_ring != nullptr;
}

// ── Push ───────────────────────────────────────────────

void hb_ring_push(const HbRingEntry& entry) {
    if (!s_ring) return;

    s_ring[s_ring_head] = entry;
    s_ring_head = (s_ring_head + 1) % HB_RING_SIZE;
    // Allow count to saturate at UINT32_MAX — used to compute
    // logical index mapping.
    if (s_ring_count < UINT32_MAX) {
        s_ring_count++;
    }
}

// ── Query helpers ──────────────────────────────────────

uint32_t hb_ring_count() {
    return s_ring_count;
}

bool hb_ring_get(uint32_t index, HbRingEntry* out) {
    if (!s_ring || !out) return false;

    const uint32_t oldest = hb_ring_oldest_index();
    if (index < oldest || index >= s_ring_count) return false;

    const uint32_t offset = index - oldest;
    const uint32_t phys = (s_ring_count <= HB_RING_SIZE)
        ? index
        : ((s_ring_head + offset) % HB_RING_SIZE);
    *out = s_ring[phys];
    return true;
}

void hb_ring_clear() {
    if (!s_ring) return;
    memset(s_ring, 0, HB_RING_SIZE * sizeof(HbRingEntry));
    s_ring_head  = 0;
    s_ring_count = 0;
}

// ── Query: emit last N entries as @hb-ring records ────

uint32_t hb_ring_query(uint32_t n) {
    uint32_t total = hb_ring_count();
    if (total == 0) {
        // Emit empty response
        emit_tag(tag::HB_RING);
        emit_sep();
        emit_kv_u(key::N, 0);
        emit_end();
        return 0;
    }

    uint32_t retained = hb_ring_retained_count();
    uint32_t requested = (n < retained) ? n : retained;
    uint32_t start_idx = total - requested;
    uint32_t emitted   = 0;

    // Emit header with total count
    emit_tag(tag::HB_RING);
    emit_sep();
    emit_kv_u(key::N, total);
    emit_end();
    emitted++;

    // Emit entries
    for (uint32_t idx = start_idx; idx < total; idx++) {
        HbRingEntry e;
        if (!hb_ring_get(idx, &e)) break;

        emit_tag(tag::HB_RING);
        emit_sep();
        emit_kv_u(key::I, idx);
        emit_sep();
        emit_kv_u(key::T, e.t_s);
        emit_sep();
        emit_kv_u(key::H, e.h);
        emit_sep();
        emit_kv_u("hm", e.hm);
        emit_sep();
        emit_kv_u("p", e.p);
        emit_sep();
        emit_kv_u(key::B, e.b);
        emit_sep();
        emit_kv("rssi", e.rssi / 4);
        emit_sep();
        emit_kv_u("wt", e.wt);
        emit_sep();
        emit_kv_u("diff", (e.flags & 0x01U) ? 1U : 0U);
        emit_end();
        emitted++;
    }

    return emitted;
}

}  // namespace telemetry
}  // namespace sigurdos

#endif // SIGURDOS_TELEMETRY
