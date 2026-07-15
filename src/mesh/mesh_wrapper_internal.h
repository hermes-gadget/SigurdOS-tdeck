#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// Internal seam between mesh_wrapper.cpp and companion_adapter.cpp
// (ARCH-001, #820). The companion adapter used to be textually #included
// into mesh_wrapper.cpp so it could reach the wrapper's file-scope statics;
// these accessors make that boundary explicit. This is not part of the
// UI-facing API — include mesh_wrapper.h for that.

#include <stdint.h>
#include <stddef.h>

namespace sigurdos {
namespace mesh {

class SigurdMeshV2;

// The live mesh instance. nullptr before init() and in radio-less builds.
SigurdMeshV2* meshInstance();

// The node's display name (NUL-terminated, max 31 chars + NUL).
const char* meshOwnName();

// Current time from the wrapper's auto-discovering RTC clock.
uint32_t meshRtcTime();
// Monotonically unique variant (never returns the same value twice).
uint32_t meshRtcTimeUnique();

// False only in RX-only remote-test builds.
bool meshRadioTxAllowed();

// Raw radio-driver counters for the companion stats frames. All fields are
// zero when the radio driver is not initialised.
struct MeshRadioDriverStats {
    float    last_rssi;
    float    last_snr;
    uint32_t packets_recv;
    uint32_t packets_sent;
    uint32_t packets_recv_errors;
};
void meshRadioDriverStats(MeshRadioDriverStats& out);

// Persist the current self identity (companion private-key import).
void meshSaveSelfIdentity();

// Persist a candidate private key before activating it in the running mesh.
// On failure the current identity, contacts, and login sessions are unchanged.
bool meshImportSelfIdentity(const uint8_t* private_key);

// Append a self-sent message to the persistent message store.
uint32_t meshStoreOutgoingMessage(const char* conversation, const char* text,
                                  uint32_t timestamp, bool is_channel,
                                  bool sent_flood, uint8_t attempt = 0,
                                  uint8_t txt_type = 0);

// Mirror a self-sent message into the UI's incoming-message queue so open
// chat screens show it. timestamp 0 means "now".
void meshQueuePushOutgoing(const char* conversation, const char* sender,
                           const char* text, uint32_t timestamp,
                           uint32_t store_id = 0);

// "DM: <name>" conversation key shared by the message store, the chat UI
// and the companion adapter. Truncates to fit; always NUL-terminates.
inline void formatDmConversation(char* out, size_t out_size, const char* name)
{
    if (!out || out_size == 0) return;

    static constexpr char prefix[] = "DM: ";
    size_t pos = 0;
    for (const char* p = prefix; *p && pos + 1 < out_size; ++p) {
        out[pos++] = *p;
    }

    const char* src = name ? name : "";
    while (*src && pos + 1 < out_size) {
        out[pos++] = *src++;
    }
    out[pos] = '\0';
}

} // namespace mesh
} // namespace sigurdos
