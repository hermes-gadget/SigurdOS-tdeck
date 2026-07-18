// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

#include "mesh/message_store.h"
#include <helpers/BaseSerialInterface.h>

namespace sigurdos {
namespace comms {

static constexpr uint8_t SIGURDOS_COMPANION_FIRMWARE_VER_CODE = 12;
static constexpr size_t  SIGURDOS_COMPANION_PUB_KEY_SIZE = 32;
static constexpr size_t  SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE = 6;
static constexpr size_t  SIGURDOS_COMPANION_PATH_SIZE = 64;
static constexpr uint32_t SIGURDOS_COMPANION_BLE_PIN_MIN = 100000;
static constexpr uint32_t SIGURDOS_COMPANION_BLE_PIN_MAX = 999999;
static constexpr size_t  SIGURDOS_COMPANION_CHANNEL_DATA_MAX_PAYLOAD = MAX_FRAME_SIZE - 9;
static constexpr size_t  SIGURDOS_COMPANION_PUSH_BLOB_MAX_PAYLOAD =
    MAX_FRAME_SIZE - 2 - SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE;

enum CompanionCommand : uint8_t {
    CMD_APP_START = 1,
    CMD_SEND_TXT_MSG = 2,
    CMD_SEND_CHANNEL_TXT_MSG = 3,
    CMD_GET_CONTACTS = 4,
    CMD_GET_DEVICE_TIME = 5,
    CMD_SET_DEVICE_TIME = 6,
    CMD_SEND_SELF_ADVERT = 7,
    CMD_SET_ADVERT_NAME = 8,
    CMD_ADD_UPDATE_CONTACT = 9,
    CMD_SYNC_NEXT_MESSAGE = 10,
    CMD_SET_RADIO_PARAMS = 11,
    CMD_SET_RADIO_TX_POWER = 12,
    CMD_RESET_PATH = 13,
    CMD_SET_ADVERT_LATLON = 14,
    CMD_REMOVE_CONTACT = 15,
    CMD_SHARE_CONTACT = 16,
    CMD_EXPORT_CONTACT = 17,
    CMD_IMPORT_CONTACT = 18,
    CMD_REBOOT = 19,
    CMD_GET_BATT_AND_STORAGE = 20,
    CMD_SET_TUNING_PARAMS = 21,
    CMD_DEVICE_QUERY = 22,
    CMD_EXPORT_PRIVATE_KEY = 23,
    CMD_IMPORT_PRIVATE_KEY = 24,
    CMD_SEND_RAW_DATA = 25,
    CMD_SEND_LOGIN = 26,
    CMD_SEND_STATUS_REQ = 27,
    CMD_HAS_CONNECTION = 28,
    CMD_LOGOUT = 29,
    CMD_GET_CONTACT_BY_KEY = 30,
    CMD_GET_CHANNEL = 31,
    CMD_SET_CHANNEL = 32,
    CMD_SIGN_START = 33,
    CMD_SIGN_DATA = 34,
    CMD_SIGN_FINISH = 35,
    CMD_SEND_TRACE_PATH = 36,
    CMD_SET_DEVICE_PIN = 37,
    CMD_SET_OTHER_PARAMS = 38,
    CMD_SEND_TELEMETRY_REQ = 39,
    CMD_GET_CUSTOM_VARS = 40,
    CMD_SET_CUSTOM_VAR = 41,
    CMD_GET_ADVERT_PATH = 42,
    CMD_GET_TUNING_PARAMS = 43,
    CMD_SEND_BINARY_REQ = 50,
    CMD_FACTORY_RESET = 51,
    CMD_SEND_PATH_DISCOVERY_REQ = 52,
    CMD_SET_FLOOD_SCOPE_KEY = 54,
    CMD_SEND_CONTROL_DATA = 55,
    CMD_GET_STATS = 56,
    CMD_SEND_ANON_REQ = 57,
    CMD_SET_AUTOADD_CONFIG = 58,
    CMD_GET_AUTOADD_CONFIG = 59,
    CMD_GET_ALLOWED_REPEAT_FREQ = 60,
    CMD_SET_PATH_HASH_MODE = 61,
    CMD_SEND_CHANNEL_DATA = 62,
    CMD_SET_DEFAULT_FLOOD_SCOPE = 63,
    CMD_GET_DEFAULT_FLOOD_SCOPE = 64,
    CMD_SEND_RAW_PACKET = 65,
};

// Sub-types for CMD_GET_STATS / RESP_CODE_STATS.
enum CompanionStatsType : uint8_t {
    STATS_TYPE_CORE = 0,
    STATS_TYPE_RADIO = 1,
    STATS_TYPE_PACKETS = 2,
};

enum CompanionResponse : uint8_t {
    RESP_CODE_OK = 0,
    RESP_CODE_ERR = 1,
    RESP_CODE_CONTACTS_START = 2,
    RESP_CODE_CONTACT = 3,
    RESP_CODE_END_OF_CONTACTS = 4,
    RESP_CODE_SELF_INFO = 5,
    RESP_CODE_SENT = 6,
    RESP_CODE_CONTACT_MSG_RECV = 7,
    RESP_CODE_CHANNEL_MSG_RECV = 8,
    RESP_CODE_CURR_TIME = 9,
    RESP_CODE_NO_MORE_MESSAGES = 10,
    RESP_CODE_BATT_AND_STORAGE = 12,
    RESP_CODE_DEVICE_INFO = 13,
    RESP_CODE_PRIVATE_KEY = 14,
    RESP_CODE_DISABLED = 15,
    RESP_CODE_EXPORT_CONTACT = 11,
    RESP_CODE_CONTACT_MSG_RECV_V3 = 16,
    RESP_CODE_CHANNEL_MSG_RECV_V3 = 17,
    RESP_CODE_CHANNEL_INFO = 18,
    RESP_CODE_SIGN_START = 19,
    RESP_CODE_SIGNATURE = 20,
    RESP_CODE_CUSTOM_VARS = 21,
    RESP_CODE_ADVERT_PATH = 22,
    RESP_CODE_TUNING_PARAMS = 23,
    RESP_CODE_STATS = 24,
    RESP_CODE_AUTOADD_CONFIG = 25,
    RESP_ALLOWED_REPEAT_FREQ = 26,
    RESP_CODE_CHANNEL_DATA_RECV = 27,
    RESP_CODE_DEFAULT_FLOOD_SCOPE = 28,
};

enum CompanionPush : uint8_t {
    PUSH_CODE_ADVERT = 0x80,
    PUSH_CODE_PATH_UPDATED = 0x81,
    PUSH_CODE_SEND_CONFIRMED = 0x82,
    PUSH_CODE_MSG_WAITING = 0x83,
    PUSH_CODE_RAW_DATA = 0x84,
    PUSH_CODE_LOGIN_SUCCESS = 0x85,
    PUSH_CODE_LOGIN_FAIL = 0x86,
    PUSH_CODE_STATUS_RESPONSE = 0x87,
    PUSH_CODE_LOG_RX_DATA = 0x88,
    PUSH_CODE_TRACE_DATA = 0x89,
    PUSH_CODE_NEW_ADVERT = 0x8A,
    PUSH_CODE_TELEMETRY_RESPONSE = 0x8B,
    PUSH_CODE_BINARY_RESPONSE = 0x8C,
    PUSH_CODE_PATH_DISCOVERY_RESPONSE = 0x8D,
    PUSH_CODE_CONTROL_DATA = 0x8E,
    PUSH_CODE_CONTACT_DELETED = 0x8F,
    PUSH_CODE_CONTACTS_FULL = 0x90,
};

enum CompanionError : uint8_t {
    ERR_CODE_UNSUPPORTED_CMD = 1,
    ERR_CODE_NOT_FOUND = 2,
    ERR_CODE_TABLE_FULL = 3,
    ERR_CODE_BAD_STATE = 4,
    ERR_CODE_FILE_IO_ERROR = 5,
    ERR_CODE_ILLEGAL_ARG = 6,
};

inline bool companionBlePinValid(uint32_t pin)
{
    return pin == 0 ||
           (pin >= SIGURDOS_COMPANION_BLE_PIN_MIN &&
            pin <= SIGURDOS_COMPANION_BLE_PIN_MAX);
}

// NOTE: prefixed with COMPANION_ to avoid colliding with the MeshCore
// TXT_TYPE_* macros in helpers/TxtDataHelpers.h, which are transitively
// included by mesh_wrapper.cpp and would otherwise macro-expand these names.
enum CompanionTextType : uint8_t {
    COMPANION_TXT_PLAIN = 0,
    COMPANION_TXT_CLI_DATA = 1,
    COMPANION_TXT_SIGNED_PLAIN = 2,
};

struct CompanionContact {
    uint8_t pub_key[SIGURDOS_COMPANION_PUB_KEY_SIZE];
    uint8_t type;
    uint8_t flags;
    uint8_t out_path_len;
    uint8_t out_path[SIGURDOS_COMPANION_PATH_SIZE];
    char name[32];
    uint32_t last_advert_timestamp;
    uint32_t lastmod;
    int32_t gps_lat;
    int32_t gps_lon;
};

struct CompanionChannel {
    char name[32];
    uint8_t secret[SIGURDOS_COMPANION_PUB_KEY_SIZE];
};

struct CompanionSendResult {
    bool ok;
    bool sent_flood;
    uint32_t expected_ack;
    uint32_t est_timeout;
};

struct CompanionSelfInfo {
    uint8_t pub_key[SIGURDOS_COMPANION_PUB_KEY_SIZE];
    char node_name[32];
    uint8_t advert_type;
    int8_t tx_power_dbm;
    int8_t max_tx_power_dbm;
    int32_t lat;
    int32_t lon;
    bool multi_acks;
    uint8_t advert_loc_policy;
    uint8_t telemetry_modes;
    uint8_t manual_add_contacts;
    uint32_t freq_khz;
    uint32_t bw_hz;
    uint8_t sf;
    uint8_t cr;
};

// Optional fields for CMD_SET_OTHER_PARAMS — each *_present flag mirrors the
// official handler's length-gated parsing (later fields are optional).
struct CompanionOtherParams {
    uint8_t manual_add_contacts;
    uint8_t telemetry_modes;
    bool    telemetry_present;
    uint8_t advert_loc_policy;
    bool    loc_policy_present;
    uint8_t multi_acks;
    bool    multi_acks_present;
};

struct CompanionCoreStats {
    uint16_t batt_mv;
    uint32_t uptime_secs;
    uint16_t err_flags;
    uint8_t  queue_len;
};

struct CompanionRadioStats {
    int16_t noise_floor;
    int8_t  last_rssi;
    int8_t  last_snr_quarters;
    uint32_t tx_air_secs;
    uint32_t rx_air_secs;
};

struct CompanionPacketStats {
    uint32_t recv;
    uint32_t sent;
    uint32_t sent_flood;
    uint32_t sent_direct;
    uint32_t recv_flood;
    uint32_t recv_direct;
    uint32_t recv_errors;
};

static constexpr size_t SIGURDOS_COMPANION_SIGNATURE_SIZE = 64;
static constexpr size_t SIGURDOS_COMPANION_MAX_SIGN_DATA = 1024;

class CompanionBridgeHost {
public:
    virtual ~CompanionBridgeHost() = default;

