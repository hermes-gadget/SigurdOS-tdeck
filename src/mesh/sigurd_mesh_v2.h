// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// SigurdMeshV2 — BaseChatMesh subclass for SigurdOS.
// Built alongside the original SigurdMesh during Phase 0 migration.
// Once parity is proven, this replaces SigurdMesh entirely.
//
// MeshCore is MIT licensed (meshcore-dev/MeshCore).

#pragma once
#include <string.h>
#include <stdlib.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/TransportKeyStore.h>
#include <SPIFFS.h>
#include "mesh_wrapper.h"
#include "hal/prefs.h"
#include "regions.h"
#include "hal/tdeck_board.h"
#include "hal/battery.h"
#include "hal/gps.h"
#include "../diagnostics/debug_cfg.h"
#include <helpers/sensors/LPPDataHelpers.h>

namespace sigurdos {
namespace mesh {

// RSSI/SNR side-channel — BaseChatMesh::ContactInfo doesn't carry signal data
struct SignalSample {
    uint8_t key[4];
    int     rssi;
    float   snr;
    uint32_t updated_at;
};

// NOTE: PingResult is declared in mesh_wrapper.h (sigurdos::mesh::PingResult).

class SigurdMeshV2 : public ::BaseChatMesh {
public:
    SigurdMeshV2(::mesh::Radio& radio, ::mesh::MillisecondClock& clock, ::mesh::RNG& rng,
               ::mesh::RTCClock& rtc, ::mesh::PacketManager& pm, ::mesh::MeshTables& mt)
        : BaseChatMesh(radio, clock, rng, rtc, pm, mt) {}
    ~SigurdMeshV2() {}

    // ── Identity & name ─────────────────────────
    char _own_name[32] = "SigurdOS";

    void setOwnName(const char* name) {
        if (name) {
            strncpy(_own_name, name, sizeof(_own_name) - 1);
            _own_name[sizeof(_own_name) - 1] = '\0';
        }
    }
    const char* getOwnName() const { return _own_name; }

    // _message_cb and setMessageCallback() were removed in 2026-06 — dead code
    // after the RX double-queue fix removed _message_cb(...) invocations from all
    // message handlers. See mesh_wrapper.cpp for removal history.

    // ── RSSI/SNR side-channel ───────────────────
    static constexpr int SIGNAL_SAMPLES_MAX = 64;
    SignalSample _signal_samples[SIGNAL_SAMPLES_MAX];
    int _n_signal_samples = 0;

    // ── Signal history for sparkline ──────────────
    struct SignalHistorySample {
        int rssi;
        float snr;
        uint32_t timestamp;
    };
    static constexpr int SIGNAL_HISTORY_MAX = 64;
    SignalHistorySample _signal_history[SIGNAL_HISTORY_MAX];
    int _signal_history_head = 0;
    int _signal_history_count = 0;

    void pushSignalHistory(int rssi, float snr);


    int getSignalHistoryCount() const { return _signal_history_count; }
    int getSignalHistoryRSSI(int idx) const {
        if (idx < 0 || idx >= _signal_history_count) return 0;
        int real = (_signal_history_head - _signal_history_count + idx + SIGNAL_HISTORY_MAX) % SIGNAL_HISTORY_MAX;
        return _signal_history[real].rssi;
    }
    float getSignalHistorySNR(int idx) const {
        if (idx < 0 || idx >= _signal_history_count) return 0.0f;
        int real = (_signal_history_head - _signal_history_count + idx + SIGNAL_HISTORY_MAX) % SIGNAL_HISTORY_MAX;
        return _signal_history[real].snr;
    }

    void updateSignalSample(const uint8_t* pub_key, int rssi, float snr);

    int getContactRSSI(const uint8_t* pub_key) const;

    float getContactSNR(const uint8_t* pub_key) const;


    // ── Trace route ─────────────────────────────
    bool     _has_trace_result = false;
    uint32_t _last_trace_tag = 0;
    uint8_t  _last_trace_len = 0;
    uint8_t  _last_trace_snrs[MAX_PATH_SIZE] = {0};
    uint8_t  _last_trace_hashes[MAX_PATH_SIZE] = {0};

