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

uint32_t sendMessageWithScopeKey(const char* dest_name, const char* text, const uint8_t* key16) {
    (void)key16;
    return sendMessage(dest_name, text);
}

bool sendChannelMessageWithScopeKey(const char* channel_name, const char* text, const uint8_t* key16) {
    (void)key16;
    return sendChannelMessage(channel_name, text);
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
bool getContactByName(const char* name, ContactInfo* out) {
    (void)name; (void)out; return false;
}
bool addContactManual(const char*, const char*, uint8_t) { return false; }
int getChannelCount() { return 0; }
int exportChannels(char names[][37], int max) { return 0; }
bool addChannel(const char* name, const char* psk) { return false; }
bool removeChannel(int idx) { (void)idx; return true; }

bool removeContact(const char* name) { (void)name; return false; }
bool resetPathTo(const char* name) { (void)name; return false; }

// ── Radio stats ──────────────────────────────────

int getNoiseFloor() { return mock_noise; }
int getLastRSSI()   { return mock_rssi; }
float getLastSNR()  { return mock_snr; }

bool sendAdvert() { return false; }
void saveState() {}

bool companionBleAvailable() { return false; }
bool companionBleSetEnabled(bool enabled) { (void)enabled; return false; }
bool companionBleEnabled() { return false; }
bool companionBleConnected() { return false; }
uint32_t companionBleLastSyncTime() { return 0; }
uint32_t companionBlePin() { return 123456; }

// ── Packet log ──────────────────────────────────

static PacketLogEntry mock_pkt_log[8];
static int mock_pkt_count = 0;

int getPacketLogCount() { return mock_pkt_count; }
uint32_t getPacketLogGeneration() { return (uint32_t)mock_pkt_count; }
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

// ACK tracking bridge
static constexpr int MOCK_MAX_ACKED = 32;
struct MockAckedMsg {
    char dest[32];
    uint32_t timestamp;
};

static MockAckedMsg mock_acked_msgs[MOCK_MAX_ACKED];
static int mock_acked_head = 0;
static int mock_acked_count = 0;
static int mock_ack_counter = 0;

void registerAckedMessage(const char* dest_name, uint32_t timestamp) {
    if (!dest_name) return;

    MockAckedMsg& a = mock_acked_msgs[mock_acked_head];
    strncpy(a.dest, dest_name, sizeof(a.dest) - 1);
    a.dest[sizeof(a.dest) - 1] = '\0';
    a.timestamp = timestamp;

    mock_acked_head = (mock_acked_head + 1) % MOCK_MAX_ACKED;
    if (mock_acked_count < MOCK_MAX_ACKED) mock_acked_count++;
    mock_ack_counter++;
}

bool isMessageAcked(const char* dest_name, uint32_t timestamp) {
    if (!dest_name) return false;

    int i = mock_acked_head;
    for (int c = 0; c < mock_acked_count; c++) {
        i--;
        if (i < 0) i = MOCK_MAX_ACKED - 1;
        if (mock_acked_msgs[i].timestamp == timestamp &&
            strcmp(mock_acked_msgs[i].dest, dest_name) == 0) {
            return true;
        }
    }
    return false;
}

int getAckCounter() {
    return mock_ack_counter;
}

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

// ── Regions stubs (RegionMap API) ────────────────────

static RegionEntry  mock_region_entries[MAX_REGION_ENTRIES];
static int          mock_region_count = 0;
static uint16_t     mock_next_id = 1;
static char         mock_active_region[31] = "";

void regionsInit(TransportKeyStore&) {}
bool regionsLoad() { return true; }
bool regionsSave() { return true; }
RegionMap* getRegionMap() { return nullptr; }

::RegionEntry* addRegion(const char* name, const char* parent_name) {
    (void)parent_name;
    if (!name || !name[0] || mock_region_count >= MAX_REGION_ENTRIES) return nullptr;
    // reject duplicates
    for (int i = 0; i < mock_region_count; i++) {
        if (strcmp(mock_region_entries[i].name, name) == 0) return nullptr;
    }
    RegionEntry* e = &mock_region_entries[mock_region_count++];
    memset(e, 0, sizeof(RegionEntry));
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    e->id = mock_next_id++;
    return e;
}

bool removeRegion(const char* name) {
    if (!name || !name[0]) return false;
    for (int i = 0; i < mock_region_count; i++) {
        if (strcmp(mock_region_entries[i].name, name) == 0) {
            memmove(&mock_region_entries[i], &mock_region_entries[i + 1],
                    (mock_region_count - i - 1) * sizeof(RegionEntry));
            mock_region_count--;
            return true;
        }
    }
    return false;
}

::RegionEntry* findRegion(const char* name) {
    if (!name || !name[0]) return nullptr;
    for (int i = 0; i < mock_region_count; i++) {
        if (strcmp(mock_region_entries[i].name, name) == 0)
            return &mock_region_entries[i];
    }
    return nullptr;
}

::RegionEntry* findRegionPrefix(const char* prefix) {
    if (!prefix || !prefix[0]) return nullptr;
    size_t plen = strlen(prefix);
    for (int i = 0; i < mock_region_count; i++) {
        if (strncmp(mock_region_entries[i].name, prefix, plen) == 0)
            return &mock_region_entries[i];
    }
    return nullptr;
}

int listRegionNames(char* dest, int max_len, uint8_t mask, bool invert) {
    (void)mask; (void)invert;
    if (!dest || max_len <= 0) return 0;
    int pos = 0;
    for (int i = 0; i < mock_region_count && pos < max_len - 1; i++) {
        int need = strlen(mock_region_entries[i].name) + 2; // name + ", " or null
        if (pos + need > max_len) break;
        if (pos > 0) dest[pos++] = ',';
        int n = 0;
        while (mock_region_entries[i].name[n] && pos < max_len - 1)
            dest[pos++] = mock_region_entries[i].name[n++];
    }
    dest[pos] = '\0';
    return pos;
}

int getRegionCount() { return mock_region_count; }

size_t exportRegions(char* dest, size_t max_len) {
    if (!dest || max_len == 0) return 0;
    dest[0] = '\0';
    return 0;
}

bool regionAllowsFlood(const char* name) {
    (void)name;
    return true; // mock: default allow
}

bool setRegionFloodAllowed(const char* name, bool allowed) {
    (void)name; (void)allowed;
    return true;
}

::RegionEntry* regionDeniesFlood(::mesh::Packet*) { return nullptr; }

const char* getHomeRegionName() { return nullptr; }
bool setHomeRegion(const char* name) { (void)name; return true; }
const char* getDefaultScopeName() { return nullptr; }
bool setDefaultScope(const char* name) { (void)name; return true; }

const char* getActiveRegion() {
    return mock_active_region[0] ? mock_active_region : "";
}

bool setActiveRegionName(const char* name) {
    if (name && name[0]) {
        strncpy(mock_active_region, name, sizeof(mock_active_region) - 1);
        mock_active_region[sizeof(mock_active_region) - 1] = '\0';
    } else {
        mock_active_region[0] = '\0';
    }
    return true;
}

// mesh_wrapper-level setter: mirrors setActiveRegionName for the cache so
// getActiveRegion() reflects the change (real build also propagates to g_mesh).
bool setActiveRegion(const char* name) { return setActiveRegionName(name); }

void setSendUnscopedOnce(bool v) { (void)v; }

void syncRegionsFromChannels() {}

int listRegions(RegionInfo* out, int max) {
    if (!out || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < mock_region_count && n < max; i++) {
        memset(&out[n], 0, sizeof(RegionInfo));
        strncpy(out[n].name, mock_region_entries[i].name, sizeof(out[n].name) - 1);
        out[n].name[sizeof(out[n].name) - 1] = '\0';
        out[n].id = mock_region_entries[i].id;
        out[n].parent_id = mock_region_entries[i].parent;
        out[n].flags = mock_region_entries[i].flags;
        out[n].is_home = false;
        out[n].is_default = false;
        n++;
    }
    return n;
}

} // namespace sigurdos::mesh