    virtual uint32_t blePin() const = 0;
    virtual uint8_t clientRepeat() const = 0;
    virtual uint8_t pathHashMode() const = 0;
    virtual void selfInfo(CompanionSelfInfo& out) const = 0;

    virtual uint32_t currentTime() const = 0;
    virtual uint32_t monotonicMillis() const = 0;
    virtual bool setCurrentTime(uint32_t epoch) = 0;
    virtual uint16_t batteryMilliVolts() const = 0;
    virtual uint32_t storageUsedKb() const = 0;
    virtual uint32_t storageTotalKb() const = 0;

    virtual int contactCount() const = 0;
    virtual bool getContact(int index, CompanionContact& out) const = 0;
    virtual bool getContactByPubKeyPrefix(const uint8_t* prefix, size_t prefix_len,
                                          CompanionContact& out) const = 0;

    virtual int channelCount() const = 0;
    virtual bool getChannel(int index, CompanionChannel& out) const = 0;
    virtual bool setChannel(int index, const CompanionChannel& channel) = 0;

    virtual CompanionSendResult sendTextByPubKeyPrefix(const uint8_t* prefix,
                                                       size_t prefix_len,
                                                       uint8_t txt_type,
                                                       uint8_t attempt,
                                                       uint32_t timestamp,
                                                       const char* text) = 0;
    virtual CompanionSendResult sendChannelText(int channel_index,
                                                uint32_t timestamp,
                                                const char* text) = 0;
    virtual bool sendChannelData(int channel_index,
                                 const uint8_t* path,
                                 uint8_t path_len,
                                 uint16_t data_type,
                                 const uint8_t* payload,
                                 size_t payload_len) = 0;
    virtual bool sendRawData(const uint8_t* path,
                             uint8_t path_len,
                             const uint8_t* payload,
                             size_t payload_len) = 0;
    virtual bool sendControlData(const uint8_t* payload, size_t payload_len) = 0;
    virtual bool sendAdvert(bool flood) = 0;
    virtual bool setAdvertName(const char* name) = 0;
    virtual bool setAdvertLatLon(int32_t lat, int32_t lon) = 0;
    virtual bool setBlePin(uint32_t pin) = 0;
    virtual bool exportPrivateKey(uint8_t* out64) const = 0;
    virtual bool importPrivateKey(const uint8_t* key64) = 0;