    // Send a TRACE packet to a contact (by index). Requires a known direct path.
    bool sendTrace(int contact_idx, uint32_t tag);


    // Companion CMD_SEND_TRACE_PATH: trace an explicit caller-supplied path with
    // a caller-chosen tag/auth/flags. Fills est_timeout for RESP_CODE_SENT.
    bool sendTracePathRaw(uint32_t tag, uint32_t auth, uint8_t flags,
                          const uint8_t* path, uint8_t path_len, uint32_t& est_timeout);


    // Companion CMD_SEND_LOGIN: send a login and register the login entry so the
    // response is matched in onContactResponse. Returns the MSG_SEND_* result.
    int sendLoginCompanion(const ::ContactInfo& contact, const char* password,
                           uint32_t& est_timeout);


    // Public companion shims for the protected keep-alive connection helpers.
    bool companionHasConnection(const uint8_t* pub_key);

    void companionStopConnection(const uint8_t* pub_key);


    // Companion CMD_EXPORT_CONTACT (self): serialise this node's advert into out.
    // Returns bytes written (0 on failure).
    int exportSelfContact(const char* name, uint8_t* out, size_t out_cap);


    void onTraceRecv(::mesh::Packet* pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                     const uint8_t* path_snrs, const uint8_t* path_hashes,
                     uint8_t path_len) override;


    bool hasTraceResult() { return _has_trace_result; }
    uint8_t getTracePathLen() { return _last_trace_len; }
    void getTracePath(uint8_t* snrs_out, uint8_t* hashes_out);

    void clearTraceResult() { _has_trace_result = false; _last_trace_len = 0; }

    // ── Ping Nearby ─────────────────────────────
    static constexpr int PING_RESULTS_MAX = 32;
    static constexpr uint32_t PING_COOLDOWN_MS = 30000;
    static constexpr uint32_t PING_WINDOW_MS = 3000;

    uint32_t _ping_tag = 0;
    uint32_t _ping_sent_at = 0;
    uint32_t _ping_last_at = 0;
    int      _ping_n_results = 0;
    PingResult _ping_results[PING_RESULTS_MAX];

    // Send a zero-hop PING control packet to discover nearby nodes.
    bool sendPingNearby();


    void onControlDataRecv(::mesh::Packet* pkt) override;


    bool pingIsActive() {
        return _ping_sent_at > 0 && (_ms->getMillis() - _ping_sent_at) < PING_WINDOW_MS;
    }
    bool pingOnCooldown() {
        return _ping_last_at > 0 && (_ms->getMillis() - _ping_last_at) < PING_COOLDOWN_MS;
    }
    uint32_t pingCooldownRemaining() {
        if (!pingOnCooldown()) return 0;
        return PING_COOLDOWN_MS - (_ms->getMillis() - _ping_last_at);
    }
    uint32_t activePingRemaining() {
        if (!pingIsActive()) return 0;
        return PING_WINDOW_MS - (_ms->getMillis() - _ping_sent_at);
    }
    int getPingResultCount() { return _ping_n_results; }
    const PingResult* getPingResult(int i) {
        if (i < 0 || i >= _ping_n_results) return nullptr;
        return &_ping_results[i];
    }

    // ════════════════════════════════════════════════════
    // Room message fetch request type (Phase 4.6)
    // NOTE: 0x03 is REQ_TYPE_GET_TELEMETRY_DATA in room server firmware,
    // so we use 0x06 to avoid conflict.
    static constexpr uint8_t REQ_TYPE_GET_ROOM_MSGS = 0x06;
    static constexpr uint8_t REQ_TYPE_GET_TELEMETRY_DATA = 0x03;

    //  REQ/RESPONSE framework (Phase 4.1)
    // ════════════════════════════════════════════════════

