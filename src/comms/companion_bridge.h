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

enum CompanionCommand : uint8_t {
    CMD_APP_START = 1,
    CMD_SEND_TXT_MSG = 2,
    CMD_SEND_CHANNEL_TXT_MSG = 3,
    CMD_GET_CONTACTS = 4,
    CMD_GET_DEVICE_TIME = 5,
    CMD_SET_DEVICE_TIME = 6,
    CMD_SEND_SELF_ADVERT = 7,
    CMD_SYNC_NEXT_MESSAGE = 10,
    CMD_SET_RADIO_PARAMS = 11,
    CMD_SET_RADIO_TX_POWER = 12,
    CMD_DEVICE_QUERY = 22,
    CMD_EXPORT_PRIVATE_KEY = 23,
    CMD_IMPORT_PRIVATE_KEY = 24,
    CMD_GET_CHANNEL = 31,
    CMD_SET_CHANNEL = 32,
    CMD_SET_DEVICE_PIN = 37,
    CMD_GET_BATT_AND_STORAGE = 20,
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
    RESP_CODE_CONTACT_MSG_RECV_V3 = 16,
    RESP_CODE_CHANNEL_MSG_RECV_V3 = 17,
    RESP_CODE_CHANNEL_INFO = 18,
};

enum CompanionPush : uint8_t {
    PUSH_CODE_SEND_CONFIRMED = 0x82,
    PUSH_CODE_MSG_WAITING = 0x83,
};

enum CompanionError : uint8_t {
    ERR_CODE_UNSUPPORTED_CMD = 1,
    ERR_CODE_NOT_FOUND = 2,
    ERR_CODE_TABLE_FULL = 3,
    ERR_CODE_BAD_STATE = 4,
    ERR_CODE_FILE_IO_ERROR = 5,
    ERR_CODE_ILLEGAL_ARG = 6,
};

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

class CompanionBridgeHost {
public:
    virtual ~CompanionBridgeHost() = default;

    virtual uint32_t blePin() const = 0;
    virtual uint8_t clientRepeat() const = 0;
    virtual uint8_t pathHashMode() const = 0;
    virtual void selfInfo(CompanionSelfInfo& out) const = 0;

    virtual uint32_t currentTime() const = 0;
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
    virtual bool sendAdvert(bool flood) = 0;
    virtual bool setBlePin(uint32_t pin) = 0;
    virtual bool exportPrivateKey(uint8_t* out64) const = 0;
    virtual bool importPrivateKey(const uint8_t* key64) = 0;
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
    bool notifySendConfirmed(uint32_t ack, uint32_t trip_time_ms);

private:
    static constexpr int OFFLINE_QUEUE_SIZE = 16;
    struct Frame {
        uint8_t len;
        uint8_t buf[MAX_FRAME_SIZE];
    };

    void writeOKFrame();
    void writeErrFrame(uint8_t err);
    void writeDisabledFrame();
    void writeContactFrame(uint8_t code, const CompanionContact& contact);
    void writeNoMoreMessages();
    bool offlineFrameExists(const uint8_t* frame, size_t len) const;
    bool addToOfflineQueue(const uint8_t* frame, size_t len);
    void seedOfflineQueueFromStore(uint32_t since);
    int  getFromOfflineQueue(uint8_t* frame);
    bool buildMessageFrame(const sigurdos::mesh::StoredMessage& msg,
                           uint8_t* out, size_t* out_len);

    BaseSerialInterface* _serial = nullptr;
    CompanionBridgeHost* _host = nullptr;
    uint8_t _app_target_ver = 3;
    uint32_t _last_sync_time = 0;
    uint32_t _iter_filter_since = 0;
    uint32_t _most_recent_lastmod = 0;
    int _contact_iter = -1;
    int _offline_len = 0;
    Frame _offline[OFFLINE_QUEUE_SIZE];
    uint8_t _cmd_frame[MAX_FRAME_SIZE + 1];
    uint8_t _out_frame[MAX_FRAME_SIZE + 1];
};

} // namespace comms
} // namespace sigurdos