    // ── Radio / tuning / params ──────────────────────────────
    virtual bool setRadioParams(uint32_t freq_khz, uint32_t bw_hz,
                                uint8_t sf, uint8_t cr, uint8_t client_repeat) = 0;
    virtual bool setRadioTxPower(int8_t dbm) = 0;
    virtual bool setTuningParams(uint32_t rx_base_x1000, uint32_t airtime_x1000) = 0;
    virtual void tuningParams(uint32_t& rx_base_x1000, uint32_t& airtime_x1000) const = 0;
    virtual void setOtherParams(const CompanionOtherParams& p) = 0;
    virtual bool setPathHashMode(uint8_t mode) = 0;
    virtual void getAutoAddConfig(uint8_t* cfg, uint8_t* max_hops) const = 0;
    virtual void setAutoAddConfig(uint8_t cfg, uint8_t max_hops) = 0;
    virtual int8_t maxTxPowerDbm() const = 0;

    // ── Contact CRUD / connection ────────────────────────────
    // Full 32-byte pub key. addOrUpdateContact takes a parsed contact.
    virtual bool getContactByPubKey(const uint8_t* pub_key, CompanionContact& out) const = 0;
    virtual bool addOrUpdateContact(const CompanionContact& c) = 0;
    virtual bool removeContactByPubKey(const uint8_t* pub_key) = 0;
    virtual bool resetPathByPubKey(const uint8_t* pub_key) = 0;
    virtual bool shareContactByPubKey(const uint8_t* pub_key) = 0;
    // exportContactByPubKey: pub_key==nullptr exports SELF. Returns bytes
    // written to out (capacity out_cap), 0 on failure.
    virtual int  exportContactByPubKey(const uint8_t* pub_key, uint8_t* out, size_t out_cap) = 0;
    virtual bool importContact(const uint8_t* data, size_t len) = 0;
    virtual bool hasConnectionTo(const uint8_t* pub_key) const = 0;
    virtual void logout(const uint8_t* pub_key) = 0;

