// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include "mesh/mesh_wrapper.h"
#include "mesh/regions.h"
#include <cstring>

namespace sigurdos::mesh {

static MeshMessage     mock_msgs[8];
static int             mock_msg_count = 0;
static uint32_t        mock_drop_count = 0;
static char            mock_own_name[32] = "MockNode";
static int             mock_noise = -120;
static int             mock_rssi  = -80;
static float           mock_snr   = 5.0f;

// ── Lifecycle ────────────────────────────────────

bool init() { return true; }
void loop() {}

// ── Send ─────────────────────────────────────────

uint32_t sendMessage(const char* dest_name, const char* text) {
    (void)dest_name; (void)text; return 0;
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
uint32_t getQueueDropCount() { return mock_drop_count; }

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
int exportContactsFull(ContactInfo* out, int max) { (void)out; return 0; }
int getChannelCount() { return 0; }
int exportChannels(char names[][32], int max) { return 0; }
bool addChannel(const char* name, const char* psk) { return false; }

bool removeContact(const char* name) { (void)name; return false; }
bool resetPathTo(const char* name) { (void)name; return false; }

// ── Radio stats ──────────────────────────────────

int getNoiseFloor() { return mock_noise; }
int getLastRSSI()   { return mock_rssi; }
float getLastSNR()  { return mock_snr; }

bool sendAdvert() { return false; }
void saveState() {}

// ── Packet log ──────────────────────────────────

static PacketLogEntry mock_pkt_log[8];
static int mock_pkt_count = 0;

int getPacketLogCount() { return mock_pkt_count; }
bool getPacketLogEntry(int index, PacketLogEntry* out) {
    if (index < 0 || index >= mock_pkt_count || !out) return false;
    *out = mock_pkt_log[index];
    return true;
}

void mock_push_packet(const char* source, int rssi, float snr, const char* type) {
    if (mock_pkt_count >= 8) return;
    PacketLogEntry& e = mock_pkt_log[mock_pkt_count++];
    strncpy(e.source, source, sizeof(e.source) - 1);
    e.source[sizeof(e.source) - 1] = '\0';
    e.rssi = rssi;
    e.snr = snr;
    strncpy(e.type, type, sizeof(e.type) - 1);
    e.type[sizeof(e.type) - 1] = '\0';
    e.timestamp = 0;
}

void mock_clear_packets() { mock_pkt_count = 0; }

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
void mock_set_drop_count(uint32_t v) { mock_drop_count = v; }

// ── QR code support stubs ──────────────────────
bool getContactPubkeyHex(const char* name, char* hex_out, size_t hex_sz) {
    (void)name; (void)hex_out; (void)hex_sz; return false;
}
bool getChannelSecretHex(int channel_idx, char* hex_out, size_t hex_sz) {
    (void)channel_idx; (void)hex_out; (void)hex_sz; return false;
}

// ── Identity backup stubs ──────────────────────
bool exportIdentity(char* hex_out, size_t hex_sz) {
    if (!hex_out || hex_sz < 2) return false;
    hex_out[0] = '0'; hex_out[1] = '\0';
    return false; // mock: no real identity
}

bool importIdentity(const char* hex_privkey) {
    (void)hex_privkey;
    return false; // mock: cannot import
}

// ── Regions stubs ────────────────────────────────

static SigurdRegion    mock_regions[SIGURD_MAX_REGIONS];
static int             mock_region_count = 0;
static char            mock_active_region[31] = "";

int listRegions(SigurdRegion* out, int max) {
    if (!out || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < mock_region_count && n < max; i++) {
        memcpy(&out[n], &mock_regions[i], sizeof(SigurdRegion));
        n++;
    }
    return n;
}

bool addRegion(const char* name, const char* key_b64_or_null) {
    (void)key_b64_or_null;
    if (!name || !name[0] || mock_region_count >= SIGURD_MAX_REGIONS) return false;
    SigurdRegion r;
    memset(&r, 0, sizeof(r));
    strncpy(r.name, name, sizeof(r.name) - 1);
    memcpy(&mock_regions[mock_region_count++], &r, sizeof(SigurdRegion));
    return true;
}

bool removeRegion(const char* name) {
    if (!name || !name[0]) return false;
    for (int i = 0; i < mock_region_count; i++) {
        if (strcmp(mock_regions[i].name, name) == 0) {
            for (int j = i; j < mock_region_count - 1; j++)
                memcpy(&mock_regions[j], &mock_regions[j + 1], sizeof(SigurdRegion));
            mock_region_count--;
            return true;
        }
    }
    return false;
}

bool setActiveRegion(const char* name) {
    if (name && name[0]) {
        strncpy(mock_active_region, name, sizeof(mock_active_region) - 1);
        mock_active_region[sizeof(mock_active_region) - 1] = '\0';
    } else {
        mock_active_region[0] = '\0';
    }
    return true;
}

const char* getActiveRegion() {
    return mock_active_region[0] ? mock_active_region : "";
}

void setSendUnscopedOnce(bool v) { (void)v; }

void syncRegionsFromChannels() {}

// regions.h stubs
int loadRegions(SigurdRegion* out, int max) {
    return listRegions(out, max);
}

bool saveRegions(const SigurdRegion* list, int count) {
    (void)list; (void)count; return true;
}

bool deriveRegionKey(const char* name, uint8_t* out_key16) {
    (void)name; (void)out_key16; return false;
}

} // namespace sigurdos::mesh