    static constexpr int MAX_PENDING_REQUESTS = 8;
    struct PendingRequest {
        uint32_t tag;
        char     dest_name[32];
        uint8_t  req_type;       // request type (0 = unknown/data)
        char     channel_name[32]; // for REQ_TYPE_GET_ROOM_MSGS: which channel to fetch
        uint32_t sent_at_ms;
        bool     in_use = false;
    };
    PendingRequest _pending_reqs[MAX_PENDING_REQUESTS];

    static constexpr int MAX_RESPONSES = 8;
    static constexpr int MAX_RESPONSE_DATA = 128;
    struct ResponseEntry {
        uint32_t tag;
        char     contact_name[32];
        uint8_t  data[MAX_RESPONSE_DATA];
        uint8_t  len;
        bool     valid;
    };
    ResponseEntry _responses[MAX_RESPONSES];
    int _n_responses = 0;

    // Send a typed REQ to a contact by name. Returns true if sent.
    // The response arrives via onContactResponse() and is stored in _responses[].
    bool sendRequest(const char* name, uint8_t req_type);


    // Send a custom-data REQ to a contact by name.
    bool sendRequestWithData(const char* name, const uint8_t* data, uint8_t data_len);


    // Polling API for received responses
    int getResponseCount() const { return _n_responses; }
    const ResponseEntry* getResponse(int idx) const {
        if (idx < 0 || idx >= _n_responses) return nullptr;
        return &_responses[idx];
    }
    void clearResponses() { _n_responses = 0; }

    // ── Room message fetch (Phase 4.6) ─────────────────
    static constexpr int MAX_ROOM_MSG_FETCH = 16;
    struct RoomMsgFetchEntry {
        char sender[32];
        char text[160];
        uint32_t timestamp;
        char channel[32];
        bool valid;
    };
    RoomMsgFetchEntry _room_fetch_buf[MAX_ROOM_MSG_FETCH];
    int _n_room_fetched = 0;

    // Send a request to fetch recent messages from a room server.
    // Sends a REQ with REQ_TYPE_GET_ROOM_MSGS, data = channel_name.
    // Responses populate _room_fetch_buf and are also pushed to mesh_v2_queue.
    bool sendRoomMsgFetchRequest(const char* name, const char* channel_name);


    // Polling API for fetched room messages
    int getRoomMsgFetchCount() const { return _n_room_fetched; }
    const RoomMsgFetchEntry* getRoomMsgFetchEntry(int idx) const {
        if (idx < 0 || idx >= _n_room_fetched) return nullptr;
        return &_room_fetch_buf[idx];
    }
    void clearRoomMsgFetch() { _n_room_fetched = 0; }

    // Parse room message response data (tag has already been matched)
    void parseRoomMsgResponse(const ::ContactInfo& contact,
                              const uint8_t* data, uint8_t len,
                              const char* channel_name);


    // ════════════════════════════════════════════════════
    //  BaseChatMesh pure virtual handlers
    // ════════════════════════════════════════════════════

    void onDiscoveredContact(::ContactInfo& contact, bool is_new,
                             uint8_t path_len, const uint8_t* path) override;


    // Notify the phone app when the oldest contact is evicted to make room.
    void onContactOverwrite(const uint8_t* pub_key) override;


    // ── ACK tracking ──────────────────────────────
    struct PendingAck {
        // All members carry in-class default initializers so that the
        // default-constructed _pending_acks[] array below is fully zeroed.
        // Without these, dest_name/timestamp/expected_ack/sent_at_ms hold
        // garbage until first write, which is fragile for ACK matching.
        char dest_name[32] = {};
        uint32_t timestamp = 0;
        uint32_t expected_ack = 0;
        uint32_t sent_at_ms = 0;
        bool in_use = false;
    };
    static constexpr int MAX_PENDING_ACKS = 16;
    PendingAck _pending_acks[MAX_PENDING_ACKS];

    void addPendingAck(const char* name, uint32_t ts, uint32_t expected_ack);


    ::ContactInfo* processAck(const uint8_t* data) override;


    void onMessageRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt,
                       uint32_t sender_timestamp, const char* text) override;


