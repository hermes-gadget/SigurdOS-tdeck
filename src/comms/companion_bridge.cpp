// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "companion_bridge.h"

#include <cstdlib>
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
    uint8_t hash_size = (path_len >> 6) + 1;
    if (hash_size == 4) return false;
    size_t n = (size_t)(path_len & 63) * (size_t)hash_size;
    if (n > SIGURDOS_COMPANION_PATH_SIZE) return false;
    if (out_len) *out_len = n;
    return true;
}

} // namespace

void CompanionBridge::begin(BaseSerialInterface* serial, CompanionBridgeHost* host)
{
    _serial = serial;
    _host = host;
    _app_target_ver = 3;
    _last_sync_time = 0;
    _contact_iter = -1;
    _offline_len = 0;
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
    else _serial->disable();
    return _serial->isEnabled() == enabled;
}

void CompanionBridge::loop()
{
    if (!_serial || !_host || !_serial->isEnabled()) return;
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
        _serial->writeFrame(_out_frame, i);
        _contact_iter = -1;
    }
}

void CompanionBridge::writeOKFrame()
{
    uint8_t b = RESP_CODE_OK;
    if (_serial) _serial->writeFrame(&b, 1);
}

void CompanionBridge::writeErrFrame(uint8_t err)
{
    uint8_t b[2] = { RESP_CODE_ERR, err };
    if (_serial) _serial->writeFrame(b, 2);
}

void CompanionBridge::writeDisabledFrame()
{
    uint8_t b = RESP_CODE_DISABLED;
    if (_serial) _serial->writeFrame(&b, 1);
}

void CompanionBridge::writeContactFrame(uint8_t code, const CompanionContact& contact)
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
    if (_serial) _serial->writeFrame(_out_frame, i);
}

void CompanionBridge::writeNoMoreMessages()
{
    uint8_t b = RESP_CODE_NO_MORE_MESSAGES;
    if (_serial) _serial->writeFrame(&b, 1);
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

bool CompanionBridge::addToOfflineQueue(const uint8_t* frame, size_t len)
{
    if (!frame || len == 0 || len > MAX_FRAME_SIZE) return false;
    if (offlineFrameExists(frame, len)) return false;
    if (_offline_len >= OFFLINE_QUEUE_SIZE) {
        for (int i = 1; i < _offline_len; i++) _offline[i - 1] = _offline[i];
        _offline_len--;
    }
    _offline[_offline_len].len = (uint8_t)len;
    std::memcpy(_offline[_offline_len].buf, frame, len);
    _offline_len++;
    return true;
}

int CompanionBridge::getFromOfflineQueue(uint8_t* frame)
{
    if (!frame || _offline_len <= 0) return 0;
    int len = _offline[0].len;
    std::memcpy(frame, _offline[0].buf, len);
    for (int i = 1; i < _offline_len; i++) _offline[i - 1] = _offline[i];
    _offline_len--;
    return len;
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
        out[i++] = COMPANION_TXT_PLAIN;
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
        out[i++] = COMPANION_TXT_PLAIN;
        std::memcpy(&out[i], &msg.timestamp, 4);
        i += 4;
    }

    size_t tlen = boundedTextLen(msg.text, sigurdos::mesh::SIGURDOS_MSG_TEXT_LEN - 1);
    if (i + tlen > MAX_FRAME_SIZE) tlen = MAX_FRAME_SIZE - i;
    std::memcpy(&out[i], msg.text, tlen);
    i += (int)tlen;
    *out_len = (size_t)i;
    return true;
}

void CompanionBridge::seedOfflineQueueFromStore(uint32_t since)
{
    // Heap-allocate the snapshot rather than using a static array — keeping it
    // off the tight internal dram0_0_seg .bss region. Freed before returning.
    sigurdos::mesh::StoredMessage* recent = (sigurdos::mesh::StoredMessage*)
        std::malloc(sizeof(sigurdos::mesh::StoredMessage) * OFFLINE_QUEUE_SIZE);
    if (!recent) return;
    int n = sigurdos::mesh::messageStoreLoadRecent(nullptr, recent, OFFLINE_QUEUE_SIZE);
    bool added_any = false;
    for (int idx = 0; idx < n; idx++) {
        if (since != 0 && recent[idx].timestamp <= since) continue;
        // The offline queue is a mirror of *incoming* messages only. Never feed
        // the app a self/outgoing message: it already has the ones it sent (it
        // got RESP_CODE_SENT), and the companion protocol has no
        // device-originated-send frame — echoing one back arrives as a bogus
        // *incoming* message (mis-attributed sender, duplicate bubble).
        if (recent[idx].is_self) continue;
        uint8_t frame[MAX_FRAME_SIZE];
        size_t len = 0;
        if (buildMessageFrame(recent[idx], frame, &len) && addToOfflineQueue(frame, len)) {
            added_any = true;
        }
    }
    std::free(recent);
    if (added_any && isConnected()) {
        uint8_t tickle = PUSH_CODE_MSG_WAITING;
        _serial->writeFrame(&tickle, 1);
    }
}