    // ── System ───────────────────────────────────────────────
    virtual void reboot() = 0;
    virtual bool factoryReset() = 0;

    // ── Stats ────────────────────────────────────────────────
    virtual void coreStats(CompanionCoreStats& out) const = 0;
    virtual void radioStats(CompanionRadioStats& out) const = 0;
    virtual void packetStats(CompanionPacketStats& out) const = 0;
    virtual size_t allowedRepeatFreqRanges(uint32_t* lower_upper_pairs, size_t max_pairs) const = 0;

    // ── Flood scope (companion regions) ──────────────────────
    // getDefaultFloodScope returns true and fills name(31)/key(16) if a default
    // scope is set; false means "no default" (null name/key).
    virtual bool getDefaultFloodScope(char* name_out, uint8_t* key_out) const = 0;
    virtual bool setDefaultFloodScope(const char* name, const uint8_t* key) = 0;
    // setFloodScopeOverride: unscoped=true forces wildcard; else key==nullptr
    // resets to default, non-null sets a transient scope key.
    virtual bool setFloodScopeOverride(const uint8_t* key, bool unscoped) = 0;

    // ── Async requests (response arrives later as a PUSH_CODE_*) ──
    virtual CompanionSendResult sendLogin(const uint8_t* pub_key, const char* password) = 0;
    virtual CompanionSendResult sendStatusReq(const uint8_t* pub_key) = 0;
    virtual CompanionSendResult sendTelemetryReq(const uint8_t* pub_key) = 0;
    virtual CompanionSendResult sendBinaryReq(const uint8_t* pub_key,
                                              const uint8_t* data,
                                              uint8_t data_len) = 0;
    virtual CompanionSendResult sendAnonReq(const uint8_t* pub_key,
                                            const uint8_t* data,
                                            uint8_t data_len) = 0;
    virtual void cancelBinaryReq(uint32_t tag) = 0;
    virtual void cancelBinaryReqs() = 0;
    virtual CompanionSendResult sendTracePath(uint32_t tag, uint32_t auth, uint8_t flags,
                                              const uint8_t* path, uint8_t path_len) = 0;
    // sendPathDiscovery initiates flood path discovery for a contact by pubkey.
    // Returns a CompanionSendResult with the assigned tag for RESP_CODE_SENT.
    virtual CompanionSendResult sendPathDiscovery(const uint8_t* pub_key) = 0;
    // selfTelemetry fills a MeshCore telemetry blob (CayenneLPP-style). out_len
    // is set to bytes written (0 if unsupported).
    virtual void selfTelemetry(uint8_t* out, size_t* out_len) const = 0;
    // getCustomVars writes comma-separated companion name:value pairs to out,
    // returns bytes written (excluding null terminator). 0 = no variables.
    virtual int getCustomVars(char* out, size_t out_cap) const = 0;
    // setCustomVar sets a named companion custom variable. Returns true on success.
    virtual bool setCustomVar(const char* name, const char* value) = 0;
    // signData signs len bytes with the node key; returns signature length.
    virtual int signData(const uint8_t* data, size_t len, uint8_t* sig_out) = 0;
    // getAdvertPath returns the encoded MeshCore path descriptor separately
    // from the number of bytes copied. A zero descriptor is a valid zero-hop
    // path, so the boolean return value reports whether an entry was found.
    virtual bool getAdvertPath(const uint8_t* pub_key,
                               uint8_t* path_out, size_t path_capacity,
                               uint8_t* path_descriptor_out,
                               size_t* path_bytes_out,
                               uint32_t* timestamp_out) const = 0;
};

class CompanionBridge {
public:
    void begin(BaseSerialInterface* serial, CompanionBridgeHost* host);
    void loop();
    bool handleFrame(const uint8_t* frame, size_t len);