    void onCommandDataRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt,
                           uint32_t sender_timestamp, const char* text) override;


    // ── Anonymous data (Phase 4.7) ──────────────────────
    void onAnonDataRecv(::mesh::Packet* pkt, const uint8_t* secret,
                        const ::mesh::Identity& sender,
                        uint8_t* data, size_t len) override;

    void onSignedMessageRecv(const ::ContactInfo& contact, ::mesh::Packet* pkt,
                             uint32_t sender_timestamp, const uint8_t* sender_prefix,
                             const char* text) override;


    uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override {
        return 500 + (uint32_t)(16.0f * pkt_airtime_millis);
    }

    uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis,
                                        uint8_t path_len) const override {
        return 500 + (uint32_t)((pkt_airtime_millis * 6.0f + 250.0f) *
                                (float)((path_len & 63) + 1));
    }

    void onSendTimeout() override {}

    void onChannelMessageRecv(const ::mesh::GroupChannel& channel, ::mesh::Packet* pkt,
                              uint32_t timestamp, const char* text) override;


    uint8_t onContactRequest(const ::ContactInfo& contact, uint32_t sender_timestamp,
                             const uint8_t* data, uint8_t len, uint8_t* reply) override;


    void onContactResponse(const ::ContactInfo& contact, const uint8_t* data,
                           uint8_t len) override;


    void onContactPathUpdated(const ::ContactInfo& contact) override;


    // ── Path discovery (Phase 4.4) ──────────────
    static constexpr int MAX_DISCOVERY_PENDING = 4;
    struct DiscoveryPending {
        char     dest_name[32];
        uint32_t tag;
        bool     in_use = false;
        bool     completed = false;
        uint32_t started_at_ms;
    };
    DiscoveryPending _discovery_pending[MAX_DISCOVERY_PENDING];

    // Send a path discovery request — forces flood routing to learn the return path.
    // Returns the request tag (>0) on success, 0 on failure.
    uint32_t sendPathDiscovery(const char* name);


    // Check if a pending discovery has completed
    bool isDiscoveryComplete(const char* name) {
        for (int i = 0; i < MAX_DISCOVERY_PENDING; i++) {
            if (_discovery_pending[i].in_use &&
                strcmp(_discovery_pending[i].dest_name, name) == 0) {
                return _discovery_pending[i].completed;
            }
        }
        return false;
    }

    // Get path length for a contact (OUT_PATH_UNKNOWN = 0xFF if unknown)
    uint8_t getPathLen(const char* name) {
        int n = getNumContacts();
        ::ContactInfo tmp;
        for (int i = 0; i < n; i++) {
            if (getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
                return tmp.out_path_len;
            }
        }
        return OUT_PATH_UNKNOWN;
    }

    // ── SPIFFS blob persistence ─────────────────

    int getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) override;


    bool putBlobByKey(const uint8_t key[], int key_len,
                       const uint8_t src_buf[], int len) override;


    // ── Behavior overrides ──────────────────────

    bool allowPacketForward(const ::mesh::Packet* packet) override;


    bool isAutoAddEnabled() const override { return true; }
    bool shouldAutoAddContactType(uint8_t type) const override;

    bool shouldOverwriteWhenFull() const override { return true; }
    uint8_t getAutoAddMaxHops() const override {
        return sigurdos::prefs_get().flood_max_hops;
    }
    void onContactsFull() override {
        sigurdos::mesh::mesh_v2_companion_contacts_full_push();
    }
    float getAirtimeBudgetFactor() const override {
        sigurdos::NodePrefs p = sigurdos::prefs_get();
        if (p.duty_cycle == 0) return -1.0f;
        return (float)p.duty_cycle / 100.0f;
    }
    uint8_t getExtraAckTransmitCount() const override {
        return sigurdos::prefs_get().multi_acks ? 1 : 0;
    }

    // ── Repeater/room login session tracking (Phase 4.5) ──
    static constexpr int MAX_LOGIN_ENTRIES = 4;

    // Login status: NONE=no login, PENDING=request sent, OK=logged in, FAILED=login rejected
    enum LoginStatus : uint8_t {
        LOGIN_NONE = 0,
        LOGIN_PENDING,
        LOGIN_OK,
        LOGIN_FAILED
    };

    struct LoginEntry {
        char     contact_name[32];
        uint8_t  permission;        // server permission byte (0=guest, 1=admin, etc.)
        uint8_t  acl_permissions;   // v7+ ACL byte
        uint8_t  status;            // LoginStatus
        uint32_t started_at_ms;     // when login was initiated (for timeout)
        bool     in_use = false;
    };
    LoginEntry _login_entries[MAX_LOGIN_ENTRIES];

    int findLoginEntry(const char* name) const {
        for (int i = 0; i < MAX_LOGIN_ENTRIES; i++) {
            if (_login_entries[i].in_use &&
                strcmp(_login_entries[i].contact_name, name) == 0)
                return i;
        }
        return -1;
    }

    int addLoginEntry(const char* name);


    void removeLoginEntry(const char* name) {
        int idx = findLoginEntry(name);
        if (idx >= 0) {
            _login_entries[idx].in_use = false;
            _login_entries[idx].status = LOGIN_NONE;
        }
    }

    bool isLoggedIn(const char* name) const {
        int idx = findLoginEntry(name);
        return idx >= 0 && _login_entries[idx].status == LOGIN_OK;
    }

    uint8_t getLoginPermission(const char* name) const {
        int idx = findLoginEntry(name);
        return idx >= 0 ? _login_entries[idx].permission : 0;
    }

    uint8_t getLoginStatus(const char* name) const {
        int idx = findLoginEntry(name);
        return idx >= 0 ? static_cast<uint8_t>(_login_entries[idx].status)
                        : static_cast<uint8_t>(LOGIN_NONE);
    }

    // Send a login request to a repeater or room server contact.
    // Uses BaseChatMesh::sendLogin() which sends as PAYLOAD_TYPE_ANON_REQ.
    // The response arrives in onContactResponse() with RESP_SERVER_LOGIN_OK at data[4].
    void sendLoginTo(const ::ContactInfo& contact, const char* password);


    // Logout: stop the keep-alive connection and clear the session.
    void sendLogoutTo(const ::ContactInfo& contact);


    // Send an admin CLI command to a logged-in repeater/room server.
    // Uses BaseChatMesh::sendCommandData() which sends as PAYLOAD_TYPE_TXT_MSG
    // with TXT_TYPE_CLI_DATA. The reply arrives in onCommandDataRecv or as a signed message.
    bool sendCommandDataTo(const ::ContactInfo& contact, const char* text);


    void setDutyCycle(uint8_t percent) {
        sigurdos::NodePrefs p = sigurdos::prefs_get();
        p.duty_cycle = percent;
        sigurdos::prefs_set(p);
    }

    int getContactCount() { return getNumContacts(); }

    // Number of populated channels. BaseChatMesh keeps them contiguous from
    // slot 0; an empty name marks the end.
    int getChannelCount();


    // Transient pointer accessors backed by per-instance caches. Callers use
    // the returned pointer before the next call (matching SigurdMesh usage).
    ChannelDetails _ch_cache;
    const ChannelDetails* getChannel(int idx);


    const char* getChannelName(int idx) {
        const ChannelDetails* c = getChannel(idx);
        return c ? c->name : "";
    }

    ::ContactInfo _contact_cache;
    const ::ContactInfo* getContact(int idx);


    bool removeContact(int idx);


    bool resetPathTo(int idx);


    bool removeChannel(int idx);


    // Base64 PSK decode (self-contained, matches MeshCore's alphabet).
    static int decode_b64(const char* in, size_t in_len, uint8_t* out, size_t out_cap);


    // bool-returning addChannel for wrapper compatibility. Inserts via
    // setChannel() at the next free slot (keeps the channel array contiguous
    // and consistent with getChannelCount()).
    bool addChannelBool(const char* name, const char* psk_base64);


    // Hashtag channel (no PSK): secret = sha256(name), hash = sha256(secret).
    bool addHashtagChannel(const char* name);


    bool loadChannel(const uint8_t* secret, size_t secret_len,
                     const uint8_t* hash, const char* name);


    // ── Send helpers ────────────────────────────

    bool sendTextTo(const char* name, const char* text);


    // Overload that accepts a pre-captured timestamp so the caller (UI layer)
    // can store the same value for ACK matching. Prevents the ~1-2% silent ACK
    // failure caused by timestamps diverging across separate getCurrentTime() calls.
    bool sendTextTo(const char* name, const char* text, uint32_t fixed_ts);


    bool sendGroupText(int idx, const char* text);


    // ── Group data datagrams (Phase 4.8) ────────────
    // Standard group data types
    static constexpr uint16_t GDT_NONE        = 0x0000;
    static constexpr uint16_t GDT_TEMPERATURE = 0x0001;
    static constexpr uint16_t GDT_HUMIDITY    = 0x0002;
    static constexpr uint16_t GDT_PRESSURE    = 0x0003;
    static constexpr uint16_t GDT_LOCATION    = 0x0004;
    static constexpr uint16_t GDT_BATTERY     = 0x0005;
    static constexpr uint16_t GDT_STATUS      = 0x0006;
    static constexpr uint16_t GDT_CUSTOM      = 0x00FF;

    static constexpr int MAX_GROUP_DATA_RECV = 8;
    struct GroupDataEntry {
        uint16_t data_type;
        uint8_t  data[64];
        uint8_t  data_len;
        char     channel_name[32];
        uint32_t timestamp;
        bool     valid;
    };
    GroupDataEntry _grp_data_recv[MAX_GROUP_DATA_RECV];
    int _n_grp_data_recv = 0;

    // Send a typed data datagram to a group channel.
    bool sendGroupDataToChannel(int idx, uint16_t data_type,
                                const uint8_t* data, int data_len);


    bool sendGroupDataToChannel(int idx, const uint8_t* path, uint8_t path_len,
                                uint16_t data_type, const uint8_t* data, int data_len);


    // Override to receive group data datagrams.
    void onChannelDataRecv(const ::mesh::GroupChannel& channel,
                           ::mesh::Packet* pkt, uint16_t data_type,
                           const uint8_t* data, size_t data_len) override;


    // Polling API
    int getGroupDataCount() const;

    const GroupDataEntry* getGroupDataEntry(int idx) const;

    void clearGroupData();


    // ── Anonymous message (Phase 4.7) ────────────────
    // Send a text message to a node identified by raw pubkey bytes (not in contact list).
    // Creates a temporary ContactInfo and uses BaseChatMesh::sendAnonReq().
    // The recipient sees it via onAnonDataRecv with a fallback name.
    bool sendAnonMessage(const uint8_t* pub_key, const char* text);


    // Static hex-to-bytes helper (used by wrapper)
    static int hexToBytes(const char* hex, uint8_t* out, size_t out_max);


    // ── Flood advert ────────────────────────────
    void broadcastAdvert(const char* name, uint8_t adv_type = ADV_TYPE_CHAT);

    void broadcastAdvert(const char* name, double lat, double lon,
                         uint8_t adv_type = ADV_TYPE_CHAT);


    float getPacketSNR() const;


    // ── Advert path tracking ──────────────────────
    static constexpr int ADVERT_PATH_TABLE_SIZE = 16;
    struct AdvertPathEntry {
        uint8_t pubkey_prefix[7];  // first 7 bytes of pub_key
        uint8_t path_len;          // number of hops in the advert path
        char    name[32];
        uint32_t recv_timestamp;
    };
    AdvertPathEntry _advert_paths[ADVERT_PATH_TABLE_SIZE] = {};

    void storeAdvertPath(const uint8_t* pub_key, const char* name,
                         uint8_t path_len, const uint8_t* /*path*/) {
        if (!pub_key || !name) return;
        // Find existing entry for this pubkey or the oldest slot
        AdvertPathEntry* target = &_advert_paths[0];
        uint32_t oldest = _advert_paths[0].recv_timestamp;
        for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
            if (memcmp(_advert_paths[i].pubkey_prefix, pub_key,
                       sizeof(_advert_paths[i].pubkey_prefix)) == 0) {
                target = &_advert_paths[i];
                break;
            }
            if (_advert_paths[i].recv_timestamp < oldest) {
                oldest = _advert_paths[i].recv_timestamp;
                target = &_advert_paths[i];
            }
        }
        memcpy(target->pubkey_prefix, pub_key, sizeof(target->pubkey_prefix));
        target->path_len = path_len;
        strncpy(target->name, name, sizeof(target->name) - 1);
        target->name[sizeof(target->name) - 1] = '\0';
        target->recv_timestamp = getRTCClock()->getCurrentTime();
    }

    // Returns the inbound advert path length (hops) for a contact, or 0 if unknown.
    uint8_t getAdvertPathLen(const char* name) const;


    // NOTE: airtime/packet-count stats (getTotalAirTime, getReceiveAirTime,
    // resetStats, getNumSent/RecvFlood/Direct) and getRemainingTxBudget are
    // inherited directly from mesh::Dispatcher — no overrides needed.

    // ── Flood scope (regions) ─────────────────────────
    // Override sendFloodScoped() to stamp transport codes
    // when an active region scope is set.
