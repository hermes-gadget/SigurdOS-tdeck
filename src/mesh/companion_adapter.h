#pragma once

#if defined(SIGURDOS_COMPANION_USB) && SIGURDOS_COMPANION_USB && \
    defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
#error "Companion USB-only builds must not enable the BLE transport"
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// Companion (phone app) bridge adapter — hosts the BLE/USB companion
// protocol against the mesh wrapper. Extracted from mesh_wrapper.cpp's
// textual #include of companion_adapter.inc (ARCH-001, #820). The adapter
// reaches wrapper internals only through mesh_wrapper_internal.h.

#include <stdint.h>
#include <stddef.h>

namespace sigurdos {
namespace mesh {

// One-time companion transport init: BLE PIN generation, serial transport
// begin, bridge begin, advertising per prefs. Call once from mesh init after
// prefs and identity are ready. No-op when no companion transport is
// compiled in (neither SIGURDOS_COMPANION_BLE nor SIGURDOS_COMPANION_USB).
void companionAdapterInit();

// Per-loop bridge servicing (frame pump + BLE validation telemetry).
void companionAdapterLoop();

// Persist an incoming mesh message to the message store and mirror it to a
// connected companion app. Returns false if the store rejected it.
// txt_type is a COMPANION_TXT_* value (0 = plain text); extra carries up to
// 8 trailing bytes (e.g. the 4-byte sender prefix of signed messages).
bool storeIncomingMessageForCompanion(const char* sender, const char* channel,
                                      const char* text, int rssi, float snr,
                                      uint32_t sender_timestamp = 0,
                                      uint8_t path_len = 0xFF,
                                      const uint8_t* sender_prefix = nullptr,
                                      uint8_t txt_type = 0,
                                      const uint8_t* extra = nullptr,
                                      uint8_t extra_len = 0,
                                      uint32_t* store_id_out = nullptr);

} // namespace mesh
} // namespace sigurdos
