// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "companion_bridge.h"
#include "mesh/path_codec.h"

#include <cstring>

#if defined(ESP32_PLATFORM)
#include "hal/tdeck_pins.h"
#endif

namespace sigurdos {
namespace comms {

namespace {

static constexpr const char* BUILD_DATE = __DATE__;
static constexpr const char* MANUFACTURER = "SigurdOS";
#if defined(SIGURDOS_VERSION)
static constexpr const char* FIRMWARE_VERSION = SIGURDOS_VERSION;
#else
static constexpr const char* FIRMWARE_VERSION = "SigurdOS";
#endif
static constexpr uint8_t COMPANION_OUT_PATH_UNKNOWN = 0xFF;

static void strzcpy(char* dest, const char* src, size_t dest_sz)
{
    if (!dest || dest_sz == 0) return;
    if (!src) src = "";
    std::strncpy(dest, src, dest_sz - 1);
    dest[dest_sz - 1] = '\0';
}

static size_t boundedTextLen(const char* text, size_t max_len)
{
    if (!text) return 0;
    return strnlen(text, max_len);
}

static bool pathByteLen(uint8_t path_len, size_t* out_len)
{
    if (out_len) *out_len = 0;
    if (path_len == COMPANION_OUT_PATH_UNKNOWN) return true;
    if (!sigurdos::mesh::path::encodedLengthValid(path_len)) return false;
    const size_t n = sigurdos::mesh::path::byteCount(path_len);
    static_assert(SIGURDOS_COMPANION_PATH_SIZE ==
                      sigurdos::mesh::path::MAX_ENCODED_PATH_BYTES,
                  "companion and mesh path capacities must match");
    if (out_len) *out_len = n;
    return true;
}

static size_t commandMinimumLength(uint8_t cmd)
{
    switch (cmd) {
    case CMD_DEVICE_QUERY: return 2;
    case CMD_APP_START: return 8;
    case CMD_SEND_TXT_MSG: return 14;
    case CMD_SEND_CHANNEL_TXT_MSG: return 7;
    case CMD_SEND_CHANNEL_DATA: return 5;
    case CMD_SET_DEVICE_TIME: return 5;
    case CMD_SET_ADVERT_NAME: return 2;
    case CMD_SET_ADVERT_LATLON: return 9;
    case CMD_SET_RADIO_PARAMS: return 11;
    case CMD_SET_RADIO_TX_POWER: return 2;
    case CMD_SET_TUNING_PARAMS: return 9;
    case CMD_GET_CHANNEL: return 2;
    case CMD_SET_CHANNEL: return 2 + 32 + 16;
    case CMD_SET_DEVICE_PIN: return 5;
    case CMD_IMPORT_PRIVATE_KEY: return 65;
    case CMD_SET_OTHER_PARAMS: return 2;
    case CMD_SET_PATH_HASH_MODE: return 3;
    case CMD_SET_AUTOADD_CONFIG: return 2;
    case CMD_GET_CONTACT_BY_KEY:
    case CMD_RESET_PATH:
    case CMD_REMOVE_CONTACT:
    case CMD_SHARE_CONTACT:
    case CMD_HAS_CONNECTION:
    case CMD_LOGOUT:
        return 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE;
    case CMD_ADD_UPDATE_CONTACT:
        return 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE + 2 + 1 +
               SIGURDOS_COMPANION_PATH_SIZE + 32 + 4;
    case CMD_IMPORT_CONTACT: return 2 + 32 + 64 + 1;
    case CMD_REBOOT: return 7;
    case CMD_FACTORY_RESET: return 6;
    case CMD_GET_STATS: return 2;
    case CMD_SET_CUSTOM_VAR: return 4;
    case CMD_GET_ADVERT_PATH:
    case CMD_SEND_PATH_DISCOVERY_REQ:
        return 2 + SIGURDOS_COMPANION_PUB_KEY_SIZE;
    case CMD_SET_FLOOD_SCOPE_KEY: return 2;
    case CMD_SIGN_DATA: return 2;
    case CMD_SEND_LOGIN:
    case CMD_SEND_STATUS_REQ:
        return 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE;
    case CMD_SEND_TELEMETRY_REQ: return 4;
    case CMD_SEND_BINARY_REQ:
    case CMD_SEND_ANON_REQ:
        return 2 + SIGURDOS_COMPANION_PUB_KEY_SIZE;
    case CMD_SEND_TRACE_PATH: return 11;
    case CMD_SEND_RAW_DATA: return 6;
    case CMD_SEND_CONTROL_DATA: return 2;
    default:
        return 0;
    }
}

} // namespace

void CompanionBridge::begin(BaseSerialInterface* serial, CompanionBridgeHost* host)
{
    _serial = serial;
    _host = host;
    _app_target_ver = 3;
    _version_negotiated = true;
    _last_sync_time = 0;
    _contact_iter = -1;
    _offline_len = 0;
    _sign_active = false;
    _sign_len = 0;
    _was_connected = false;
    _pending_response_len = 0;
    _push_drop_count = 0;
    clearPendingBinary();
}

bool CompanionBridge::isEnabled() const
{
    return _serial && _serial->isEnabled();
}

bool CompanionBridge::isConnected() const
{
    return _serial && _serial->isConnected();
}

bool CompanionBridge::setEnabled(bool enabled)
{
    if (!_serial) return false;
    if (enabled) _serial->enable();
    else {
        _serial->disable();
        resetConnectionSession();
        _was_connected = false;
        if (_host) _host->cancelBinaryReqs();
    }
    return _serial->isEnabled() == enabled;
}

int CompanionBridge::findPendingBinary(uint32_t tag) const
{
    if (tag == 0) return -1;
    for (int i = 0; i < MAX_PENDING_BINARY_REQUESTS; ++i) {
        if (_pending_binary[i].tag == tag) return i;
    }
    return -1;
}

int CompanionBridge::findFreePendingBinary() const
{
    for (int i = 0; i < MAX_PENDING_BINARY_REQUESTS; ++i) {
        if (_pending_binary[i].tag == 0) return i;
    }
    return -1;
}

void CompanionBridge::expirePendingBinary()
{
    if (!_host) return;
    const uint32_t now = _host->monotonicMillis();
    for (int i = 0; i < MAX_PENDING_BINARY_REQUESTS; ++i) {
        PendingBinaryRequest& request = _pending_binary[i];
        if (request.tag == 0 || (int32_t)(now - request.deadline_ms) < 0) continue;
        const uint32_t tag = request.tag;
        request = {};
        _host->cancelBinaryReq(tag);
    }
}

void CompanionBridge::clearPendingBinary()
{
    std::memset(_pending_binary, 0, sizeof(_pending_binary));
}

void CompanionBridge::resetConnectionSession()
{
    _app_target_ver = 3;
    _version_negotiated = false;
    _contact_iter = -1;
    _iter_filter_since = 0;
    _most_recent_lastmod = 0;
    _offline_len = 0;
    _sign_active = false;
    _sign_len = 0;
    _pending_response_len = 0;
    clearPendingBinary();
}

bool CompanionBridge::sendResponseFrame(const uint8_t* frame, size_t len)
{
    if (!_serial || !frame || len == 0 || len > MAX_FRAME_SIZE ||
        _pending_response_len != 0) {
        return false;
    }
    if (_serial->writeFrame(frame, len) == len) return true;
    std::memcpy(_pending_response, frame, len);
    _pending_response_len = static_cast<uint8_t>(len);
    return true;
}

bool CompanionBridge::flushPendingResponse()
{
    if (_pending_response_len == 0) return true;
    if (!_serial || !_serial->isConnected()) return false;
    const size_t len = _pending_response_len;
    if (_serial->writeFrame(_pending_response, len) != len) return false;
    _pending_response_len = 0;
    return true;
}

bool CompanionBridge::sendPushFrame(const uint8_t* frame, size_t len)
{
    if (!_serial || !frame || len == 0 || len > MAX_FRAME_SIZE ||
        _serial->writeFrame(frame, len) != len) {
        ++_push_drop_count;
        return false;
    }
    return true;
}

void CompanionBridge::loop()
{
    if (!_serial || !_host || !_serial->isEnabled()) return;

    expirePendingBinary();

    // Clear in-progress signing state on BLE disconnect to prevent
    // cross-session signature injection (#712).  Check before we
    // consume frames so a disconnect + reconnect + malicious FINISH
    // sees a clean slate.
    if (_was_connected && !_serial->isConnected()) {
        resetConnectionSession();
        _host->cancelBinaryReqs();
    }
    _was_connected = _serial->isConnected();
    if (!flushPendingResponse()) return;
    size_t len = _serial->checkRecvFrame(_cmd_frame);
    if (len > 0) {
        handleFrame(_cmd_frame, len);
    }

    if (_contact_iter >= 0 && !_serial->isWriteBusy()) {
        CompanionContact c{};
        while (_contact_iter < _host->contactCount()) {
            int idx = _contact_iter++;
            if (!_host->getContact(idx, c)) continue;
            if (_iter_filter_since != 0 && c.lastmod <= _iter_filter_since) continue;
            if (c.lastmod > _most_recent_lastmod) _most_recent_lastmod = c.lastmod;
            writeContactFrame(RESP_CODE_CONTACT, c);
            return;
        }

        int i = 0;
        _out_frame[i++] = RESP_CODE_END_OF_CONTACTS;
        std::memcpy(&_out_frame[i], &_most_recent_lastmod, 4);
        i += 4;
        sendResponseFrame(_out_frame, i);
        _contact_iter = -1;
    }
}

void CompanionBridge::writeOKFrame()
{
    uint8_t b = RESP_CODE_OK;
    sendResponseFrame(&b, 1);
}

void CompanionBridge::writeErrFrame(uint8_t err)
{
    uint8_t b[2] = { RESP_CODE_ERR, err };
    sendResponseFrame(b, 2);
}

void CompanionBridge::writeDisabledFrame()
{
    uint8_t b = RESP_CODE_DISABLED;
    sendResponseFrame(&b, 1);
}

bool CompanionBridge::writeContactFrame(uint8_t code, const CompanionContact& contact,
                                        bool command_response)
{
    int i = 0;
    _out_frame[i++] = code;
    std::memcpy(&_out_frame[i], contact.pub_key, SIGURDOS_COMPANION_PUB_KEY_SIZE);
    i += SIGURDOS_COMPANION_PUB_KEY_SIZE;
    _out_frame[i++] = contact.type;
    _out_frame[i++] = contact.flags;
    _out_frame[i++] = contact.out_path_len;
    std::memcpy(&_out_frame[i], contact.out_path, SIGURDOS_COMPANION_PATH_SIZE);
    i += SIGURDOS_COMPANION_PATH_SIZE;
    strzcpy((char*)&_out_frame[i], contact.name, 32);
    i += 32;
    std::memcpy(&_out_frame[i], &contact.last_advert_timestamp, 4);
    i += 4;
    std::memcpy(&_out_frame[i], &contact.gps_lat, 4);
    i += 4;
    std::memcpy(&_out_frame[i], &contact.gps_lon, 4);
    i += 4;
    std::memcpy(&_out_frame[i], &contact.lastmod, 4);
    i += 4;
    if (command_response) return sendResponseFrame(_out_frame, i);
    return sendPushFrame(_out_frame, i);
}

void CompanionBridge::writeNoMoreMessages()
{
    uint8_t b = RESP_CODE_NO_MORE_MESSAGES;
    sendResponseFrame(&b, 1);
}

bool CompanionBridge::offlineFrameExists(const uint8_t* frame, size_t len) const
{
    if (!frame || len == 0 || len > MAX_FRAME_SIZE) return false;
    for (int i = 0; i < _offline_len; i++) {
        if (_offline[i].len == len &&
            std::memcmp(_offline[i].buf, frame, len) == 0) {
            return true;
        }
    }
    return false;
}

bool CompanionBridge::addToOfflineQueue(uint32_t store_id, bool persistent,
                                        const uint8_t* frame, size_t len)
{
    if (!frame || len == 0 || len > MAX_FRAME_SIZE) return false;
    if (persistent && store_id == 0) return false;
    if (offlineFrameExists(frame, len)) return false;
    if (_offline_len >= OFFLINE_QUEUE_SIZE) {
        // Durable text retains strict oldest-first order. A volatile channel
        // frame may replace only an older volatile frame.
        if (persistent) return false;
        int volatile_index = -1;
        for (int i = 0; i < _offline_len; ++i) {
            if (!_offline[i].persistent) {
                volatile_index = i;
                break;
            }
        }
        if (volatile_index < 0) return false;
        for (int i = volatile_index + 1; i < _offline_len; ++i) {
            _offline[i - 1] = _offline[i];
        }
        _offline_len--;
    }
    _offline[_offline_len].store_id = store_id;
    _offline[_offline_len].persistent = persistent;
    _offline[_offline_len].len = (uint8_t)len;
    std::memcpy(_offline[_offline_len].buf, frame, len);
    _offline_len++;
    return true;
}

int CompanionBridge::peekOfflineQueue(uint8_t* frame, uint32_t* store_id,
                                      bool* persistent)
{
    if (!frame || _offline_len <= 0) return 0;
    if (store_id) *store_id = _offline[0].store_id;
    if (persistent) *persistent = _offline[0].persistent;
    int len = _offline[0].len;
    std::memcpy(frame, _offline[0].buf, len);
    return len;
}

void CompanionBridge::removeFirstOfflineFrame()
{
    if (_offline_len <= 0) return;
    for (int i = 1; i < _offline_len; i++) _offline[i - 1] = _offline[i];
    _offline_len--;
}

bool CompanionBridge::buildMessageFrame(const sigurdos::mesh::StoredMessage& msg,
                                        uint8_t* out, size_t* out_len)
{
    if (!out || !out_len) return false;
    int i = 0;
    if (msg.is_channel) {
        if (_app_target_ver >= 3) {
            out[i++] = RESP_CODE_CHANNEL_MSG_RECV_V3;
            out[i++] = (uint8_t)msg.snr_quarters;
            out[i++] = 0;
            out[i++] = 0;
        } else {
            out[i++] = RESP_CODE_CHANNEL_MSG_RECV;
        }

        uint8_t channel_idx = 0xFF;
        if (_host) {
            CompanionChannel ch{};
            for (int ci = 0; ci < _host->channelCount(); ci++) {
                if (_host->getChannel(ci, ch) &&
                    std::strncmp(ch.name, msg.conversation, sizeof(ch.name)) == 0) {
                    channel_idx = (uint8_t)ci;
                    break;
                }
            }
        }
        out[i++] = channel_idx;
        out[i++] = msg.path_len;
        out[i++] = msg.txt_type;
        std::memcpy(&out[i], &msg.timestamp, 4);
        i += 4;
    } else {
        if (_app_target_ver >= 3) {
            out[i++] = RESP_CODE_CONTACT_MSG_RECV_V3;
            out[i++] = (uint8_t)msg.snr_quarters;
            out[i++] = 0;
            out[i++] = 0;
        } else {
            out[i++] = RESP_CODE_CONTACT_MSG_RECV;
        }
        std::memcpy(&out[i], msg.sender_prefix, SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE);
        i += SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE;
        out[i++] = msg.path_len;
        out[i++] = msg.txt_type;
        std::memcpy(&out[i], &msg.timestamp, 4);
        i += 4;
        // Signed-plain messages carry a 4-byte sender prefix after the
        // timestamp (exactly as upstream MyMesh queues).
        if (msg.txt_type == COMPANION_TXT_SIGNED_PLAIN && msg.extra_len >= 4) {
            std::memcpy(&out[i], msg.extra, 4);
            i += 4;
        }
    }

    size_t tlen = boundedTextLen(msg.text, sigurdos::mesh::SIGURDOS_MSG_TEXT_LEN - 1);
    if (i + tlen > MAX_FRAME_SIZE) tlen = MAX_FRAME_SIZE - i;
    std::memcpy(&out[i], msg.text, tlen);
    i += (int)tlen;
    *out_len = (size_t)i;
    return true;
}

bool CompanionBridge::refillOfflineQueueFromStore(bool notify_waiting)
{
    // A page is a snapshot of the oldest unsent records. Mixing another page
    // into a partially drained one can duplicate frames and reorder delivery.
    if (_offline_len > 0) return false;

    const int n = sigurdos::mesh::messageStoreLoadUnsent(
        _offline_snapshot, OFFLINE_QUEUE_SIZE);
    bool added_any = false;
    for (int idx = 0; idx < n; idx++) {
        // The offline queue is a mirror of *incoming* messages only. Never feed
        // the app a self/outgoing message: it already has the ones it sent (it
        // got RESP_CODE_SENT), and the companion protocol has no
        // device-originated-send frame — echoing one back arrives as a bogus
        // *incoming* message (mis-attributed sender, duplicate bubble).
        if (_offline_snapshot[idx].is_self) continue;
        uint8_t frame[MAX_FRAME_SIZE];
        size_t len = 0;
        if (buildMessageFrame(_offline_snapshot[idx], frame, &len) &&
            addToOfflineQueue(_offline_snapshot[idx].store_id, true, frame, len)) {
            added_any = true;
        }
    }
    // Do NOT mark any records as sent here. Records are marked individually
    // only when CMD_SYNC_NEXT_MESSAGE successfully writes the frame to the
    // app (see the SYNC_NEXT_MESSAGE handler below).
    if (added_any && notify_waiting && isConnected()) {
        uint8_t tickle = PUSH_CODE_MSG_WAITING;
        sendPushFrame(&tickle, 1);
    }
    return added_any;
}

bool CompanionBridge::enqueueMessage(const sigurdos::mesh::StoredMessage& msg)
{
    // Only incoming messages are mirrored to the app (see
    // refillOfflineQueueFromStore).
    if (msg.is_self) return false;
    uint8_t frame[MAX_FRAME_SIZE];
    size_t len = 0;
    if (!buildMessageFrame(msg, frame, &len)) return false;
    bool added = addToOfflineQueue(msg.store_id, msg.store_id != 0, frame, len);
    if (added) {
        // The record is NOT marked companion_sent here — that happens only when
        // CMD_SYNC_NEXT_MESSAGE successfully writes the frame to the app. This
        // prevents data loss if the app disconnects before draining the queue.
        if (isConnected()) {
            uint8_t tickle = PUSH_CODE_MSG_WAITING;
            sendPushFrame(&tickle, 1);
        }
    }
    // A persistent record rejected because the in-memory page is full is still
    // durably accepted; SYNC_NEXT_MESSAGE will load it on a later page.
    return added || msg.store_id != 0;
}

bool CompanionBridge::enqueueChannelData(uint8_t channel_index,
                                         int8_t snr_quarters,
                                         uint8_t path_len,
                                         uint16_t data_type,
                                         const uint8_t* payload,
                                         size_t payload_len)
{
    if (payload_len > SIGURDOS_COMPANION_CHANNEL_DATA_MAX_PAYLOAD) return false;
    if (payload_len > 0 && !payload) return false;
    if (data_type == 0 || !pathByteLen(path_len, nullptr)) return false;

    int i = 0;
    _out_frame[i++] = RESP_CODE_CHANNEL_DATA_RECV;
    _out_frame[i++] = (uint8_t)snr_quarters;
    _out_frame[i++] = 0;
    _out_frame[i++] = 0;
    _out_frame[i++] = channel_index;
    _out_frame[i++] = path_len;
    _out_frame[i++] = (uint8_t)(data_type & 0xFF);
    _out_frame[i++] = (uint8_t)(data_type >> 8);
    _out_frame[i++] = (uint8_t)payload_len;
    if (payload_len > 0) {
        std::memcpy(&_out_frame[i], payload, payload_len);
        i += (int)payload_len;
    }

    bool added = addToOfflineQueue(0, false, _out_frame, (size_t)i);
    if (added && isConnected()) {
        uint8_t tickle = PUSH_CODE_MSG_WAITING;
        sendPushFrame(&tickle, 1);
    }
    return added;
}

bool CompanionBridge::notifySendConfirmed(uint32_t ack, uint32_t trip_time_ms)
{
    if (!_serial) return false;
    uint8_t frame[9];
    int i = 0;
    frame[i++] = PUSH_CODE_SEND_CONFIRMED;
    std::memcpy(&frame[i], &ack, 4);
    i += 4;
    std::memcpy(&frame[i], &trip_time_ms, 4);
    i += 4;
    return sendPushFrame(frame, i);
}

void CompanionBridge::onIdentityChanged()
{
    // Any iterator/request/signing state may contain contact or signature
    // material associated with the previous private key. Keep the transport
    // connected so the import command can return OK, but force subsequent
    // operations to start from a clean bridge session.
    _contact_iter = -1;
    _iter_filter_since = 0;
    _most_recent_lastmod = 0;
    _offline_len = 0;
    _sign_active = false;
    _sign_len = 0;
    clearPendingBinary();
}

void CompanionBridge::writeSentOrErr(const CompanionSendResult& r)
{
    if (!r.ok) {
        writeErrFrame(ERR_CODE_NOT_FOUND);
        return;
    }
    _out_frame[0] = RESP_CODE_SENT;
    _out_frame[1] = r.sent_flood ? 1 : 0;
    std::memcpy(&_out_frame[2], &r.expected_ack, 4);
    std::memcpy(&_out_frame[6], &r.est_timeout, 4);
    sendResponseFrame(_out_frame, 10);
}

bool CompanionBridge::pushAdvert(const CompanionContact& contact, bool is_new)
{
    if (!isConnected()) return false;
    if (is_new) {
        return writeContactFrame(PUSH_CODE_NEW_ADVERT, contact, false);
    } else {
        _out_frame[0] = PUSH_CODE_ADVERT;
        std::memcpy(&_out_frame[1], contact.pub_key, SIGURDOS_COMPANION_PUB_KEY_SIZE);
        return sendPushFrame(_out_frame,
                             1 + SIGURDOS_COMPANION_PUB_KEY_SIZE);
    }
}

bool CompanionBridge::pushPathUpdated(const CompanionContact& contact)
{
    if (!isConnected()) return false;
    _out_frame[0] = PUSH_CODE_PATH_UPDATED;
    std::memcpy(&_out_frame[1], contact.pub_key, SIGURDOS_COMPANION_PUB_KEY_SIZE);
    return sendPushFrame(_out_frame,
                         1 + SIGURDOS_COMPANION_PUB_KEY_SIZE);
}

bool CompanionBridge::pushPathDiscoveryResponse(
    const uint8_t* pubkey_prefix,
    const uint8_t* in_path, uint8_t in_path_len,
    const uint8_t* out_path, uint8_t out_path_len)
{
    if (!isConnected() || !pubkey_prefix ||
        !sigurdos::mesh::path::encodedLengthValid(in_path_len) ||
        !sigurdos::mesh::path::encodedLengthValid(out_path_len)) {
        return false;
    }

    const size_t in_bytes = sigurdos::mesh::path::byteCount(in_path_len);
    const size_t out_bytes = sigurdos::mesh::path::byteCount(out_path_len);
    if ((in_bytes > 0 && !in_path) || (out_bytes > 0 && !out_path) ||
        10 + in_bytes + out_bytes > MAX_FRAME_SIZE) {
        return false;
    }

    int i = 0;
    _out_frame[i++] = PUSH_CODE_PATH_DISCOVERY_RESPONSE;
    _out_frame[i++] = 0;  // reserved
    std::memcpy(&_out_frame[i], pubkey_prefix,
                SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE);
    i += SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE;
    _out_frame[i++] = out_path_len;
    if (out_bytes > 0) {
        std::memcpy(&_out_frame[i], out_path, out_bytes);
        i += (int)out_bytes;
    }
    _out_frame[i++] = in_path_len;
    if (in_bytes > 0) {
        std::memcpy(&_out_frame[i], in_path, in_bytes);
        i += (int)in_bytes;
    }
    return sendPushFrame(_out_frame, i);
}

bool CompanionBridge::pushContactDeleted(const uint8_t* pub_key)
{
    if (!isConnected() || !pub_key) return false;
    _out_frame[0] = PUSH_CODE_CONTACT_DELETED;
    std::memcpy(&_out_frame[1], pub_key, SIGURDOS_COMPANION_PUB_KEY_SIZE);
    return sendPushFrame(_out_frame,
                         1 + SIGURDOS_COMPANION_PUB_KEY_SIZE);
}

bool CompanionBridge::pushContactsFull()
{
    if (!isConnected()) return false;
    uint8_t b = PUSH_CODE_CONTACTS_FULL;
    return sendPushFrame(&b, 1);
}

bool CompanionBridge::pushLoginResult(const uint8_t* pubkey_prefix, bool success,
                                      uint8_t permission, uint32_t tag,
                                      uint8_t acl_permissions, uint8_t firmware_level,
                                      bool legacy_success)
{
    if (!_serial || !pubkey_prefix) return false;
    int i = 0;
    _out_frame[i++] = success ? PUSH_CODE_LOGIN_SUCCESS : PUSH_CODE_LOGIN_FAIL;
    _out_frame[i++] = success ? permission : 0;
    std::memcpy(&_out_frame[i], pubkey_prefix, SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE);
    i += SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE;
    // Current success fields come directly from the server response. Legacy
    // success and all failure frames end after the public-key prefix.
    if (success && !legacy_success) {
        std::memcpy(&_out_frame[i], &tag, 4);
        i += 4;
        _out_frame[i++] = acl_permissions;
        _out_frame[i++] = firmware_level;
    }
    return sendPushFrame(_out_frame, i);
}

bool CompanionBridge::pushStatusResponse(const uint8_t* pubkey_prefix,
                                         const uint8_t* blob, size_t blob_len)
{
    if (!_serial || !pubkey_prefix || (blob_len > 0 && !blob) ||
        blob_len > SIGURDOS_COMPANION_PUSH_BLOB_MAX_PAYLOAD) return false;
    int i = 0;
    _out_frame[i++] = PUSH_CODE_STATUS_RESPONSE;
    _out_frame[i++] = 0;  // reserved
    std::memcpy(&_out_frame[i], pubkey_prefix, SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE);
    i += SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE;
    if (blob_len > 0) {
        std::memcpy(&_out_frame[i], blob, blob_len);
        i += (int)blob_len;
    }
    return sendPushFrame(_out_frame, i);
}

bool CompanionBridge::pushTelemetryResponse(const uint8_t* pubkey_prefix,
                                            const uint8_t* blob, size_t blob_len)
{
    if (!_serial || !pubkey_prefix || (blob_len > 0 && !blob) ||
        blob_len > SIGURDOS_COMPANION_PUSH_BLOB_MAX_PAYLOAD) return false;
    int i = 0;
    _out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    _out_frame[i++] = 0;  // reserved
    std::memcpy(&_out_frame[i], pubkey_prefix, SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE);
    i += SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE;
    if (blob_len > 0) {
        std::memcpy(&_out_frame[i], blob, blob_len);
        i += (int)blob_len;
    }
    return sendPushFrame(_out_frame, i);
}

bool CompanionBridge::pushBinaryResponse(uint32_t tag,
                                         const uint8_t* blob, size_t blob_len)
{
    expirePendingBinary();
    const int pending = findPendingBinary(tag);
    if (!_serial || pending < 0 || (blob_len > 0 && !blob) ||
        6 + blob_len > MAX_FRAME_SIZE) {
        return false;
    }
    int i = 0;
    _out_frame[i++] = PUSH_CODE_BINARY_RESPONSE;
    _out_frame[i++] = 0;  // reserved
    std::memcpy(&_out_frame[i], &tag, sizeof(tag));
    i += sizeof(tag);
    if (blob_len > 0) {
        std::memcpy(&_out_frame[i], blob, blob_len);
        i += (int)blob_len;
    }
    const bool written = sendPushFrame(_out_frame, i);
    if (written) _pending_binary[pending] = {};
    return written;
}

bool CompanionBridge::pushRawData(int8_t snr_quarters, int8_t rssi,
                                  const uint8_t* payload, size_t payload_len)
{
    if (!isConnected() || (payload_len > 0 && !payload)) return false;
    if (payload_len > MAX_FRAME_SIZE - 4) return false;
    int i = 0;
    _out_frame[i++] = PUSH_CODE_RAW_DATA;
    _out_frame[i++] = (uint8_t)snr_quarters;
    _out_frame[i++] = (uint8_t)rssi;
    _out_frame[i++] = 0xFF;  // reserved by the stock companion protocol
    if (payload_len > 0) {
        std::memcpy(&_out_frame[i], payload, payload_len);
        i += (int)payload_len;
    }
    return sendPushFrame(_out_frame, i);
}

bool CompanionBridge::pushControlData(int8_t snr_quarters, int8_t rssi,
                                      uint8_t path_len,
                                      const uint8_t* payload, size_t payload_len)
{
    if (!isConnected() || (payload_len > 0 && !payload)) return false;
    if (payload_len > MAX_FRAME_SIZE - 4) return false;
    int i = 0;
    _out_frame[i++] = PUSH_CODE_CONTROL_DATA;
    _out_frame[i++] = (uint8_t)snr_quarters;
    _out_frame[i++] = (uint8_t)rssi;
    _out_frame[i++] = path_len;
    if (payload_len > 0) {
        std::memcpy(&_out_frame[i], payload, payload_len);
        i += (int)payload_len;
    }
    return sendPushFrame(_out_frame, i);
}

bool CompanionBridge::pushTraceData(uint32_t tag, uint32_t auth, uint8_t flags,
                                    const uint8_t* path_hashes, const uint8_t* path_snrs,
                                    uint8_t path_len, int8_t final_snr_quarters)
{
    if (!isConnected()) return false;
    uint8_t path_sz = flags & 0x03;
    const size_t hash_size = (size_t)1U << path_sz;
    if (path_len > sigurdos::mesh::path::MAX_ENCODED_PATH_BYTES ||
        path_len % hash_size != 0 ||
        (path_len > 0 && !path_hashes)) return false;
    size_t snr_count = (size_t)path_len / hash_size;
    if (snr_count > 0 && !path_snrs) return false;
    // [code][reserved][path_len][flags][tag:4][auth:4][hashes][snrs][final_snr]
    if (12 + (size_t)path_len + snr_count + 1 > MAX_FRAME_SIZE) return false;
    int i = 0;
    _out_frame[i++] = PUSH_CODE_TRACE_DATA;
    _out_frame[i++] = 0;  // reserved
    _out_frame[i++] = path_len;
    _out_frame[i++] = flags;
    std::memcpy(&_out_frame[i], &tag, 4); i += 4;
    std::memcpy(&_out_frame[i], &auth, 4); i += 4;
    if (path_len && path_hashes) { std::memcpy(&_out_frame[i], path_hashes, path_len); i += path_len; }
    if (snr_count && path_snrs) { std::memcpy(&_out_frame[i], path_snrs, snr_count); i += (int)snr_count; }
    _out_frame[i++] = (uint8_t)final_snr_quarters;
    return sendPushFrame(_out_frame, i);
}

bool CompanionBridge::handleFrame(const uint8_t* frame, size_t len)
{
    if (!_serial || !_host || !frame || len == 0 || len > MAX_FRAME_SIZE) return false;
    // One slot is reserved exclusively for the current command response. If
    // it is occupied, do not execute another command (especially a
    // non-idempotent one) until the prior response is transport-admitted.
    if (_pending_response_len != 0) return false;
    expirePendingBinary();
    std::memcpy(_cmd_frame, frame, len);
    _cmd_frame[len] = 0;

    const uint8_t cmd = _cmd_frame[0];
    const size_t minimum_length = commandMinimumLength(cmd);
    if (minimum_length != 0 && len < minimum_length) {
        writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        return true;
    }
    if (cmd == CMD_DEVICE_QUERY && len >= 2) {
        _app_target_ver = _cmd_frame[1];
        _version_negotiated = true;
        // Frames encoded before negotiation must never survive a version
        // change. Durable records remain in the message store and are rebuilt
        // on the next sync request.
        _offline_len = 0;
        int i = 0;
        _out_frame[i++] = RESP_CODE_DEVICE_INFO;
        _out_frame[i++] = SIGURDOS_COMPANION_FIRMWARE_VER_CODE;
        _out_frame[i++] = MAX_CONTACTS / 2;
        _out_frame[i++] = 8;   // MAX_GROUP_CHANNELS
        uint32_t pin = _host->blePin();
        std::memcpy(&_out_frame[i], &pin, 4);
        i += 4;
        std::memset(&_out_frame[i], 0, 12);
        strzcpy((char*)&_out_frame[i], BUILD_DATE, 12);
        i += 12;
        strzcpy((char*)&_out_frame[i], MANUFACTURER, 40);
        i += 40;
        strzcpy((char*)&_out_frame[i], FIRMWARE_VERSION, 20);
        i += 20;
        _out_frame[i++] = _host->clientRepeat();
        _out_frame[i++] = _host->pathHashMode();
        sendResponseFrame(_out_frame, i);
        return true;
    }

    if (cmd == CMD_APP_START && len >= 8) {
        _contact_iter = -1;
        CompanionSelfInfo si{};
        _host->selfInfo(si);
        int i = 0;
        _out_frame[i++] = RESP_CODE_SELF_INFO;
        _out_frame[i++] = si.advert_type;
        _out_frame[i++] = (uint8_t)si.tx_power_dbm;
        _out_frame[i++] = (uint8_t)si.max_tx_power_dbm;
        std::memcpy(&_out_frame[i], si.pub_key, SIGURDOS_COMPANION_PUB_KEY_SIZE);
        i += SIGURDOS_COMPANION_PUB_KEY_SIZE;
        std::memcpy(&_out_frame[i], &si.lat, 4);
        i += 4;
        std::memcpy(&_out_frame[i], &si.lon, 4);
        i += 4;
        _out_frame[i++] = si.multi_acks;
        _out_frame[i++] = si.advert_loc_policy;
        _out_frame[i++] = si.telemetry_modes;
        _out_frame[i++] = si.manual_add_contacts;
        std::memcpy(&_out_frame[i], &si.freq_khz, 4);
        i += 4;
        std::memcpy(&_out_frame[i], &si.bw_hz, 4);
        i += 4;
        _out_frame[i++] = si.sf;
        _out_frame[i++] = si.cr;
        size_t nlen = boundedTextLen(si.node_name, sizeof(si.node_name) - 1);
        if (i + nlen > MAX_FRAME_SIZE) nlen = MAX_FRAME_SIZE - i;
        std::memcpy(&_out_frame[i], si.node_name, nlen);
        i += (int)nlen;
        sendResponseFrame(_out_frame, i);
        refillOfflineQueueFromStore(true);
        return true;
    }

    if (cmd == CMD_GET_CONTACTS) {
        if (_contact_iter >= 0) {
            writeErrFrame(ERR_CODE_BAD_STATE);
            return true;
        }
        _iter_filter_since = 0;
        if (len >= 5) std::memcpy(&_iter_filter_since, &_cmd_frame[1], 4);
        uint32_t count = (uint32_t)_host->contactCount();
        _out_frame[0] = RESP_CODE_CONTACTS_START;
        std::memcpy(&_out_frame[1], &count, 4);
        sendResponseFrame(_out_frame, 5);
        _contact_iter = 0;
        _most_recent_lastmod = 0;
        return true;
    }

    if (cmd == CMD_SYNC_NEXT_MESSAGE) {
        _last_sync_time = _host->currentTime();
        if (!_version_negotiated) {
            writeErrFrame(ERR_CODE_BAD_STATE);
            return true;
        }
        if (_offline_len == 0) {
            // APP_START primes only one bounded page. Refill synchronously so
            // this same request returns the next persisted message instead of
            // prematurely reporting NO_MORE_MESSAGES at the page boundary.
            refillOfflineQueueFromStore(false);
        }
        uint32_t store_id = 0;
        bool persistent = false;
        int out_len = peekOfflineQueue(_out_frame, &store_id, &persistent);
        if (out_len > 0) {
            // Dequeue only after the transport accepts the complete frame.
            size_t written = _serial->writeFrame(_out_frame, out_len);
            if (written == (size_t)out_len) {
                if (!persistent ||
                    sigurdos::mesh::messageStoreMarkCompanionSent(store_id)) {
                    removeFirstOfflineFrame();
                }
            }
        } else {
            writeNoMoreMessages();
        }
        return true;
    }

    if (cmd == CMD_SEND_TXT_MSG && len >= 14) {
        int i = 1;
        uint8_t txt_type = _cmd_frame[i++];
        uint8_t attempt = _cmd_frame[i++];
        uint32_t timestamp = 0;
        std::memcpy(&timestamp, &_cmd_frame[i], 4);
        i += 4;
        const uint8_t* prefix = &_cmd_frame[i];
        i += SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE;
        const char* text = (const char*)&_cmd_frame[i];
        if (txt_type != COMPANION_TXT_PLAIN && txt_type != COMPANION_TXT_CLI_DATA) {
            writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
            return true;
        }
        CompanionSendResult result = _host->sendTextByPubKeyPrefix(
            prefix, SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE, txt_type, attempt, timestamp, text);
        if (!result.ok) {
            writeErrFrame(ERR_CODE_NOT_FOUND);
            return true;
        }
        _out_frame[0] = RESP_CODE_SENT;
        _out_frame[1] = result.sent_flood ? 1 : 0;
        std::memcpy(&_out_frame[2], &result.expected_ack, 4);
        std::memcpy(&_out_frame[6], &result.est_timeout, 4);
        sendResponseFrame(_out_frame, 10);
        return true;
    }

    if (cmd == CMD_SEND_CHANNEL_TXT_MSG && len >= 7) {
        int i = 1;
        uint8_t txt_type = _cmd_frame[i++];
        uint8_t channel_idx = _cmd_frame[i++];
        uint32_t timestamp = 0;
        std::memcpy(&timestamp, &_cmd_frame[i], 4);
        i += 4;
        if (txt_type != COMPANION_TXT_PLAIN) {
            writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
            return true;
        }
        CompanionSendResult result = _host->sendChannelText(channel_idx, timestamp,
                                                            (const char*)&_cmd_frame[i]);
        if (result.ok) writeOKFrame();
        else writeErrFrame(ERR_CODE_NOT_FOUND);
        return true;
    }

    if (cmd == CMD_SEND_CHANNEL_DATA) {
        if (len < 5) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }

        size_t i = 1;
        uint8_t channel_idx = _cmd_frame[i++];
        uint8_t path_len = _cmd_frame[i++];

        size_t path_bytes = 0;
        if (!pathByteLen(path_len, &path_bytes) || i + path_bytes + 2 > len) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }

        const uint8_t* path = nullptr;
        if (path_len != COMPANION_OUT_PATH_UNKNOWN) {
            path = &_cmd_frame[i];
            i += path_bytes;
        }

        uint16_t data_type = (uint16_t)_cmd_frame[i] | ((uint16_t)_cmd_frame[i + 1] << 8);
        i += 2;
        const uint8_t* payload = &_cmd_frame[i];
        size_t payload_len = len - i;

        if (data_type == 0 || payload_len > SIGURDOS_COMPANION_CHANNEL_DATA_MAX_PAYLOAD) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }

        if (_host->sendChannelData(channel_idx, path, path_len, data_type, payload, payload_len)) {
            writeOKFrame();
        } else {
            writeErrFrame(ERR_CODE_NOT_FOUND);
        }
        return true;
    }

    if (cmd == CMD_GET_DEVICE_TIME) {
        _out_frame[0] = RESP_CODE_CURR_TIME;
        uint32_t now = _host->currentTime();
        std::memcpy(&_out_frame[1], &now, 4);
        sendResponseFrame(_out_frame, 5);
        return true;
    }

    if (cmd == CMD_SET_DEVICE_TIME && len >= 5) {
        uint32_t secs = 0;
        std::memcpy(&secs, &_cmd_frame[1], 4);
        if (secs >= _host->currentTime() && _host->setCurrentTime(secs)) writeOKFrame();
        else writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        return true;
    }

    if (cmd == CMD_GET_BATT_AND_STORAGE) {
        int i = 0;
        _out_frame[i++] = RESP_CODE_BATT_AND_STORAGE;
        uint16_t mv = _host->batteryMilliVolts();
        uint32_t used = _host->storageUsedKb();
        uint32_t total = _host->storageTotalKb();
        std::memcpy(&_out_frame[i], &mv, 2); i += 2;
        std::memcpy(&_out_frame[i], &used, 4); i += 4;
        std::memcpy(&_out_frame[i], &total, 4); i += 4;
        sendResponseFrame(_out_frame, i);
        return true;
    }

    if (cmd == CMD_SET_ADVERT_NAME) {
        if (len < 2) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        if (_host->setAdvertName((const char*)&_cmd_frame[1])) writeOKFrame();
        else writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        return true;
    }

    if (cmd == CMD_SET_ADVERT_LATLON) {
        if (len < 9) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        int32_t lat = 0;
        int32_t lon = 0;
        std::memcpy(&lat, &_cmd_frame[1], 4);
        std::memcpy(&lon, &_cmd_frame[5], 4);
        if (_host->setAdvertLatLon(lat, lon)) writeOKFrame();
        else writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        return true;
    }

    if (cmd == CMD_SET_RADIO_PARAMS) {
        if (len < 11) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        int i = 1;
        uint32_t freq_khz = 0;
        uint32_t bw_hz = 0;
        std::memcpy(&freq_khz, &_cmd_frame[i], 4);
        i += 4;
        std::memcpy(&bw_hz, &_cmd_frame[i], 4);
        i += 4;
        uint8_t sf = _cmd_frame[i++];
        uint8_t cr = _cmd_frame[i++];
        uint8_t client_repeat = (len > (size_t)i) ? _cmd_frame[i] : 0;
        if (_host->setRadioParams(freq_khz, bw_hz, sf, cr, client_repeat)) writeOKFrame();
        else writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        return true;
    }

    if (cmd == CMD_SET_RADIO_TX_POWER) {
        if (len < 2) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        if (_host->setRadioTxPower((int8_t)_cmd_frame[1])) writeOKFrame();
        else writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        return true;
    }

    if (cmd == CMD_SET_TUNING_PARAMS) {
        if (len < 9) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        uint32_t rx_delay_base_x1000 = 0;
        uint32_t airtime_factor_x1000 = 0;
        std::memcpy(&rx_delay_base_x1000, &_cmd_frame[1], 4);
        std::memcpy(&airtime_factor_x1000, &_cmd_frame[5], 4);
        if (_host->setTuningParams(rx_delay_base_x1000, airtime_factor_x1000)) writeOKFrame();
        else writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        return true;
    }

    if (cmd == CMD_GET_TUNING_PARAMS) {
        uint32_t rx_delay_base_x1000 = 0;
        uint32_t airtime_factor_x1000 = 0;
        _host->tuningParams(rx_delay_base_x1000, airtime_factor_x1000);
        int i = 0;
        _out_frame[i++] = RESP_CODE_TUNING_PARAMS;
        std::memcpy(&_out_frame[i], &rx_delay_base_x1000, 4);
        i += 4;
        std::memcpy(&_out_frame[i], &airtime_factor_x1000, 4);
        i += 4;
        sendResponseFrame(_out_frame, i);
        return true;
    }

    if (cmd == CMD_SEND_SELF_ADVERT) {
        bool flood = len >= 2 && _cmd_frame[1] == 1;
        if (_host->sendAdvert(flood)) writeOKFrame();
        else writeErrFrame(ERR_CODE_TABLE_FULL);
        return true;
    }

    if (cmd == CMD_GET_CHANNEL && len >= 2) {
        uint8_t channel_idx = _cmd_frame[1];
        CompanionChannel ch{};
        if (!_host->getChannel(channel_idx, ch)) {
            writeErrFrame(ERR_CODE_NOT_FOUND);
            return true;
        }
        int i = 0;
        _out_frame[i++] = RESP_CODE_CHANNEL_INFO;
        _out_frame[i++] = channel_idx;
        strzcpy((char*)&_out_frame[i], ch.name, 32);
        i += 32;
        std::memcpy(&_out_frame[i], ch.secret, 16);
        i += 16;
        sendResponseFrame(_out_frame, i);
        return true;
    }

    if (cmd == CMD_SET_CHANNEL && len >= 2 + 32 + 32) {
        writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
        return true;
    }

    if (cmd == CMD_SET_CHANNEL && len >= 2 + 32 + 16) {
        uint8_t channel_idx = _cmd_frame[1];
        CompanionChannel ch{};
        strzcpy(ch.name, (const char*)&_cmd_frame[2], sizeof(ch.name));
        std::memcpy(ch.secret, &_cmd_frame[2 + 32], 16);
        if (_host->setChannel(channel_idx, ch)) writeOKFrame();
        else writeErrFrame(ERR_CODE_NOT_FOUND);
        return true;
    }

    if (cmd == CMD_SET_DEVICE_PIN && len >= 5) {
        uint32_t pin = 0;
        std::memcpy(&pin, &_cmd_frame[1], 4);
        if (companionBlePinValid(pin) && _host->setBlePin(pin)) writeOKFrame();
        else writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        return true;
    }

    if (cmd == CMD_EXPORT_PRIVATE_KEY) {
#if SIGURDOS_ENABLE_PRIVATE_KEY_EXPORT
        uint8_t key[64];
        if (!_host->exportPrivateKey(key)) {
            writeDisabledFrame();
            return true;
        }
        _out_frame[0] = RESP_CODE_PRIVATE_KEY;
        std::memcpy(&_out_frame[1], key, sizeof(key));
        sendResponseFrame(_out_frame, 1 + sizeof(key));
#else
        writeDisabledFrame();
#endif
        return true;
    }

    if (cmd == CMD_IMPORT_PRIVATE_KEY && len >= 65) {
#if SIGURDOS_ENABLE_PRIVATE_KEY_IMPORT
        if (_host->importPrivateKey(&_cmd_frame[1])) writeOKFrame();
        else writeErrFrame(ERR_CODE_ILLEGAL_ARG);
#else
        writeDisabledFrame();
#endif
        return true;
    }

    if (cmd == CMD_SET_OTHER_PARAMS && len >= 2) {
        CompanionOtherParams p{};
        p.manual_add_contacts = _cmd_frame[1];
        if (len >= 3) { p.telemetry_modes = _cmd_frame[2]; p.telemetry_present = true; }
        if (len >= 4) { p.advert_loc_policy = _cmd_frame[3]; p.loc_policy_present = true; }
        if (len >= 5) { p.multi_acks = _cmd_frame[4]; p.multi_acks_present = true; }
        _host->setOtherParams(p);
        writeOKFrame();
        return true;
    }

    if (cmd == CMD_SET_PATH_HASH_MODE && len >= 3 && _cmd_frame[1] == 0) {
        if (_cmd_frame[2] >= 3 || !_host->setPathHashMode(_cmd_frame[2])) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        } else {
            writeOKFrame();
        }
        return true;
    }

    if (cmd == CMD_SET_AUTOADD_CONFIG && len >= 2) {
        uint8_t max_hops = (len >= 3) ? _cmd_frame[2] : 0;
        if (max_hops > 64) max_hops = 64;
        _host->setAutoAddConfig(_cmd_frame[1], max_hops);
        writeOKFrame();
        return true;
    }

    if (cmd == CMD_GET_AUTOADD_CONFIG) {
        uint8_t cfg = 0, max_hops = 0;
        _host->getAutoAddConfig(&cfg, &max_hops);
        int i = 0;
        _out_frame[i++] = RESP_CODE_AUTOADD_CONFIG;
        _out_frame[i++] = cfg;
        _out_frame[i++] = max_hops;
        sendResponseFrame(_out_frame, i);
        return true;
    }

    // ── Contacts CRUD / connection ───────────────────────────
    if (cmd == CMD_GET_CONTACT_BY_KEY && len >= 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE) {
        CompanionContact c{};
        if (_host->getContactByPubKey(&_cmd_frame[1], c)) {
            writeContactFrame(RESP_CODE_CONTACT, c);
        } else {
            writeErrFrame(ERR_CODE_NOT_FOUND);
        }
        return true;
    }

    if (cmd == CMD_RESET_PATH && len >= 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE) {
        if (_host->resetPathByPubKey(&_cmd_frame[1])) writeOKFrame();
        else writeErrFrame(ERR_CODE_NOT_FOUND);
        return true;
    }

    if (cmd == CMD_REMOVE_CONTACT && len >= 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE) {
        if (_host->removeContactByPubKey(&_cmd_frame[1])) writeOKFrame();
        else writeErrFrame(ERR_CODE_NOT_FOUND);
        return true;
    }

    if (cmd == CMD_SHARE_CONTACT && len >= 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE) {
        if (_host->shareContactByPubKey(&_cmd_frame[1])) writeOKFrame();
        else writeErrFrame(ERR_CODE_NOT_FOUND);
        return true;
    }

    if (cmd == CMD_ADD_UPDATE_CONTACT &&
        len >= 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE + 2 + 1 + SIGURDOS_COMPANION_PATH_SIZE + 32 + 4) {
        CompanionContact c{};
        size_t i = 1;
        std::memcpy(c.pub_key, &_cmd_frame[i], SIGURDOS_COMPANION_PUB_KEY_SIZE);
        i += SIGURDOS_COMPANION_PUB_KEY_SIZE;
        c.type = _cmd_frame[i++];
        c.flags = _cmd_frame[i++];
        c.out_path_len = _cmd_frame[i++];
        if (!sigurdos::mesh::path::storedLengthValid(c.out_path_len)) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        std::memcpy(c.out_path, &_cmd_frame[i], SIGURDOS_COMPANION_PATH_SIZE);
        i += SIGURDOS_COMPANION_PATH_SIZE;
        std::memcpy(c.name, &_cmd_frame[i], 32);
        c.name[sizeof(c.name) - 1] = '\0';
        i += 32;
        std::memcpy(&c.last_advert_timestamp, &_cmd_frame[i], 4);
        i += 4;
        c.lastmod = _host->currentTime();  // fallback if frame omits it
        if (len >= i + 8) {
            std::memcpy(&c.gps_lat, &_cmd_frame[i], 4); i += 4;
            std::memcpy(&c.gps_lon, &_cmd_frame[i], 4); i += 4;
            if (len >= i + 4) std::memcpy(&c.lastmod, &_cmd_frame[i], 4);
        }
        if (_host->addOrUpdateContact(c)) writeOKFrame();
        else writeErrFrame(ERR_CODE_TABLE_FULL);
        return true;
    }

    if (cmd == CMD_EXPORT_CONTACT) {
        const uint8_t* pub_key =
            (len >= 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE) ? &_cmd_frame[1] : nullptr;
        int out_len = _host->exportContactByPubKey(pub_key, &_out_frame[1], MAX_FRAME_SIZE - 1);
        if (out_len > 0) {
            _out_frame[0] = RESP_CODE_EXPORT_CONTACT;
            sendResponseFrame(_out_frame, (size_t)out_len + 1);
        } else {
            writeErrFrame(pub_key ? ERR_CODE_NOT_FOUND : ERR_CODE_TABLE_FULL);
        }
        return true;
    }

    if (cmd == CMD_IMPORT_CONTACT && len > 2 + 32 + 64) {
        if (_host->importContact(&_cmd_frame[1], len - 1)) writeOKFrame();
        else writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        return true;
    }

    if (cmd == CMD_HAS_CONNECTION && len >= 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE) {
        if (_host->hasConnectionTo(&_cmd_frame[1])) writeOKFrame();
        else writeErrFrame(ERR_CODE_NOT_FOUND);
        return true;
    }

    if (cmd == CMD_LOGOUT && len >= 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE) {
        _host->logout(&_cmd_frame[1]);
        writeOKFrame();
        return true;
    }

    // ── System ───────────────────────────────────────────────
    if (cmd == CMD_REBOOT && len >= 7 && std::memcmp(&_cmd_frame[1], "reboot", 6) == 0) {
        _host->reboot();  // does not return on device
        return true;
    }

    if (cmd == CMD_FACTORY_RESET && len >= 6 && std::memcmp(&_cmd_frame[1], "reset", 5) == 0) {
        if (_serial) _serial->disable();  // phone disconnects; prevents reconnect mid-wipe
        if (_host->factoryReset()) {       // reboots on device; returns on native
            writeOKFrame();
        } else {
            writeErrFrame(ERR_CODE_FILE_IO_ERROR);
        }
        return true;
    }

    // ── Stats ────────────────────────────────────────────────
    if (cmd == CMD_GET_STATS && len >= 2) {
        uint8_t stats_type = _cmd_frame[1];
        int i = 0;
        if (stats_type == STATS_TYPE_CORE) {
            CompanionCoreStats s{};
            _host->coreStats(s);
            _out_frame[i++] = RESP_CODE_STATS;
            _out_frame[i++] = STATS_TYPE_CORE;
            std::memcpy(&_out_frame[i], &s.batt_mv, 2); i += 2;
            std::memcpy(&_out_frame[i], &s.uptime_secs, 4); i += 4;
            std::memcpy(&_out_frame[i], &s.err_flags, 2); i += 2;
            _out_frame[i++] = s.queue_len;
            sendResponseFrame(_out_frame, i);
        } else if (stats_type == STATS_TYPE_RADIO) {
            CompanionRadioStats s{};
            _host->radioStats(s);
            _out_frame[i++] = RESP_CODE_STATS;
            _out_frame[i++] = STATS_TYPE_RADIO;
            std::memcpy(&_out_frame[i], &s.noise_floor, 2); i += 2;
            _out_frame[i++] = (uint8_t)s.last_rssi;
            _out_frame[i++] = (uint8_t)s.last_snr_quarters;
            std::memcpy(&_out_frame[i], &s.tx_air_secs, 4); i += 4;
            std::memcpy(&_out_frame[i], &s.rx_air_secs, 4); i += 4;
            sendResponseFrame(_out_frame, i);
        } else if (stats_type == STATS_TYPE_PACKETS) {
            CompanionPacketStats s{};
            _host->packetStats(s);
            _out_frame[i++] = RESP_CODE_STATS;
            _out_frame[i++] = STATS_TYPE_PACKETS;
            std::memcpy(&_out_frame[i], &s.recv, 4); i += 4;
            std::memcpy(&_out_frame[i], &s.sent, 4); i += 4;
            std::memcpy(&_out_frame[i], &s.sent_flood, 4); i += 4;
            std::memcpy(&_out_frame[i], &s.sent_direct, 4); i += 4;
            std::memcpy(&_out_frame[i], &s.recv_flood, 4); i += 4;
            std::memcpy(&_out_frame[i], &s.recv_direct, 4); i += 4;
            std::memcpy(&_out_frame[i], &s.recv_errors, 4); i += 4;
            sendResponseFrame(_out_frame, i);
        } else {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        }
        return true;
    }

    if (cmd == CMD_GET_ALLOWED_REPEAT_FREQ) {
        uint32_t pairs[16];
        size_t n = _host->allowedRepeatFreqRanges(pairs, 8);
        int i = 0;
        _out_frame[i++] = RESP_ALLOWED_REPEAT_FREQ;
        for (size_t k = 0; k < n * 2 && i + 4 <= MAX_FRAME_SIZE; k++) {
            std::memcpy(&_out_frame[i], &pairs[k], 4); i += 4;
        }
        sendResponseFrame(_out_frame, i);
        return true;
    }

    if (cmd == CMD_GET_CUSTOM_VARS) {
        int n = _host->getCustomVars((char*)_out_frame + 1, MAX_FRAME_SIZE - 1);
        _out_frame[0] = RESP_CODE_CUSTOM_VARS;
        sendResponseFrame(_out_frame, (size_t)(1 + ((n > 0) ? n : 0)));
        return true;
    }

    if (cmd == CMD_SET_CUSTOM_VAR && len >= 4) {
        char* varname = (char*)&_cmd_frame[1];
        char* separator = std::strchr(varname, ':');
        if (!separator) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        *separator = '\0';
        const char* value = separator + 1;
        if (_host->setCustomVar(varname, value)) {
            writeOKFrame();
        } else {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        }
        return true;
    }

    if (cmd == CMD_GET_ADVERT_PATH && len >= 2 + SIGURDOS_COMPANION_PUB_KEY_SIZE) {
        // Look up stored advert path for this contact's pubkey
        uint8_t path_buf[SIGURDOS_COMPANION_PATH_SIZE]{};
        uint8_t path_descriptor = 0;
        size_t path_bytes = 0;
        uint32_t timestamp = 0;
        if (!_host->getAdvertPath(&_cmd_frame[2], path_buf, sizeof(path_buf),
                                  &path_descriptor, &path_bytes, &timestamp) ||
            !sigurdos::mesh::path::encodedLengthValid(path_descriptor) ||
            path_bytes != sigurdos::mesh::path::byteCount(path_descriptor) ||
            path_bytes > sizeof(path_buf) ||
            1 + sizeof(timestamp) + 1 + path_bytes > MAX_FRAME_SIZE) {
            writeErrFrame(ERR_CODE_NOT_FOUND);
            return true;
        }
        int i = 0;
        _out_frame[i++] = RESP_CODE_ADVERT_PATH;
        std::memcpy(&_out_frame[i], &timestamp, 4);
        i += 4;
        _out_frame[i++] = path_descriptor;
        if (path_bytes > 0) {
            std::memcpy(&_out_frame[i], path_buf, path_bytes);
            i += (int)path_bytes;
        }
        sendResponseFrame(_out_frame, i);
        return true;
    }

    if (cmd == CMD_SEND_PATH_DISCOVERY_REQ &&
        len >= 2 + SIGURDOS_COMPANION_PUB_KEY_SIZE && _cmd_frame[1] == 0) {
        // Initiate path discovery for a contact — sends a flood telemetry request
        // Results arrive later via onContactPathRecv and are pushed as
        // PUSH_CODE_PATH_DISCOVERY_RESPONSE.
        CompanionSendResult r = _host->sendPathDiscovery(&_cmd_frame[2]);
        writeSentOrErr(r);
        return true;
    }

    // ── Flood scope (companion regions) ──────────────────────
    if (cmd == CMD_GET_DEFAULT_FLOOD_SCOPE) {
        char name[31];
        uint8_t key[16];
        _out_frame[0] = RESP_CODE_DEFAULT_FLOOD_SCOPE;
        if (_host->getDefaultFloodScope(name, key)) {
            std::memcpy(&_out_frame[1], name, 31);
            std::memcpy(&_out_frame[1 + 31], key, 16);
            sendResponseFrame(_out_frame, 1 + 31 + 16);
        } else {
            sendResponseFrame(_out_frame, 1);  // null scope
        }
        return true;
    }

    if (cmd == CMD_SET_DEFAULT_FLOOD_SCOPE) {
        if (len == 1 + 31 + 16) {
            char name[32];
            std::memcpy(name, &_cmd_frame[1], 31);
            name[31] = '\0';
            size_t nlen = strnlen(name, 31);
            if (nlen > 0 && nlen < 31) {
                if (_host->setDefaultFloodScope(name, &_cmd_frame[1 + 31])) {
                    writeOKFrame();
                } else {
                    writeErrFrame(ERR_CODE_FILE_IO_ERROR);
                }
            } else {
                writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            }
        } else if (len == 1) {
            if (_host->setDefaultFloodScope(nullptr, nullptr)) writeOKFrame();
            else writeErrFrame(ERR_CODE_FILE_IO_ERROR);
        } else {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        }
        return true;
    }

    if (cmd == CMD_SET_FLOOD_SCOPE_KEY) {
        if (len == 2 && _cmd_frame[1] == 1) {
            if (_host->setFloodScopeOverride(nullptr, true)) writeOKFrame();
            else writeErrFrame(ERR_CODE_FILE_IO_ERROR);
        } else if (len >= 2 && _cmd_frame[1] == 0 &&
                   (len == 2 || len == 2 + 16)) {
            const uint8_t* key = len == 2 + 16 ? &_cmd_frame[2] : nullptr;
            if (_host->setFloodScopeOverride(key, false)) writeOKFrame();
            else writeErrFrame(ERR_CODE_FILE_IO_ERROR);
        } else {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        }
        return true;
    }

    // ── Message signing ──────────────────────────────────────
    if (cmd == CMD_SIGN_START) {
        _sign_active = true;
        _sign_len = 0;
        int i = 0;
        _out_frame[i++] = RESP_CODE_SIGN_START;
        _out_frame[i++] = 0;  // reserved
        uint32_t maxlen = SIGURDOS_COMPANION_MAX_SIGN_DATA;
        std::memcpy(&_out_frame[i], &maxlen, 4); i += 4;
        sendResponseFrame(_out_frame, i);
        return true;
    }

    if (cmd == CMD_SIGN_DATA && len > 1) {
        if (!_sign_active) {
            writeErrFrame(ERR_CODE_BAD_STATE);
        } else if (_sign_len + (len - 1) > SIGURDOS_COMPANION_MAX_SIGN_DATA) {
            writeErrFrame(ERR_CODE_TABLE_FULL);
        } else {
            std::memcpy(&_sign_buf[_sign_len], &_cmd_frame[1], len - 1);
            _sign_len += (len - 1);
            writeOKFrame();
        }
        return true;
    }

    if (cmd == CMD_SIGN_FINISH) {
        if (!_sign_active) {
            writeErrFrame(ERR_CODE_BAD_STATE);
            return true;
        }
        int sig_len = _host->signData(_sign_buf, _sign_len, &_out_frame[1]);
        _sign_active = false;
        _sign_len = 0;
        if (sig_len > 0) {
            _out_frame[0] = RESP_CODE_SIGNATURE;
            sendResponseFrame(_out_frame, 1 + (size_t)sig_len);
        } else {
            writeErrFrame(ERR_CODE_BAD_STATE);
        }
        return true;
    }

    // ── Async requests (result arrives later via a PUSH_CODE_*) ──
    if (cmd == CMD_SEND_LOGIN && len >= 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE) {
        _cmd_frame[len] = 0;  // null-terminate password
        const char* password = (const char*)&_cmd_frame[1 + SIGURDOS_COMPANION_PUB_KEY_SIZE];
        CompanionSendResult r = _host->sendLogin(&_cmd_frame[1], password);
        writeSentOrErr(r);
        return true;
    }

    if (cmd == CMD_SEND_STATUS_REQ && len >= 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE) {
        CompanionSendResult r = _host->sendStatusReq(&_cmd_frame[1]);
        writeSentOrErr(r);
        return true;
    }

    if (cmd == CMD_SEND_TELEMETRY_REQ && len == 4) {
        // Self telemetry — answered immediately with a PUSH_CODE_TELEMETRY_RESPONSE.
        uint8_t blob[MAX_FRAME_SIZE];
        size_t blob_len = 0;
        _host->selfTelemetry(blob, &blob_len);
        uint8_t prefix[SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE]{};
        CompanionSelfInfo si{};
        _host->selfInfo(si);
        std::memcpy(prefix, si.pub_key, SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE);
        pushTelemetryResponse(prefix, blob, blob_len);
        return true;
    }

    if (cmd == CMD_SEND_TELEMETRY_REQ && len >= 4 + SIGURDOS_COMPANION_PUB_KEY_SIZE) {
        CompanionSendResult r = _host->sendTelemetryReq(&_cmd_frame[4]);
        writeSentOrErr(r);
        return true;
    }

    if (cmd == CMD_SEND_BINARY_REQ) {
        if (len < 2 + SIGURDOS_COMPANION_PUB_KEY_SIZE) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        CompanionContact contact{};
        const uint8_t* pub_key = &_cmd_frame[1];
        if (!_host->getContactByPubKey(pub_key, contact)) {
            writeErrFrame(ERR_CODE_NOT_FOUND);
            return true;
        }
        const int pending = findFreePendingBinary();
        if (pending < 0) {
            writeErrFrame(ERR_CODE_TABLE_FULL);
            return true;
        }
        const uint8_t* request = &_cmd_frame[1 + SIGURDOS_COMPANION_PUB_KEY_SIZE];
        const uint8_t request_len =
            (uint8_t)(len - (1 + SIGURDOS_COMPANION_PUB_KEY_SIZE));
        CompanionSendResult r = _host->sendBinaryReq(pub_key, request, request_len);
        if (!r.ok || r.expected_ack == 0) {
            writeErrFrame(ERR_CODE_TABLE_FULL);
            return true;
        }
        _pending_binary[pending].tag = r.expected_ack;
        _pending_binary[pending].deadline_ms = _host->monotonicMillis() +
            (r.est_timeout ? r.est_timeout : BINARY_REQUEST_FALLBACK_TIMEOUT_MS);
        writeSentOrErr(r);
        return true;
    }

    if (cmd == CMD_SEND_ANON_REQ) {
        if (len <= 1 + SIGURDOS_COMPANION_PUB_KEY_SIZE) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        const int pending = findFreePendingBinary();
        if (pending < 0) {
            writeErrFrame(ERR_CODE_TABLE_FULL);
            return true;
        }
        const uint8_t* pub_key = &_cmd_frame[1];
        const uint8_t* request = &_cmd_frame[1 + SIGURDOS_COMPANION_PUB_KEY_SIZE];
        const uint8_t request_len =
            (uint8_t)(len - (1 + SIGURDOS_COMPANION_PUB_KEY_SIZE));
        CompanionSendResult r = _host->sendAnonReq(pub_key, request, request_len);
        if (!r.ok || r.expected_ack == 0) {
            writeErrFrame(ERR_CODE_TABLE_FULL);
            return true;
        }
        _pending_binary[pending].tag = r.expected_ack;
        _pending_binary[pending].deadline_ms = _host->monotonicMillis() +
            (r.est_timeout ? r.est_timeout : BINARY_REQUEST_FALLBACK_TIMEOUT_MS);
        writeSentOrErr(r);
        return true;
    }

    if (cmd == CMD_SEND_TRACE_PATH && len > 10) {
        uint8_t path_len = (uint8_t)(len - 10);
        uint8_t flags = _cmd_frame[9];
        uint8_t path_sz = flags & 0x03;
        if ((path_len >> path_sz) > SIGURDOS_COMPANION_PATH_SIZE ||
            (path_len % (1 << path_sz)) != 0) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        uint32_t tag = 0, auth = 0;
        std::memcpy(&tag, &_cmd_frame[1], 4);
        std::memcpy(&auth, &_cmd_frame[5], 4);
        CompanionSendResult r = _host->sendTracePath(tag, auth, flags, &_cmd_frame[10], path_len);
        if (r.ok) {
            _out_frame[0] = RESP_CODE_SENT;
            _out_frame[1] = 0;
            std::memcpy(&_out_frame[2], &tag, 4);
            std::memcpy(&_out_frame[6], &r.est_timeout, 4);
            sendResponseFrame(_out_frame, 10);
        } else {
            writeErrFrame(ERR_CODE_TABLE_FULL);
        }
        return true;
    }

    if (cmd == CMD_SEND_RAW_DATA) {
        // Stock frame: [cmd][direct path length][path bytes][payload].  This
        // command predates multi-byte path hashes, so only the unambiguous
        // one-byte-hop encoding (0..63) is accepted.  0xFF is the flood
        // sentinel and raw-data flooding is deliberately unsupported.
        if (len < 6) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        const uint8_t path_len = _cmd_frame[1];
        if (path_len == COMPANION_OUT_PATH_UNKNOWN) {
            writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
            return true;
        }
        if (path_len > 63 || 2u + (size_t)path_len + 4u > len) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        const uint8_t* path = &_cmd_frame[2];
        const uint8_t* payload = path + path_len;
        const size_t payload_len = len - 2u - (size_t)path_len;
        if (_host->sendRawData(path, path_len, payload, payload_len)) {
            writeOKFrame();
        } else {
            writeErrFrame(ERR_CODE_TABLE_FULL);
        }
        return true;
    }

    if (cmd == CMD_SEND_CONTROL_DATA) {
        // Control frames are deliberately constrained to the zero-hop subset
        // recognized by MeshCore. The high bit identifies that safe subset.
        if (len < 2 || (_cmd_frame[1] & 0x80) == 0) {
            writeErrFrame(ERR_CODE_ILLEGAL_ARG);
            return true;
        }
        if (_host->sendControlData(&_cmd_frame[1], len - 1)) {
            writeOKFrame();
        } else {
            writeErrFrame(ERR_CODE_TABLE_FULL);
        }
        return true;
    }

    // ── Stub handlers for upstream commands not yet implemented ──
    // These are recognized command IDs but return unsupported error until
    // full implementations and security review are added. Recognition is not
    // advertised as feature parity; see docs/COMPANION_SUPPORT.md.
    if (cmd == CMD_SEND_BINARY_REQ) {
        writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); return true;
    }
    if (cmd == CMD_SEND_ANON_REQ) {
        writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); return true;
    }
    if (cmd == CMD_SEND_RAW_PACKET) {
        writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); return true;
    }

    // Reaching the fallback with a command that has a defined frame shape
    // means its length passed preflight but another required field was
    // malformed (for example, a non-zero reserved byte).
    writeErrFrame(commandMinimumLength(cmd) != 0
        ? ERR_CODE_ILLEGAL_ARG
        : ERR_CODE_UNSUPPORTED_CMD);
    return true;
}

} // namespace comms
} // namespace sigurdos