protected:
    void sendFloodScoped(const ::ContactInfo& /*recipient*/,
                         ::mesh::Packet* pkt, uint32_t delay_millis = 0) override {
        sendScopedImpl(pkt, delay_millis);
    }
    void sendFloodScoped(const ::mesh::GroupChannel& /*channel*/,
                         ::mesh::Packet* pkt, uint32_t delay_millis = 0) override {
        sendScopedImpl(pkt, delay_millis);
    }

public:
    /// Set active flood scope from a 16-byte TransportKey.
    /// Pass nullptr to clear (unscoped floods).
    void setActiveScope(const uint8_t* key16);


    /// Clear the active flood scope — all floods become unscoped.
    void clearActiveScope() {
        memset(_active_scope.key, 0, sizeof(_active_scope.key));
        _send_unscoped = false;
    }

    /// Temporarily send the next flood unscoped (resets after one use).
    void setSendUnscopedOnce(bool v) {
        _send_unscoped = v;
    }

    /// Returns true if no active scope is set.
    bool isActiveScopeNull() const {
        return _active_scope.isNull();
    }

    /// Copy the active flood-scope key. Returns false if no scope is set.
    bool copyActiveScope(uint8_t* key16) const {
        if (!key16 || _active_scope.isNull()) return false;
        memcpy(key16, _active_scope.key, sizeof(_active_scope.key));
        return true;
    }

private:
    void sendScopedImpl(::mesh::Packet* pkt, uint32_t delay_millis) {
        if (!pkt) return;
        // Multibyte support: originate with the configured path hash size
        // (mode 0/1/2 → 1/2/3 bytes), matching the MeshCore companion firmware.
        uint8_t hash_size = pathHashSize();
        if (_send_unscoped || _active_scope.isNull()) {
            _send_unscoped = false;  // one-shot: reset after use
            sendFlood(pkt, delay_millis, hash_size);
            return;
        }
        uint16_t codes[2];
        codes[0] = _active_scope.calcTransportCode(pkt);
        codes[1] = 0;  // home/return region — REVISIT upstream
        sendFlood(pkt, codes, delay_millis, hash_size);
    }

    // Path hash size for originated packets: prefs path_hash_mode (0-2) + 1,
    // clamped to the valid 1-3 byte range accepted by Mesh::sendFlood().
    static uint8_t pathHashSize() {
        uint8_t mode = sigurdos::prefs_get().path_hash_mode;
        if (mode > 2) mode = 0;
        return mode + 1;
    }

    TransportKey _active_scope;
    bool _send_unscoped = false;
};

} // namespace mesh
} // namespace sigurdos
