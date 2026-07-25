// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// MeshCore protocol integration for SigurdOS.
// Uses SigurdMesh, a minimal mesh::Mesh subclass.

#pragma once
#include <cstdint>
#include <cstddef>
#include <helpers/RegionMap.h>  // for RegionEntry (must be before namespace)
#include "status_response.h"
#include "time_state.h"

// Node type identifiers pinned to MeshCore's AdvertDataHelpers protocol.
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
struct NodePrefs;
namespace mesh {

// Forward declarations from mesh_wrapper.cpp
// sender_timestamp / path_len carry the originating packet's stamp and mesh
// path-length byte through to the companion bridge so the phone app shows the
// correct time and hop count. Defaults (0 / 0xFF) suit callers without a packet:
// the timestamp falls back to the local clock and 0xFF means "direct/unknown".
// sender_prefix points to the sender's complete public key when available;
// null falls back to display-only routing. txt_type, extra, and extra_len
// preserve the MeshCore companion text type and signed-message extra.
void mesh_v2_queue_push(const char* sender, const char* channel,
                         const char* text, int rssi, float snr,
                         uint32_t sender_timestamp = 0, uint8_t path_len = 0xFF,
                         const uint8_t* sender_prefix = nullptr,
                         uint8_t txt_type = 0,
                         const uint8_t* extra = nullptr,
                         uint8_t extra_len = 0);

// Forwards a delivery ACK to the companion bridge so the phone app marks a
// message it sent (via the device) as confirmed. ack is the 4-byte ACK hash the
// app received in RESP_CODE_SENT; trip_time_ms is the round-trip time.
void mesh_v2_notify_send_confirmed(uint32_t ack, uint32_t trip_time_ms);
void mesh_v2_group_data_push(uint8_t channel_index,
                              uint8_t path_len,
                              int8_t snr_quarters,
                              uint16_t data_type,
                              const uint8_t* data,
                              size_t data_len);

// Live mesh-event fan-out to the companion bridge (no-ops when the bridge is
// absent / no phone connected). Primitive args keep this header MeshCore-free.
// contact_info is an opaque `const ::ContactInfo*` (kept void* so this header
// stays free of MeshCore types); the adapter casts it back.
void mesh_v2_companion_advert_push(const void* contact_info, bool is_new);
void mesh_v2_companion_path_push(const uint8_t* pub_key);
void mesh_v2_companion_path_discovery_push(const uint8_t* pub_key_prefix,
                                            const uint8_t* in_path,
                                            uint8_t in_path_len,
                                            const uint8_t* out_path,
                                            uint8_t out_path_len);
void mesh_v2_companion_contact_deleted_push(const uint8_t* pub_key);
void mesh_v2_companion_contacts_full_push();
void mesh_v2_companion_login_push(const uint8_t* pub_key, bool success,
                                  uint8_t permission, uint32_t tag,
                                  uint8_t acl_permissions, uint8_t firmware_level,
                                  bool legacy_success = false);
void mesh_v2_companion_status_push(const uint8_t* pub_key, const uint8_t* blob, size_t len);
void mesh_v2_companion_telemetry_push(const uint8_t* pub_key, const uint8_t* blob, size_t len);
void mesh_v2_companion_binary_push(uint32_t tag, const uint8_t* blob, size_t len);
void mesh_v2_companion_raw_data_push(int8_t snr_quarters, int8_t rssi,
                                     const uint8_t* payload, size_t payload_len);
void mesh_v2_companion_control_data_push(int8_t snr_quarters, int8_t rssi,
                                         uint8_t path_len,
                                         const uint8_t* payload, size_t payload_len);
void mesh_v2_companion_trace_push(uint32_t tag, uint32_t auth, uint8_t flags,
                                  const uint8_t* path_hashes, const uint8_t* path_snrs,
                                  uint8_t path_len, int8_t final_snr_quarters);

struct MeshMessage {
    char sender[32];
    char channel[4 + 65];
    char text[256];
    uint32_t timestamp;
    uint32_t store_id;
    bool is_self;
};

struct ContactInfo {
    // Canonical identity: lowercase hex of the complete 32-byte public key.
    char id[65];
    char name[32];
    uint8_t type;  // ADV_TYPE_* (ADV_TYPE_CHAT=companion, ADV_TYPE_REPEATER, ADV_TYPE_ROOM, etc.)
    uint8_t perm;  // PERM_ACL_* (0=guest, 1=read-only, 2=read-write, 3=admin), default 0
    bool has_location;
    float latitude;
    float longitude;
    int  rssi;
    float snr;
    uint32_t last_seen;
    bool favourite;
    bool name_ambiguous;
    bool has_path;
    uint8_t path_len;
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
// Contact references are stable IDs. For compatibility, a unique display name
// is accepted; duplicate display names always fail closed.
uint32_t sendMessage(const char* contact_id, const char* text);
bool sendChannelMessage(const char* channel_name, const char* text);
// Scoped variants temporarily stamp this send with key16. A null key sends unscoped.
uint32_t sendMessageWithScopeKey(const char* contact_id, const char* text, const uint8_t* key16);
bool sendChannelMessageWithScopeKey(const char* channel_name, const char* text, const uint8_t* key16);

int  pollMessages(MeshMessage* out, int max);
int  pendingMessageCount();
uint32_t getQueueDropCount();
int  getUnreadMessageCount();
void resetUnreadMessageCount();

int  getContactCount();
int  exportContactsFull(ContactInfo* out, int max);
bool getContactByName(const char* name, ContactInfo* out);
bool getContactById(const char* contact_id, ContactInfo* out);
bool addContactManual(const char* name, const char* pubkey_hex, uint8_t type = ADV_TYPE_CHAT);
bool isContactFavourite(const char* contact_id);
bool setContactFavourite(const char* contact_id, bool favourite);

int  getChannelCount();
int  exportChannels(char names[][37], int max);
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

bool sendAdvert(bool apply_default_scope = false);
uint32_t getLastAdvertTime();
bool     getLastAdvertSuccess();
bool     getLastAdvertUsedGps();
// Coordinated checkpoint for preferences, identity, contacts, channels, and
// any dirty region map state.
bool saveState();
bool saveChannels();
void loadChannels();
// wake_secs=0 uses the critical-battery default (15 min timer).
// Non-zero requests a timed wake for tests/recovery.
void shutdown(uint32_t wake_secs = 0);
bool factoryReset();

// Companion BLE bridge
bool companionBleAvailable();
bool companionBleSetEnabled(bool enabled);
bool companionBleOpenPairingWindow();
bool companionBleEnabled();
bool companionBleConnected();
uint32_t companionBleLastSyncTime();
uint32_t companionBlePin();

// ── Contact persistence ─────────────────────────
bool saveContacts();
void loadContacts();
void reloadContactsAfterIdentityChange();  // after private key import

// Deferred contact persistence. Mesh-driven contact mutations (advert
// discovery, path updates) have no UI event site, so they mark the store
// dirty and the main loop flushes it after a debounce — advert bursts batch
// into a single SPIFFS rewrite instead of one per packet.
static constexpr uint32_t CONTACT_SAVE_DEBOUNCE_MS = 30000;
inline bool contacts_save_is_due(bool dirty, uint32_t dirty_since,
                                 uint32_t now) {
    return dirty && static_cast<uint32_t>(now - dirty_since) >=
                        CONTACT_SAVE_DEBOUNCE_MS;
}
void markContactsDirty();
void saveContactsIfDue(uint32_t now);

// RTC time for UI comparisons
uint32_t getCurrentTime();
bool setSystemTime(uint32_t epoch_seconds,
                   TimeSource source = TimeSource::Manual);
TimeSyncStatus getTimeSyncStatus();

void getCurrentLocalDateTime(int* year, int* month, int* day, int* hour, int* minute);
uint32_t makeEpoch(int year, int month, int day, int hour, int minute);

// Packet log
int  getPacketLogCount();
uint32_t getPacketLogGeneration();
bool getPacketLogEntry(int index, PacketLogEntry* out);
void pushPacketLog(const char* source, int rssi, float snr, const char* type);

// Inject a simulated incoming message into the message queue (for remote test mode).
// No radio transmission occurs. The message appears as if received from another node.
void injectMessage(const char* sender, const char* channel, const char* text);

// Trace route
bool sendTrace(const char* contact_id, uint32_t* out_tag);
int  findContactIndex(const char* contact_id);
bool hasTraceResult();
uint32_t getTraceResultTag();
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
// Apply the radio transaction first, then commit the complete preference
// snapshot. An NVS failure restores the previous hardware configuration.
bool applyAndPersistRadioPrefs(const ::sigurdos::NodePrefs& prefs);
bool revertRadioParams();
int16_t getLastRadioConfigError();

// ── REQ/RESPONSE framework (Phase 4.1) ────────
bool sendRequest(const char* contact_id, uint8_t req_type);
bool sendRequestWithData(const char* contact_id, const uint8_t* data, uint8_t len);
int  getResponseCount();
// Required lengths are reported even when a requested destination is too
// small. No payload/name bytes are copied unless every requested buffer fits.
bool getResponse(int idx, uint32_t* out_tag,
                 uint8_t* out_data, size_t out_data_cap, uint8_t* out_len,
                 char* out_contact_name, size_t out_contact_name_cap,
                 size_t* out_contact_name_len);
void clearResponses();

// ── Duty cycle ────────────────────────────────
unsigned long getRemainingTxBudget();
void setDutyCycle(uint8_t percent);

// ── Contact management extensions ────────────
bool removeContact(const char* contact_id);
bool resetPathTo(const char* contact_id);
bool setContactPerm(const char* contact_id, uint8_t perm);
int  getContactPerm(const char* contact_id);

// ── Channel management extensions ────────────
bool removeChannel(int idx);

// ── ACK tracking ──────────────────────────────
void registerAckedMessage(const char* dest_name, uint32_t timestamp);
void registerConfirmationLost(const char* dest_name, uint32_t timestamp);
bool isMessageAcked(const char* dest_name, uint32_t timestamp);
bool isMessageConfirmationLost(const char* dest_name, uint32_t timestamp);
int  getAckCounter();   // incremented each time registerAckedMessage is called
int  getDeliveryCounter(); // incremented for ACK and confirmation-lost changes
uint32_t getPendingAckDropCount();
uint32_t getPendingAckExpiredCount();

// ── Room message fetch (Phase 4.6) ────────────────
bool sendRoomMsgFetchRequest(const char* contact_id, const char* channel_name);
int  getRoomMsgFetchCount();
bool getRoomMsgFetchEntry(int index, char* sender_out, int sender_sz,
                          char* text_out, int text_sz,
                          char* channel_out, int channel_sz,
                          uint32_t* timestamp_out);
void clearRoomMsgFetch();

// Send a text message to a room server contact as a peer TXT_MSG.
// Returns the timestamp used for ACK tracking, or 0 on failure.
uint32_t sendRoomMessage(const char* contact_id, const char* channel_name, const char* text);

// Count room server contacts that are currently logged in.
int getLoggedInRoomServerCount();

// Get the stable ID of a logged-in room server contact by index (0..count-1).
// Returns the full public-key ID or empty string if not found.
const char* getLoggedInRoomServerName(int index);

// ── Status request (Phase 4.2) ────────────────
bool requestStatus(const char* contact_id);
bool hasStatusResponse();
bool statusRequestPending();
bool statusRequestTimedOut();
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

bool requestTelemetry(const char* contact_id);
bool hasTelemetryResponse();
bool telemetryRequestPending();
bool telemetryRequestTimedOut();
bool getTelemetryResult(TelemetryResult* out);

// ── Path discovery (Phase 4.4) ────────────────
// Sends a flood request to discover the route to a contact.
// Returns a discovery tag (>0) on success, or 0 on failure.
uint32_t discoverPath(const char* contact_id);
bool pathDiscoveryPending(const char* contact_id);
bool pathDiscoveryTimedOut(const char* contact_id);
// Check if a path has been learned for a contact (path_len > 0 || path_len == 0xFF unknown)
bool hasPathTo(const char* contact_id);
uint8_t getContactPathLen(const char* contact_id);

// ── Advert path (inbound) ─────────────────────
// Returns the number of hops the advert from this contact traversed
// to reach us. Returns 0 if no advert path is known for this contact.
uint8_t getAdvertPathLen(const char* name);

// ── Login status values ───────────────────────
#define LOGIN_STATUS_NONE    0   // not logged in
#define LOGIN_STATUS_PENDING 1   // login request sent, awaiting response
#define LOGIN_STATUS_OK      2   // logged in successfully
#define LOGIN_STATUS_FAILED  3   // login was rejected
#define LOGIN_STATUS_TIMEOUT 4   // no login response before the deadline
#define LOGIN_STATUS_DROPPED 5   // negotiated keep-alive connection expired

// ── Repeater/room login (Phase 4.5) ──────────────
bool sendLogin(const char* contact_id, const char* password);
void sendLogout(const char* contact_id);
bool sendCommand(const char* contact_id, const char* text);
bool isLoggedIn(const char* contact_id);
uint8_t getLoginPermission(const char* contact_id);
uint8_t getLoginStatus(const char* contact_id);
void forceLoginState(const char* contact_id, uint8_t status, uint8_t permission);


// ── Command response ring buffer (for terminal UI) ──
#define MAX_CMD_RESPONSES 16
void pushCmdResponse(const char* name, const char* text, uint32_t timestamp);
bool pollCmdResponse(char* name_out, int name_sz, char* text_out, int text_sz,
                     uint32_t* timestamp_out);
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
bool getContactPubkeyHex(const char* contact_id, char* hex_out, size_t hex_sz);

// Write the 64-hex-char channel secret for the given channel index to hex_out.
// hex_sz must be at least PUB_KEY_SIZE * 2 + 1 (65). Returns false if
// g_mesh is null, the channel index is out of range, or the buffer is too small.
bool getChannelSecretHex(int channel_idx, char* hex_out, size_t hex_sz);

// ── Regions (flood scope) ──────────────────────────
struct RegionInfo;
// List saved regions. Returns count (≤ max).
int  listRegions(RegionInfo* out, int max);

// Auto-create #regions from #channels.
// Skips channels without a # prefix and regions that already exist.
void syncRegionsFromChannels();

// Add a region. For #public names, transport keys are auto-derived.
// parent_name is optional for hierarchy placement.
/// Returns the RegionEntry or nullptr on failure.
RegionEntry* addRegion(const char* name, const char* parent_name);

// Remove a saved region by name.
bool removeRegion(const char* name);

// Set the active flood-scope region (empty = wildcard/unscoped).
bool setActiveRegion(const char* name);

// Validate/create a companion scope, install a required private key, then
// commit the active name and activate the scope. Empty clears the scope/key.
bool setActiveRegionWithKey(const char* name, const uint8_t* private_key);

// Get the current active region name. Returns "" if wildcard.
const char* getActiveRegion();

// Temporarily send the next message unscoped (resets after one use).
void setSendUnscopedOnce(bool v);

// Percent-encode a string for a URL query-component value.
// Returns bytes written (excluding NUL), or 0 on overflow.
// Worst case: every byte becomes %XX (3x expansion + NUL).
size_t urlEncodeQueryValue(const char* in, char* out, size_t out_sz);

// Private region key management (implemented by regions.cpp)
::RegionEntry* addPrivateRegion(const char* name, const uint8_t key[16],
                                const char* parent_name);
bool getPrivateRegionKey(const char* name, uint8_t key_out[16]);

} // namespace mesh
} // namespace sigurdos
