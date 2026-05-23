// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include "mesh/mesh_wrapper.h"
#include <cstring>

namespace slopos::mesh {

static MeshMessage     mock_msgs[8];
static int             mock_msg_count = 0;
static char            mock_own_name[32] = "MockNode";
static int             mock_noise = -120;
static int             mock_rssi  = -80;
static float           mock_snr   = 5.0f;

// ── Lifecycle ────────────────────────────────────

bool init() { return true; }
void loop() {}

// ── Send ─────────────────────────────────────────

bool sendMessage(const char* dest_name, const char* text) {
    (void)dest_name; (void)text; return false;
}

bool sendChannelMessage(const char* channel_name, const char* text) {
    (void)channel_name; (void)text; return false;
}

int pollMessages(MeshMessage* out, int max) {
    int drained = 0;
    while (drained < max && mock_msg_count > 0) {
        out[drained] = mock_msgs[--mock_msg_count];
        drained++;
    }
    return drained;
}

int pendingMessageCount() { return mock_msg_count; }

// ── Identity ─────────────────────────────────────

void setOwnName(const char* name) {
    if (name) {
        strncpy(mock_own_name, name, sizeof(mock_own_name) - 1);
        mock_own_name[sizeof(mock_own_name) - 1] = '\0';
    }
}

const char* getOwnName() { return mock_own_name; }

// ── Contacts / channels ──────────────────────────

int getContactCount() { return 0; }
int exportContacts(char names[][32], int max) { return 0; }
int getChannelCount() { return 0; }
int exportChannels(char names[][32], int max) { return 0; }
bool addChannel(const char* name, const char* psk) { return false; }
bool addHashtagChannel(const char* name) { (void)name; return false; }

// ── Radio stats ──────────────────────────────────

int getNoiseFloor() { return mock_noise; }
int getLastRSSI()   { return mock_rssi; }
float getLastSNR()  { return mock_snr; }

bool sendAdvert() { return false; }
void saveState() {}

// ── Test helpers ─────────────────────────────────

void mock_push_message(const char* sender, const char* text) {
    if (mock_msg_count >= 8) return;
    MeshMessage& m = mock_msgs[mock_msg_count++];
    strncpy(m.sender, sender, sizeof(m.sender) - 1);
    m.channel[0] = '\0';
    strncpy(m.text, text, sizeof(m.text) - 1);
    m.timestamp = 0;
    m.is_self = false;
}

void mock_set_noise(int v)  { mock_noise = v; }
void mock_set_rssi(int v)   { mock_rssi = v; }
void mock_set_snr(float v)  { mock_snr = v; }

} // namespace slopos::mesh