    bool isEnabled() const;
    bool isConnected() const;
    bool setEnabled(bool enabled);
    uint32_t lastSyncTime() const { return _last_sync_time; }
    uint8_t appTargetVersion() const { return _app_target_ver; }

    bool enqueueMessage(const sigurdos::mesh::StoredMessage& msg);
    bool enqueueChannelData(uint8_t channel_index,
                            int8_t snr_quarters,
                            uint8_t path_len,
                            uint16_t data_type,
                            const uint8_t* payload,
                            size_t payload_len);
    bool notifySendConfirmed(uint32_t ack, uint32_t trip_time_ms);

    // ── Async / live event pushes (called from the mesh fan-out) ──
    // A heard advert / contact update → app. is_new picks NEW_ADVERT vs ADVERT.
    bool pushAdvert(const CompanionContact& contact, bool is_new);
    bool pushPathUpdated(const CompanionContact& contact);
    bool pushContactDeleted(const uint8_t* pub_key);
    bool pushContactsFull();
    bool pushLoginResult(const uint8_t* pubkey_prefix, bool success,
                         uint8_t permission, uint32_t tag,
                         uint8_t acl_permissions, uint8_t firmware_level,
                         bool legacy_success = false);
    bool pushStatusResponse(const uint8_t* pubkey_prefix,
                            const uint8_t* blob, size_t blob_len);
    bool pushTelemetryResponse(const uint8_t* pubkey_prefix,
                               const uint8_t* blob, size_t blob_len);
    bool pushBinaryResponse(uint32_t tag, const uint8_t* blob, size_t blob_len);
    bool pushRawData(int8_t snr_quarters, int8_t rssi,
                     const uint8_t* payload, size_t payload_len);
    bool pushControlData(int8_t snr_quarters, int8_t rssi, uint8_t path_len,
                         const uint8_t* payload, size_t payload_len);
    bool pushTraceData(uint32_t tag, uint32_t auth, uint8_t flags,
                       const uint8_t* path_hashes, const uint8_t* path_snrs,
                       uint8_t path_len, int8_t final_snr_quarters);

private:
    static constexpr int OFFLINE_QUEUE_SIZE = 16;
    static constexpr int MAX_PENDING_BINARY_REQUESTS = 4;
    struct Frame {
        uint32_t store_id;
        bool persistent;    // channel-data frames have no message-store record
        uint8_t len;
        uint8_t buf[MAX_FRAME_SIZE];
    };