bool CompanionBridge::enqueueMessage(const sigurdos::mesh::StoredMessage& msg)
{
    // Only incoming messages are mirrored to the app (see seedOfflineQueueFromStore).
    if (msg.is_self) return false;
    uint8_t frame[MAX_FRAME_SIZE];
    size_t len = 0;
    if (!buildMessageFrame(msg, frame, &len)) return false;
    bool added = addToOfflineQueue(frame, len);
    if (added && isConnected()) {
        uint8_t tickle = PUSH_CODE_MSG_WAITING;
        _serial->writeFrame(&tickle, 1);
    }
    return added;
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

    bool added = addToOfflineQueue(_out_frame, (size_t)i);
    if (added && isConnected()) {
        uint8_t tickle = PUSH_CODE_MSG_WAITING;
        _serial->writeFrame(&tickle, 1);
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
    return _serial->writeFrame(frame, i) == (size_t)i;
}

bool CompanionBridge::handleFrame(const uint8_t* frame, size_t len)
{
    if (!_serial || !_host || !frame || len == 0 || len > MAX_FRAME_SIZE) return false;
    std::memcpy(_cmd_frame, frame, len);
    _cmd_frame[len] = 0;

    const uint8_t cmd = _cmd_frame[0];
    if (cmd == CMD_DEVICE_QUERY && len >= 2) {
        _app_target_ver = _cmd_frame[1];
        int i = 0;
        _out_frame[i++] = RESP_CODE_DEVICE_INFO;
        _out_frame[i++] = SIGURDOS_COMPANION_FIRMWARE_VER_CODE;
        _out_frame[i++] = 32;  // MAX_CONTACTS / 2 with current MAX_CONTACTS=64
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
        _serial->writeFrame(_out_frame, i);
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
        _out_frame[i++] = si.multi_acks ? 1 : 0;
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
        _serial->writeFrame(_out_frame, i);
        seedOfflineQueueFromStore(_last_sync_time);
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
        _serial->writeFrame(_out_frame, 5);
        _contact_iter = 0;
        _most_recent_lastmod = 0;
        return true;
    }

    if (cmd == CMD_SYNC_NEXT_MESSAGE) {
        int out_len = getFromOfflineQueue(_out_frame);
        if (out_len > 0) {
            _last_sync_time = _host->currentTime();
            _serial->writeFrame(_out_frame, out_len);
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
        _serial->writeFrame(_out_frame, 10);
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
        _serial->writeFrame(_out_frame, 5);
        return true;
    }

    if (cmd == CMD_SET_DEVICE_TIME && len >= 5) {
        uint32_t secs = 0;
        std::memcpy(&secs, &_cmd_frame[1], 4);
        if (_host->setCurrentTime(secs)) writeOKFrame();
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
        _serial->writeFrame(_out_frame, i);
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
        _serial->writeFrame(_out_frame, i);
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
        if (_host->setBlePin(pin)) writeOKFrame();
        else writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        return true;
    }

    if (cmd == CMD_EXPORT_PRIVATE_KEY) {
        uint8_t key[64];
        if (!_host->exportPrivateKey(key)) {
            writeDisabledFrame();
            return true;
        }
        _out_frame[0] = RESP_CODE_PRIVATE_KEY;
        std::memcpy(&_out_frame[1], key, sizeof(key));
        _serial->writeFrame(_out_frame, 1 + sizeof(key));
        return true;
    }

    if (cmd == CMD_IMPORT_PRIVATE_KEY && len >= 65) {
        if (_host->importPrivateKey(&_cmd_frame[1])) writeOKFrame();
        else writeErrFrame(ERR_CODE_ILLEGAL_ARG);
        return true;
    }

    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
    return true;
}

} // namespace comms
} // namespace sigurdos
