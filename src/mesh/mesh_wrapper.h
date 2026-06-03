// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// MeshCore protocol integration for SigurdOS.
// Uses SigurdMesh, a minimal mesh::Mesh subclass.

#pragma once
#include <cstdint>
#include <cstddef>

// Node type identifiers from MeshCore adverts — kept local so UI code can filter.
#define ADV_TYPE_NONE      0
#define ADV_TYPE_CHAT      1
#define ADV_TYPE_REPEATER  2
#define ADV_TYPE_ROOM      3
#define ADV_TYPE_SENSOR    4

// ACL permission levels for contacts (stored in ContactInfo.perm and
// persisted in the contacts file). Packed into ContactInfo::flags bits 1-2.
#define PERM_ACL_GUEST      0
#define PERM_ACL_READ_ONLY  1
#define PERM_ACL_READ_WRITE 2
#define PERM_ACL_ADMIN      3

namespace sigurdos {
namespace mesh {

// Forward declarations from mesh_wrapper.cpp
void mesh_v2_queue_push(const char* sender, const char* channel,
                         const char* text, int rssi, float snr);

struct MeshMessage {
    char sender[32];
    char channel[32];
    char text[256];
    uint32_t timestamp;
    bool is_self;
};

struct ContactInfo {
    char name[32];
    uint8_t type;  // ADV_TYPE_* (ADV_TYPE_CHAT=companion, ADV_TYPE_REPEATER, ADV_TYPE_ROOM, etc.)
    uint8_t perm;  // PERM_ACL_* (0=guest, 1=read-only, 2=read-write, 3=admin), default 0
    bool has_location;
    float latitude;
    float longitude;
    int  rssi;
    float snr;
    uint32_t last_seen;
};

struct PacketLogEntry {
    uint32_t timestamp;
    char     source[32];
    int      rssi;
    float    snr;
    char     type[16];
};

bool init(bool spiffs_ok = true);
void loop();

// Returns 0 on failure, or the epoch-second timestamp the mesh layer used for ACK tracking.
// The UI must store this returned timestamp so isMessageAcked() can match against it later.
uint32_t sendMessage(const char* dest_name, const char* text);
bool sendChannelMessage(const char* channel_name, const char* text);

int  pollMessages(MeshMessage* out, int max);
int  pendingMessageCount();
uint32_t getQueueDropCount();
int  getUnreadMessageCount();
void resetUnreadMessageCount();

int  getContactCount();
int  exportContacts(char names[][32], int max);
int  exportContactsFull(ContactInfo* out, int max);
bool isContactFavourite(const char* name);
void setContactFavourite(const char* name, bool favourite);

int  getChannelCount();
int  exportChannels(char names[][32], int max);
bool addChannel(const char* name, const char* psk_base64);
bool addHashtagChannel(const char* name);
bool joinPublicChannel();

void setOwnName(const char* name);
const char* getOwnName();

int   getNoiseFloor();
int   getLastRSSI();
float getLastSNR();
unsigned long getTotalTxAirtimeMs();
unsigned long getTotalRxAirtimeMs();
uint32_t getNumSentFlood();
uint32_t getNumSentDirect();
uint32_t getNumRecvFlood();
uint32_t getNumRecvDirect();
void resetPacketStats();

bool sendAdvert();
uint32_t getLastAdvertTime();
bool     getLastAdvertSuccess();
bool     getLastAdvertUsedGps();
void saveState();
void saveChannels();
void loadChannels();
void shutdown();
void factoryReset();

// ── Contact persistence ─────────────────────────
void saveContacts();
void loadContacts();

// RTC time for UI comparisons
uint32_t getCurrentTime();
bool setSystemTime(uint32_t epoch_seconds);

void getCurrentLocalDateTime(int* year, int* month, int* day, int* hour, int* minute);
uint32_t makeEpoch(int year, int month, int day, int hour, int minute);

// Packet log
int  getPacketLogCount();
bool getPacketLogEntry(int index, PacketLogEntry* out);
void pushPacketLog(const char* source, int rssi, float snr, const char* type);

// Inject a simulated incoming message into the message queue (for remote test mode).
// No radio transmission occurs. The message appears as if received from another node.
void injectMessage(const char* sender, const char* channel, const char* text);

// Trace route
bool sendTrace(int contact_idx, uint32_t* out_tag);
int  findContactIndex(const char* name);
bool hasTraceResult();
uint8_t getTracePathLen();
void   getTracePath(uint8_t* snrs_out, uint8_t* hashes_out);
void   clearTraceResult();
bool   contactHasPath(int contact_idx);

// ── Ping Nearby ─────────────────────────────────
struct PingResult {
    char name[32];
    int rssi;
};
bool     sendPingNearby();
bool     pingIsActive();
bool     pingOnCooldown();
uint32_t pingCooldownRemaining();
uint32_t activePingRemaining();
int      getPingResultCount();
const PingResult* getPingResult(int i);

// ── Signal history for sparkline ─────────────
int  getSignalHistoryCount();
int  getSignalHistoryRSSI(int idx);
float getSignalHistorySNR(int idx);

// ── Live radio config (no NVS write) ──────────
bool applyRadioParams(float freq, float bw, int sf, int cr, int tx_power, bool rx_gain);
bool revertRadioParams();

// ── REQ/RESPONSE framework (Phase 4.1) ────────
bool sendRequest(const char* dest_name, uint8_t req_type);
bool sendRequestWithData(const char* dest_name, const uint8_t* data, uint8_t len);
int  getResponseCount();
bool getResponse(int idx, uint32_t* out_tag, uint8_t* out_data, uint8_t* out_len, char* out_contact_name);
void clearResponses();

// ── Duty cycle ────────────────────────────────
unsigned long getRemainingTxBudget();
void setDutyCycle(uint8_t percent);

// ── Contact management extensions ────────────
bool removeContact(const char* name);
bool resetPathTo(const char* name);
bool setContactPerm(const char* name, uint8_t perm);
int  getContactPerm(const char* name);

// ── Channel management extensions ────────────
bool removeChannel(int idx);

// ── ACK tracking ──────────────────────────────
void registerAckedMessage(const char* dest_name, uint32_t timestamp);
bool isMessageAcked(const char* dest_name, uint32_t timestamp);
int  getAckCounter();   // incremented each time registerAckedMessage is called

// ── Room message fetch (Phase 4.6) ────────────────
bool sendRoomMsgFetchRequest(const char* contact_name, const char* channel_name);
int  getRoomMsgFetchCount();
bool getRoomMsgFetchEntry(int index, char* sender_out, int sender_sz,
                          char* text_out, int text_sz,
                          char* channel_out, int channel_sz,
                          uint32_t* timestamp_out);
void clearRoomMsgFetch();

// ── Status request (Phase 4.2) ────────────────
#define NODE_STATUS_RESPONSE_SIZE  56  // size of RepeaterStats binary blob

struct NodeStatus {
    uint16_t batt_milli_volts;       // battery voltage in mV
    uint16_t curr_tx_queue_len;      // current TX queue length
    int16_t  noise_floor;            // noise floor (dBm)
    int16_t  last_rssi;              // last received RSSI (dBm)
    uint32_t n_packets_recv;         // total packets received
    uint32_t n_packets_sent;         // total packets sent
    uint32_t total_air_time_secs;    // total TX air time (seconds)
    uint32_t total_up_time_secs;     // node uptime (seconds)
    uint32_t n_sent_flood;           // flood messages sent
    uint32_t n_sent_direct;          // direct messages sent
    uint32_t n_recv_flood;           // flood messages received
    uint32_t n_recv_direct;          // direct messages received
    uint16_t err_events;             // error event count
    int16_t  last_snr;               // last SNR (value/4 = dB)
    uint16_t n_direct_dups;          // duplicate direct packets
    uint16_t n_flood_dups;           // duplicate flood packets
    uint32_t total_rx_air_time_secs; // total RX air time (seconds)
    uint32_t n_recv_errors;          // receive errors
};

bool requestStatus(const char* dest_name);
bool hasStatusResponse();
bool getStatusResult(NodeStatus* out);

// ── Telemetry queries (Phase 4.3) ─────────────
#define MAX_TELEMETRY_ITEMS 12

struct TelemetryItem {
    uint8_t channel;
    uint8_t type;
    float   value_float;     // decoded float value (temperature C, voltage V, etc.)
    char    value_str[24];   // formatted string for display
};

struct TelemetryResult {
    int          n_items;
    TelemetryItem items[MAX_TELEMETRY_ITEMS];
};

bool requestTelemetry(const char* dest_name);
bool hasTelemetryResponse();
bool getTelemetryResult(TelemetryResult* out);

// ── Path discovery (Phase 4.4) ────────────────
// Sends a flood request to discover the route to a contact.
// Returns a discovery tag (>0) on success, or 0 on failure.
uint32_t discoverPath(const char* dest_name);
// Check if a path has been learned for a contact (path_len > 0 || path_len == 0xFF unknown)
bool hasPathTo(const char* dest_name);
uint8_t getContactPathLen(const char* dest_name);

// ── Advert path (inbound) ─────────────────────
// Returns the number of hops the advert from this contact traversed
// to reach us. Returns 0 if no advert path is known for this contact.
uint8_t getAdvertPathLen(const char* name);

// ── Login status values ───────────────────────
#define LOGIN_STATUS_NONE    0   // not logged in
#define LOGIN_STATUS_PENDING 1   // login request sent, awaiting response
#define LOGIN_STATUS_OK      2   // logged in successfully
#define LOGIN_STATUS_FAILED  3   // login was rejected

// ── Repeater/room login (Phase 4.5) ──────────────
bool sendLogin(const char* name, const char* password);
void sendLogout(const char* name);
bool sendCommand(const char* name, const char* text);
bool isLoggedIn(const char* name);
uint8_t getLoginPermission(const char* name);
uint8_t getLoginStatus(const char* name);
void forceLoginState(const char* name, uint8_t status, uint8_t permission);


// ── Command response ring buffer (for terminal UI) ──
#define MAX_CMD_RESPONSES 16
void pushCmdResponse(const char* name, const char* text);
bool pollCmdResponse(char* name_out, int name_sz, char* text_out, int text_sz);
void clearCmdResponses();

#if defined(SIGURDOS_REMOTE_TEST)
// Test helper: inject a fake repeater contact into the mesh contact list.
// The contact will have the given name, type ADV_TYPE_REPEATER, and test SNR/RSSI.
// Used by the test controller to verify the repeater detail UI without real radio traffic.
bool addTestRepeater(const char* name);
bool addTestRoomServer(const char* name);
#endif

// ── Anonymous requests (Phase 4.7) ────────────────
// Send a text message to a node identified by its 64-hex-char public key.
// The node does NOT need to be in your contact list.
bool sendAnonMessage(const char* pubkey_hex, const char* text);

// ── Group data datagrams (Phase 4.8) ─────────────
// Standard data type constants
// (Defined as static constexpr in SigurdMeshV2; reusing here via enum)
enum GroupDataType : uint16_t {
    GDT_NONE        = 0x0000,
    GDT_TEMPERATURE = 0x0001,
    GDT_HUMIDITY    = 0x0002,
    GDT_PRESSURE    = 0x0003,
    GDT_LOCATION    = 0x0004,
    GDT_BATTERY     = 0x0005,
    GDT_STATUS      = 0x0006,
    GDT_CUSTOM      = 0x00FF
};

// Hex-to-bytes helper (used by terminal commands)
int hexToBytes(const char* hex, uint8_t* out, int out_max);

// Send typed data to a group channel by index
bool sendGroupDataToChannel(int channel_idx, uint16_t data_type,
                            const uint8_t* data, int data_len);

// Polling API for received group datagrams
int  getGroupDataRecvCount();
bool getGroupDataRecvEntry(int index, uint16_t* data_type_out,
                           uint8_t* data_out, int data_out_max, int* data_len_out,
                           char* channel_out, int channel_sz,
                           uint32_t* timestamp_out);
void clearGroupDataRecv();

// ── Message signing ──
// Sign arbitrary data with this node's private key.
// Returns number of bytes written to sig_out (SIGNATURE_SIZE=64 on success, 0 on failure).
int signMessage(const char* data, uint8_t* sig_out);

// ── Identity backup (export/import) ──
// Export the node's Ed25519 private key as a hex string (128 hex chars = 64 bytes).
// Returns true on success. hex_out must be at least 129 characters.
bool exportIdentity(char* hex_out, size_t hex_sz);

// Import a 128-hex-char Ed25519 private key (64 bytes), re-keying the node.
// The public key is recomputed from the private key. The identity is saved to
// SPIFFS and the node should be rebooted to reload contacts with new identity.
// Returns true on success if the key is valid.
bool importIdentity(const char* hex_privkey);

// ── URI import ──
// Import a contact from a meshcore:// URI (query-param or raw hex blob format).
// Returns true if a contact was successfully imported.
bool importContactByUri(const char* uri);

// Add a channel from a meshcore://channel/add?... URI.
// Returns true if the channel was successfully added.
bool addChannelByUri(const char* uri);

// ── QR code support ─────────────────────────────
// Look up a contact by name and write its 64-hex-char public key to hex_out.
// hex_sz must be at least PUB_KEY_SIZE * 2 + 1 (65). Returns false if
// g_mesh is null, the contact is not found, or the buffer is too small.
bool getContactPubkeyHex(const char* name, char* hex_out, size_t hex_sz);

// Write the 64-hex-char channel secret for the given channel index to hex_out.
// hex_sz must be at least PUB_KEY_SIZE * 2 + 1 (65). Returns false if
// g_mesh is null, the channel index is out of range, or the buffer is too small.
bool getChannelSecretHex(int channel_idx, char* hex_out, size_t hex_sz);

// ── Regions (flood scope) ──────────────────────────
struct SigurdRegion;
// List saved regions. Returns count (≤ max).
int  listRegions(SigurdRegion* out, int max);

// Auto-create #regions from #channels (key = SHA256(name)).
// Skips channels without a # prefix and regions that already exist.
void syncRegionsFromChannels();

// Add a region. For #public names, key is auto-derived.
// For $private names, key_b64_or_null must be a 16-byte base64 key.
bool addRegion(const char* name, const char* key_b64_or_null);

// Remove a saved region by name.
bool removeRegion(const char* name);

// Set the active flood-scope region (empty = wildcard/unscoped).
bool setActiveRegion(const char* name);

// Get the current active region name. Returns "" if wildcard.
const char* getActiveRegion();

// Temporarily send the next message unscoped (resets after one use).
void setSendUnscopedOnce(bool v);

} // namespace mesh
} // namespace sigurdos