    void writeOKFrame();
    void writeErrFrame(uint8_t err);
    void writeDisabledFrame();
    // RESP_CODE_SENT (10 bytes) on success, else an error frame.
    void writeSentOrErr(const CompanionSendResult& r);
    void writeContactFrame(uint8_t code, const CompanionContact& contact);
    void writeNoMoreMessages();
    bool offlineFrameExists(const uint8_t* frame, size_t len) const;
    bool addToOfflineQueue(uint32_t store_id, bool persistent,
                           const uint8_t* frame, size_t len);
    bool refillOfflineQueueFromStore(bool notify_waiting);
    // Peek does not dequeue. removeFirstOfflineFrame() is called only after the
    // transport accepts the complete frame.
    int  peekOfflineQueue(uint8_t* frame, uint32_t* store_id, bool* persistent);
    void removeFirstOfflineFrame();
    bool buildMessageFrame(const sigurdos::mesh::StoredMessage& msg,
                           uint8_t* out, size_t* out_len);
    int findPendingBinary(uint32_t tag) const;
    int findFreePendingBinary() const;
    void expirePendingBinary();
    void clearPendingBinary();

    struct PendingBinaryRequest {
        uint32_t tag = 0;
        uint32_t deadline_ms = 0;
    };

    static constexpr uint32_t BINARY_REQUEST_FALLBACK_TIMEOUT_MS = 30000;

    BaseSerialInterface* _serial = nullptr;
    CompanionBridgeHost* _host = nullptr;
    uint8_t _app_target_ver = 3;
    uint32_t _last_sync_time = 0;
    uint32_t _iter_filter_since = 0;
    uint32_t _most_recent_lastmod = 0;
    int _contact_iter = -1;
    int _offline_len = 0;
    Frame _offline[OFFLINE_QUEUE_SIZE];
    sigurdos::mesh::StoredMessage _offline_snapshot[OFFLINE_QUEUE_SIZE]{};
    PendingBinaryRequest _pending_binary[MAX_PENDING_BINARY_REQUESTS]{};
    uint8_t _cmd_frame[MAX_FRAME_SIZE + 1];
    uint8_t _out_frame[MAX_FRAME_SIZE + 1];

    // CMD_SIGN_START/DATA/FINISH accumulate data here between frames.
    uint8_t _sign_buf[SIGURDOS_COMPANION_MAX_SIGN_DATA];
    size_t  _sign_len = 0;
    bool    _sign_active = false;
    bool    _was_connected = false;  // detect BLE disconnect to clear signing state (#712)

};

} // namespace comms
} // namespace sigurdos
