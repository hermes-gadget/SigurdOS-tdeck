// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Companion (phone app) bridge adapter — hosts the BLE/USB companion
// protocol against the mesh wrapper. This used to be companion_adapter.inc,
// textually #included into mesh_wrapper.cpp to reach its file-scope statics;
// it now reaches wrapper internals only through mesh_wrapper_internal.h
// (ARCH-001, #820).

#include "companion_adapter.h"
#include "companion_message_policy.h"
#include "mesh_wrapper.h"
#include "mesh_wrapper_internal.h"
#include "scope_key_hex.h"
#include "message_store.h"
#include "regions.h"
#include "sigurd_mesh_v2.h"
#include "advert_blob.h"
#include "comms/companion_bridge.h"
#include "comms/observed_ble_interface.h"
#include "hal/tdeck_pins.h"
#include "hal/prefs.h"
#include "hal/radio_profiles.h"
#include "hal/gps.h"
#include "hal/battery.h"
#include "diagnostics/log.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <new>
#include <esp_heap_caps.h>
#include <esp_random.h>

// REQ_TYPE constants not defined in core BaseChatMesh.h (only in examples)
#ifndef REQ_TYPE_GET_TELEMETRY_DATA
#define REQ_TYPE_GET_TELEMETRY_DATA  0x03
#endif

using sigurdos::mesh::SigurdMeshV2;
using sigurdos::mesh::meshOwnName;
using sigurdos::mesh::meshRtcTime;
using sigurdos::mesh::meshRtcTimeUnique;
using sigurdos::mesh::meshRadioTxAllowed;
using sigurdos::mesh::meshQueuePushOutgoing;
using sigurdos::mesh::meshStoreOutgoingMessage;
using sigurdos::mesh::meshSaveSelfIdentity;
using sigurdos::mesh::formatDmConversation;

// Local shorthand for the wrapper-owned mesh instance (was the file-scope
// static g_mesh when this code lived inside mesh_wrapper.cpp).
static inline SigurdMeshV2* mesh_ptr() { return sigurdos::mesh::meshInstance(); }

using sigurdos::comms::CompanionBridge;
using sigurdos::comms::CompanionBridgeHost;
using sigurdos::comms::CompanionChannel;
using sigurdos::comms::CompanionContact;
using sigurdos::comms::CompanionSelfInfo;
using sigurdos::comms::CompanionSendResult;
using sigurdos::comms::CompanionOtherParams;
using sigurdos::comms::CompanionCoreStats;
using sigurdos::comms::CompanionRadioStats;
using sigurdos::comms::CompanionPacketStats;

static_assert(sigurdos::mesh::COMPANION_TEXT_PLAIN ==
                  sigurdos::comms::COMPANION_TXT_PLAIN,
              "plain-text companion type must match the wire protocol");
static_assert(sigurdos::mesh::COMPANION_TEXT_CLI_DATA ==
                  sigurdos::comms::COMPANION_TXT_CLI_DATA,
              "CLI-data companion type must match the wire protocol");
static_assert(sigurdos::mesh::COMPANION_TEXT_SIGNED_PLAIN ==
                  sigurdos::comms::COMPANION_TXT_SIGNED_PLAIN,
              "signed-text companion type must match the wire protocol");

static_assert(sigurdos::mesh::SIGURDOS_ADVERT_BLOB_MAX_LEN <= MAX_FRAME_SIZE - 1,
              "serialized adverts must fit the companion response frame");

// g_companion_bridge carries ~3 KB of frame and offline-queue buffers. The
// internal dram0_0_seg .bss region is nearly full at baseline, so the instance
// is constructed in PSRAM (with an internal-DRAM fallback) instead of .bss.
static CompanionBridge* g_companion_bridge_ptr = nullptr;

static CompanionBridge* companionBridge()
{
    if (!g_companion_bridge_ptr) {
        void* mem = heap_caps_malloc(sizeof(CompanionBridge),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!mem) {
            mem = heap_caps_malloc(sizeof(CompanionBridge),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (mem) g_companion_bridge_ptr = new (mem) CompanionBridge();
    }
    return g_companion_bridge_ptr;
}

#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
static sigurdos::comms::ObservedSerialBLEInterface g_ble_serial;
#elif defined(SIGURDOS_COMPANION_USB) && SIGURDOS_COMPANION_USB
#include <helpers/ArduinoSerialInterface.h>
static ArduinoSerialInterface g_usb_serial;
#endif

static constexpr uint32_t SIGURDOS_BLE_PIN_MIN = 100000;
static constexpr uint32_t SIGURDOS_BLE_PIN_MAX = 999999;

static void generate_random_ble_pin() {
    sigurdos::NodePrefs p = sigurdos::prefs_get();
    if (p.ble_pin != 0) return;  // already generated
    // Generate a random 6-digit PIN using ESP32 hardware RNG
    uint32_t pin = SIGURDOS_BLE_PIN_MIN + (esp_random() % (SIGURDOS_BLE_PIN_MAX - SIGURDOS_BLE_PIN_MIN + 1));
    p.ble_pin = pin;
    sigurdos::prefs_set(p);
}

static void fillStoredPrefixForName(const char* name, uint8_t* out)
{
    if (!out) return;
    memset(out, 0, sigurdos::mesh::SIGURDOS_MSG_PREFIX_LEN);
    if (!mesh_ptr() || !name) return;
    ::ContactInfo tmp;
    for (int i = 0; i < mesh_ptr()->getNumContacts(); i++) {
        if (mesh_ptr()->getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
            memcpy(out, tmp.id.pub_key, sigurdos::mesh::SIGURDOS_MSG_PREFIX_LEN);
            return;
        }
    }
}

// Defaults live on the declaration in companion_adapter.h.
// txt_type is COMPANION_TXT_PLAIN=0, COMPANION_TXT_CLI_DATA=1 or
// COMPANION_TXT_SIGNED_PLAIN=2; extra carries e.g. the 4-byte sender prefix
// of signed messages; sender_prefix is the exact pubkey prefix from the
// packet metadata.
bool sigurdos::mesh::storeIncomingMessageForCompanion(
                                             const char* sender, const char* channel,
                                             const char* text, int rssi, float snr,
                                             uint32_t sender_timestamp,
                                             uint8_t path_len,
                                             const uint8_t* sender_prefix,
                                             uint8_t txt_type,
                                             const uint8_t* extra,
                                             uint8_t extra_len)
{
    sigurdos::mesh::StoredMessage msg{};
    const bool is_channel = channel && channel[0];
    if (is_channel) {
        strncpy(msg.conversation, channel, sizeof(msg.conversation) - 1);
        snprintf(msg.text, sizeof(msg.text), "%s: %s", sender ? sender : "", text ? text : "");
        // Don't split a multi-byte UTF-8 character at the truncation boundary.
        // Walk back from the last byte; if it's a continuation byte (0x80-0xBF),
        // find and truncate at the character start (0xC0-0xFD or 0x00-0x7F).
        {
            size_t tlen = strlen(msg.text);
            while (tlen > 0 && (msg.text[tlen - 1] & 0xC0) == 0x80) {
                msg.text[--tlen] = '\0';
            }
            // Strip any orphaned multi-byte start byte (0xC0–0xFD)
            // left after continuation-byte cleanup (follow-up to #653).
            if (tlen > 0 && (msg.text[tlen - 1] & 0xC0) == 0xC0) {
                msg.text[--tlen] = '\0';
            }
        }
    } else {
        formatDmConversation(msg.conversation, sizeof(msg.conversation), sender);
        strncpy(msg.text, text ? text : "", sizeof(msg.text) - 1);
    }
    strncpy(msg.sender, sender ? sender : "", sizeof(msg.sender) - 1);
    // Use the originating packet's timestamp so the app shows the real send
    // time. Fall back to the local clock only if the sender left it unset —
    // otherwise an unset RTC would surface every message as "1970".
    msg.timestamp = sender_timestamp ? sender_timestamp : meshRtcTime();
    msg.is_self = false;
    msg.is_channel = is_channel;
    msg.acked = false;
    msg.rssi = (int16_t)rssi;
    msg.snr_quarters = (int8_t)(snr * 4.0f);
    // Mesh path-length byte for the V3 frame (hop count + hash size). 0xFF from
    // a direct/unknown route; the real pkt->path_len for flood-routed packets.
    msg.path_len = path_len;
    msg.txt_type = txt_type;
    msg.extra_len = (extra_len > 8) ? 8 : extra_len;
    if (extra && extra_len > 0) {
        memcpy(msg.extra, extra, msg.extra_len);
    }
    // Use the exact sender pubkey prefix from the packet metadata when
    // available; otherwise fall back to name-based lookup for older callers.
    if (sender_prefix) {
        memcpy(msg.sender_prefix, sender_prefix, sigurdos::mesh::SIGURDOS_MSG_PREFIX_LEN);
    } else {
        fillStoredPrefixForName(sender, msg.sender_prefix);
    }
    uint32_t store_id = 0;
    if (sigurdos::mesh::messageStoreAppend(msg, &store_id)) {
        msg.store_id = store_id;
        if (CompanionBridge* b = companionBridge()) b->enqueueMessage(msg);
        return true;
    }
    return false;
}

// Shared ::ContactInfo → CompanionContact conversion, used by the host accessors
// and by the live-advert/path push fan-out below.
static void companionContactFromInfo(const ::ContactInfo& c, CompanionContact& out)
{
    memset(&out, 0, sizeof(out));
    memcpy(out.pub_key, c.id.pub_key, sizeof(out.pub_key));
    out.type = c.type;
    out.flags = c.flags;
    out.out_path_len = c.out_path_len;
    memcpy(out.out_path, c.out_path, sizeof(out.out_path));
    strncpy(out.name, c.name, sizeof(out.name) - 1);
    out.last_advert_timestamp = c.last_advert_timestamp;
    out.lastmod = c.lastmod;
    out.gps_lat = c.gps_lat;
    out.gps_lon = c.gps_lon;
}

class WrapperCompanionHost final : public CompanionBridgeHost {
public:
    uint32_t blePin() const override {
        return sigurdos::prefs_get().ble_pin;
    }
    uint8_t clientRepeat() const override { return sigurdos::prefs_get().client_repeat; }
    uint8_t pathHashMode() const override { return sigurdos::prefs_get().path_hash_mode; }

    void selfInfo(CompanionSelfInfo& out) const override {
        memset(&out, 0, sizeof(out));
        const sigurdos::NodePrefs& p = sigurdos::prefs_get();
        if (mesh_ptr()) memcpy(out.pub_key, mesh_ptr()->self_id.pub_key, sizeof(out.pub_key));
        strncpy(out.node_name, meshOwnName(), sizeof(out.node_name) - 1);
        out.advert_type = p.advert_type;
        out.tx_power_dbm = p.tx_power_dbm;
        out.max_tx_power_dbm = 22;
        if (p.share_location) {
            if (sigurdos_gps_has_fix()) {
                out.lat = (int32_t)(sigurdos_gps_latitude() * 1000000.0f);
                out.lon = (int32_t)(sigurdos_gps_longitude() * 1000000.0f);
            } else if (p.advert_location_valid) {
                out.lat = p.advert_lat;
                out.lon = p.advert_lon;
            }
        }
        out.multi_acks = p.multi_acks;
        out.advert_loc_policy = p.share_location ? 1 : 0;
        out.telemetry_modes = p.telemetry_modes;
        out.manual_add_contacts = p.manual_add_contacts;
        out.freq_khz = (uint32_t)(p.freq * 1000.0f);
        out.bw_hz = (uint32_t)(p.bw * 1000.0f);
        out.sf = p.sf;
        out.cr = p.cr;
    }

    uint32_t currentTime() const override { return sigurdos::mesh::getCurrentTime(); }
    bool setCurrentTime(uint32_t epoch) override {
        // Accept time from the companion client unconditionally.
        // The official app/CLI sets time on every connect; rejecting
        // because the device's onboard clock drifted ahead breaks sync.
        return sigurdos::mesh::setSystemTime(epoch);
    }
    uint16_t batteryMilliVolts() const override { return sigurdos_battery_mv(); }
    uint32_t storageUsedKb() const override { return (uint32_t)(SPIFFS.usedBytes() / 1024); }
    uint32_t storageTotalKb() const override { return (uint32_t)(SPIFFS.totalBytes() / 1024); }

    int contactCount() const override { return mesh_ptr() ? mesh_ptr()->getNumContacts() : 0; }
    bool getContact(int index, CompanionContact& out) const override {
        if (!mesh_ptr()) return false;
        ::ContactInfo c{};
        if (!mesh_ptr()->getContactByIdx((uint32_t)index, c)) return false;
        companionContactFromInfo(c, out);
        return true;
    }
    bool getContactByPubKeyPrefix(const uint8_t* prefix, size_t prefix_len,
                                  CompanionContact& out) const override {
        if (!mesh_ptr() || !prefix) return false;
        ::ContactInfo* c = mesh_ptr()->lookupContactByPubKey(prefix, (int)prefix_len);
        if (!c) return false;
        companionContactFromInfo(*c, out);
        return true;
    }

    // Companion protocol uses fixed slot indices 0..MAX_GROUP_CHANNELS-1.
    // Return the full slot count even for empty slots (upstream clients
    // expect every index to be addressable).
    int channelCount() const override { return MAX_GROUP_CHANNELS; }
    bool getChannel(int index, CompanionChannel& out) const override {
        if (!mesh_ptr() || index < 0 || index >= MAX_GROUP_CHANNELS) return false;
        ChannelDetails cd{};
        // Use the slot-based BaseChatMesh accessor — returns true for any
        // valid index even when the slot is empty (name="" / secret=zeros).
        if (!mesh_ptr()->BaseChatMesh::getChannel(index, cd)) return false;
        memset(&out, 0, sizeof(out));
        strncpy(out.name, cd.name, sizeof(out.name) - 1);
        memcpy(out.secret, cd.channel.secret, sizeof(out.secret));
        return true;
    }
    bool setChannel(int index, const CompanionChannel& channel) override {
        if (!mesh_ptr() || index < 0 || index >= MAX_GROUP_CHANNELS) return false;
        ChannelDetails cd{};
        strncpy(cd.name, channel.name, sizeof(cd.name) - 1);
        memcpy(cd.channel.secret, channel.secret, sizeof(cd.channel.secret));
        if (!mesh_ptr()->BaseChatMesh::setChannel(index, cd)) return false;
        sigurdos::mesh::saveChannels();
        sigurdos::mesh::syncRegionsFromChannels();
        return true;
    }

    CompanionSendResult sendTextByPubKeyPrefix(const uint8_t* prefix, size_t prefix_len,
                                               uint8_t txt_type, uint8_t attempt,
                                               uint32_t timestamp,
                                               const char* text) override {
        CompanionSendResult result{};
        if (!meshRadioTxAllowed()) return result;
        if (!mesh_ptr() || !prefix || !text) return result;
        ::ContactInfo* contact = mesh_ptr()->lookupContactByPubKey(prefix, (int)prefix_len);
        if (!contact) return result;

        uint32_t ts = 0;
        if (txt_type == sigurdos::comms::COMPANION_TXT_CLI_DATA) {
            // CLI data: always use the local unique RTC time to avoid replay
            // protection failures. The app-provided timestamp is ignored.
            ts = meshRtcTimeUnique();
        } else {
            ts = timestamp ? timestamp : meshRtcTime();
        }
        uint32_t expected_ack = 0;
        uint32_t est_timeout = 0;
        int send_result = MSG_SEND_FAILED;
        if (txt_type == sigurdos::comms::COMPANION_TXT_CLI_DATA) {
            send_result = mesh_ptr()->sendCommandData(*contact, ts, attempt, text, est_timeout);
        } else {
            send_result = mesh_ptr()->sendMessage(*contact, ts, attempt, text,
                                              expected_ack, est_timeout);
        }
        if (send_result == MSG_SEND_FAILED) return result;
        if (expected_ack) mesh_ptr()->addPendingAck(contact->name, ts, expected_ack);

        char conversation[sigurdos::mesh::SIGURDOS_MSG_CONVERSATION_LEN];
        formatDmConversation(conversation, sizeof(conversation), contact->name);
        meshStoreOutgoingMessage(conversation, text, ts, false);
        meshQueuePushOutgoing(conversation, meshOwnName(), text, ts);
        sigurdos::mesh::pushPacketLog(meshOwnName(), 0, 0.0f, "TX_DM");

        result.ok = true;
        result.sent_flood = send_result == MSG_SEND_SENT_FLOOD;
        result.expected_ack = expected_ack;
        result.est_timeout = est_timeout;
        return result;
    }

    CompanionSendResult sendChannelText(int channel_index, uint32_t timestamp,
                                        const char* text) override {
        CompanionSendResult result{};
        if (!meshRadioTxAllowed()) return result;
        if (!mesh_ptr() || !text) return result;
        ChannelDetails cd;
        if (!mesh_ptr()->BaseChatMesh::getChannel(channel_index, cd)) return result;
        uint32_t ts = timestamp ? timestamp : meshRtcTime();
        if (!mesh_ptr()->sendGroupMessage(ts, cd.channel, meshOwnName(), text, (int)strlen(text))) {
            return result;
        }
        meshStoreOutgoingMessage(cd.name, text, ts, true);
        meshQueuePushOutgoing(cd.name, meshOwnName(), text, ts);
        sigurdos::mesh::pushPacketLog(meshOwnName(), 0, 0.0f, "TX_CHAN");
        result.ok = true;
        result.sent_flood = true;
        result.expected_ack = 0;
        result.est_timeout = 0;
        return result;
    }

    bool sendChannelData(int channel_index,
                         const uint8_t* path,
                         uint8_t path_len,
                         uint16_t data_type,
                         const uint8_t* payload,
                         size_t payload_len) override {
        if (!meshRadioTxAllowed()) return false;
        if (!mesh_ptr() || payload_len > sigurdos::comms::SIGURDOS_COMPANION_CHANNEL_DATA_MAX_PAYLOAD) {
            return false;
        }
        if (payload_len > 0 && !payload) return false;
        return mesh_ptr()->sendGroupDataToChannel(channel_index, path, path_len,
                                                  data_type, payload, (int)payload_len);
    }
    bool sendRawData(const uint8_t* path, uint8_t path_len,
                     const uint8_t* payload, size_t payload_len) override {
        if (!meshRadioTxAllowed() || !mesh_ptr()) return false;
        return mesh_ptr()->sendRawDataCompanion(path, path_len, payload, payload_len);
    }

    bool sendAdvert(bool flood) override {
        if (!meshRadioTxAllowed()) return false;
        if (flood) {
            // Flood-scoped advert via existing broadcast path
            return sigurdos::mesh::sendAdvert();
        }
        // Zero-hop self advert — the upstream companion semantics
        if (!mesh_ptr()) return false;
        const sigurdos::NodePrefs& p = sigurdos::prefs_get();
        ::mesh::Packet* pkt;
        if (p.share_location && sigurdos_gps_has_fix()) {
            pkt = mesh_ptr()->createSelfAdvert(meshOwnName(),
                (double)sigurdos_gps_latitude(),
                (double)sigurdos_gps_longitude());
        } else if (p.share_location && p.advert_location_valid) {
            pkt = mesh_ptr()->createSelfAdvert(meshOwnName(),
                (double)p.advert_lat / 1000000.0,
                (double)p.advert_lon / 1000000.0);
        } else {
            pkt = mesh_ptr()->createSelfAdvert(meshOwnName());
        }
        if (!pkt) return false;
        mesh_ptr()->sendZeroHop(pkt);
        return true;
    }

    bool setAdvertName(const char* name) override {
        if (!name || !name[0]) return false;
        sigurdos::NodePrefs p = sigurdos::prefs_get();
        strncpy(p.node_name, name, sizeof(p.node_name) - 1);
        p.node_name[sizeof(p.node_name) - 1] = '\0';
        sigurdos::prefs_set(p);
        sigurdos::mesh::setOwnName(p.node_name);
        return true;
    }

    bool setAdvertLatLon(int32_t lat, int32_t lon) override {
        if (lat < -90000000 || lat > 90000000) return false;
        if (lon < -180000000 || lon > 180000000) return false;
        sigurdos::NodePrefs p = sigurdos::prefs_get();
        p.share_location = true;
        p.advert_location_valid = true;
        p.advert_lat = lat;
        p.advert_lon = lon;
        sigurdos::prefs_set(p);
        return true;
    }

    bool setRadioParams(uint32_t freq_khz,
                        uint32_t bw_hz,
                        uint8_t sf,
                        uint8_t cr,
                        uint8_t client_repeat) override {
        if (freq_khz < 150000 || freq_khz > 2500000) return false;
        if (bw_hz < 7800 || bw_hz > 500000) return false;
        if (sf < 5 || sf > 12) return false;
        if (cr < 5 || cr > 8) return false;
        if (client_repeat > 1) return false;

        sigurdos::NodePrefs proposed = sigurdos::prefs_get();
        const int8_t tx_power =
            (proposed.tx_power_dbm >= -9 && proposed.tx_power_dbm <= 22)
            ? proposed.tx_power_dbm
            : LORA_TX_PWR;
        const float freq = (float)freq_khz / 1000.0f;
        const float bw = (float)bw_hz / 1000.0f;

        proposed.configured = true;
        proposed.freq = freq;
        proposed.bw = bw;
        proposed.sf = sf;
        proposed.cr = cr;
        proposed.tx_power_dbm = tx_power;
        proposed.client_repeat = client_repeat;

        const sigurdos::RadioProfile* matched = sigurdos::radio_profile_match(proposed);
        if (matched) {
            sigurdos::radio_profile_apply(*matched, proposed);
        } else {
            sigurdos::radio_profile_set_custom(proposed);
        }

        uint32_t repeat_freq_khz = 0;
        if (client_repeat != 0 &&
            (!sigurdos::radio_profile_repeat_frequency_khz(proposed,
                                                           &repeat_freq_khz) ||
             repeat_freq_khz != freq_khz)) {
            return false;
        }

        if (!sigurdos::mesh::applyRadioParams(freq, bw, sf, cr,
                                              tx_power, proposed.rx_boosted_gain)) {
            return false;
        }

        sigurdos::prefs_set(proposed);
        return true;
    }

    bool setRadioTxPower(int8_t tx_power_dbm) override {
        if (tx_power_dbm < -9 || tx_power_dbm > 22) return false;
        sigurdos::NodePrefs p = sigurdos::prefs_get();

        const float freq = p.configured ? p.freq : LORA_FREQ;
        const float bw = p.configured ? p.bw : LORA_BW;
        const int sf = p.configured ? p.sf : LORA_SF;
        const int cr = p.configured ? p.cr : LORA_CR;
        if (!sigurdos::mesh::applyRadioParams(freq, bw, sf, cr,
                                              tx_power_dbm, p.rx_boosted_gain)) {
            return false;
        }

        p.tx_power_dbm = tx_power_dbm;
        sigurdos::prefs_set(p);
        return true;
    }

    void tuningParams(uint32_t& rx_delay_base_x1000,
                      uint32_t& tx_delay_factor_x1000) const override {
        sigurdos::NodePrefs p = sigurdos::prefs_get();
        rx_delay_base_x1000 = prefFloatToX1000(p.rx_delay_base);
        tx_delay_factor_x1000 = prefFloatToX1000(p.tx_delay_factor);
    }

    bool setTuningParams(uint32_t rx_delay_base_x1000,
                         uint32_t tx_delay_factor_x1000) override {
        if (rx_delay_base_x1000 > 20000) return false;
        if (tx_delay_factor_x1000 > 2000) return false;
        sigurdos::NodePrefs p = sigurdos::prefs_get();
        p.rx_delay_base = (float)rx_delay_base_x1000 / 1000.0f;
        p.tx_delay_factor = (float)tx_delay_factor_x1000 / 1000.0f;
        sigurdos::prefs_set(p);
        return true;
    }

    bool setBlePin(uint32_t pin) override {
        // Align with the on-device 4-digit PIN gate (screens_common.cpp:280)
        // and settings PIN setter (screen_settings_system.cpp:678), which both
        // cap at 4 digits (0–9999). Previously required 100000–999999 (6-digit),
        // making companion-set PINs impossible to enter on the device (SEC-002).
        if (pin > 9999) return false;
        sigurdos::NodePrefs p = sigurdos::prefs_get();
        p.device_pin = pin;
        sigurdos::prefs_set(p);
        return true;
    }

    bool exportPrivateKey(uint8_t* out64) const override {
        if (!mesh_ptr() || !out64) return false;
        return mesh_ptr()->self_id.writeTo(out64, PRV_KEY_SIZE) >= PRV_KEY_SIZE;
    }

    bool importPrivateKey(const uint8_t* key64) override {
        if (!mesh_ptr() || !key64) return false;
        if (!::mesh::LocalIdentity::validatePrivateKey(key64)) return false;
        mesh_ptr()->self_id.readFrom(key64, PRV_KEY_SIZE);
        meshSaveSelfIdentity();
        // Invalidate ECDH shared secrets derived from the old identity and
        // reload persisted contacts with the new identity (matches upstream
        // MyMesh::CMD_IMPORT_PRIVATE_KEY — resetContacts + loadContacts).
        mesh_ptr()->resetAllContacts();
        sigurdos::mesh::loadContacts();
        // Clear login session state — stale LOGIN_OK/permission entries from
        // the old identity would otherwise persist against contact-name keys
        // and present false-positive login UI after identity change (RC6-MED-002).
        mesh_ptr()->clearAllLoginEntries();
        return true;
    }

    void setOtherParams(const CompanionOtherParams& op) override {
        sigurdos::NodePrefs p = sigurdos::prefs_get();
        if (op.telemetry_present) p.telemetry_modes = op.telemetry_modes;
        if (op.loc_policy_present) p.share_location = (op.advert_loc_policy != 0);
        if (op.multi_acks_present) p.multi_acks = (op.multi_acks != 0);
        p.manual_add_contacts = op.manual_add_contacts;
        sigurdos::prefs_set(p);
    }
    bool setPathHashMode(uint8_t mode) override {
        // Modes 0-2 = 1/2/3-byte path hash. Mode 3 is reserved (rejected).
        if (mode > 2) return false;
        sigurdos::NodePrefs p = sigurdos::prefs_get();
        p.path_hash_mode = mode;
        sigurdos::prefs_set(p);
        return true;
    }
    void getAutoAddConfig(uint8_t* cfg, uint8_t* max_hops) const override {
        const sigurdos::NodePrefs& p = sigurdos::prefs_get();
        if (cfg) *cfg = p.autoadd_config;
        if (max_hops) *max_hops = p.autoadd_max_hops;
    }
    void setAutoAddConfig(uint8_t cfg, uint8_t max_hops) override {
        sigurdos::NodePrefs p = sigurdos::prefs_get();
        p.autoadd_config = cfg;
        p.autoadd_max_hops = max_hops;
        sigurdos::prefs_set(p);
    }
    int8_t maxTxPowerDbm() const override { return 22; }

    // ── Contact CRUD / connection ────────────────────────────
    bool getContactByPubKey(const uint8_t* pub_key, CompanionContact& out) const override {
        if (!mesh_ptr() || !pub_key) return false;
        ::ContactInfo* c = mesh_ptr()->lookupContactByPubKey(
            pub_key, (int)sigurdos::comms::SIGURDOS_COMPANION_PUB_KEY_SIZE);
        if (!c) return false;
        companionContactFromInfo(*c, out);
        return true;
    }
    bool addOrUpdateContact(const CompanionContact& c) override {
        if (!mesh_ptr()) return false;
        ::ContactInfo* existing = mesh_ptr()->lookupContactByPubKey((const uint8_t*)c.pub_key, 32);
        ::ContactInfo ci{};
        if (existing) ci = *existing;
        memcpy(ci.id.pub_key, c.pub_key, 32);
        ci.type = c.type;
        ci.flags = c.flags;
        ci.out_path_len = c.out_path_len;
        memcpy(ci.out_path, c.out_path, sizeof(ci.out_path));
        strncpy(ci.name, c.name, sizeof(ci.name) - 1);
        ci.name[sizeof(ci.name) - 1] = '\0';
        ci.last_advert_timestamp = c.last_advert_timestamp;
        ci.gps_lat = c.gps_lat;
        ci.gps_lon = c.gps_lon;
        ci.lastmod = c.lastmod;
        bool ok;
        if (existing) { *existing = ci; ok = true; }
        else ok = mesh_ptr()->addContact(ci);
        if (ok) sigurdos::mesh::saveContacts();
        return ok;
    }
    bool removeContactByPubKey(const uint8_t* pub_key) override {
        if (!mesh_ptr() || !pub_key) return false;
        ::ContactInfo* c = mesh_ptr()->lookupContactByPubKey(pub_key, 32);
        if (!c) return false;
        bool ok = mesh_ptr()->BaseChatMesh::removeContact(*c);
        if (ok) sigurdos::mesh::saveContacts();
        return ok;
    }
    bool resetPathByPubKey(const uint8_t* pub_key) override {
        if (!mesh_ptr() || !pub_key) return false;
        ::ContactInfo* c = mesh_ptr()->lookupContactByPubKey(pub_key, 32);
        if (!c) return false;
        mesh_ptr()->BaseChatMesh::resetPathTo(*c);
        sigurdos::mesh::saveContacts();
        return true;
    }
    bool shareContactByPubKey(const uint8_t* pub_key) override {
        if (!mesh_ptr() || !pub_key) return false;
        ::ContactInfo* c = mesh_ptr()->lookupContactByPubKey(pub_key, 32);
        if (!c) return false;
        return mesh_ptr()->shareContactZeroHop(*c);
    }
    int exportContactByPubKey(const uint8_t* pub_key, uint8_t* out, size_t out_cap) override {
        if (!mesh_ptr() || !out) return 0;
        if (!pub_key) return mesh_ptr()->exportSelfContact(meshOwnName(), out, out_cap);
        ::ContactInfo* c = mesh_ptr()->lookupContactByPubKey(pub_key, 32);
        if (!c) return 0;
        return mesh_ptr()->exportContactBounded(*c, out, out_cap);
    }
    bool importContact(const uint8_t* data, size_t len) override {
        if (!mesh_ptr() || !data) return false;
        bool ok = mesh_ptr()->importContact(data, (uint8_t)len);
        if (ok) sigurdos::mesh::saveContacts();
        return ok;
    }
    bool hasConnectionTo(const uint8_t* pub_key) const override {
        return mesh_ptr() && pub_key && mesh_ptr()->companionHasConnection(pub_key);
    }
    void logout(const uint8_t* pub_key) override {
        if (mesh_ptr() && pub_key) mesh_ptr()->companionStopConnection(pub_key);
    }

    // ── System ───────────────────────────────────────────────
    void reboot() override {
        sigurdos::mesh::saveState();
        sigurdos::mesh::saveContacts();
        sigurdos::mesh::saveChannels();
        delay(200);
        ESP.restart();
    }
    bool factoryReset() override {
        sigurdos::mesh::factoryReset();  // formats + reboots; does not return
        return true;
    }

    // ── Stats ────────────────────────────────────────────────
    void coreStats(CompanionCoreStats& s) const override {
        s.batt_mv = sigurdos_battery_mv();
        s.uptime_secs = (uint32_t)(millis() / 1000);
        s.err_flags = 0;
        s.queue_len = 0;
    }
    void radioStats(CompanionRadioStats& s) const override {
        sigurdos::mesh::MeshRadioDriverStats d{};
        sigurdos::mesh::meshRadioDriverStats(d);
        s.noise_floor = (int16_t)sigurdos::mesh::getNoiseFloor();
        s.last_rssi = (int8_t)d.last_rssi;
        s.last_snr_quarters = (int8_t)(d.last_snr * 4.0f);
        s.tx_air_secs = (uint32_t)(sigurdos::mesh::getTotalTxAirtimeMs() / 1000);
        s.rx_air_secs = (uint32_t)(sigurdos::mesh::getTotalRxAirtimeMs() / 1000);
    }
    void packetStats(CompanionPacketStats& s) const override {
        sigurdos::mesh::MeshRadioDriverStats d{};
        sigurdos::mesh::meshRadioDriverStats(d);
        s.recv = d.packets_recv;
        s.sent = d.packets_sent;
        s.sent_flood = sigurdos::mesh::getNumSentFlood();
        s.sent_direct = sigurdos::mesh::getNumSentDirect();
        s.recv_flood = sigurdos::mesh::getNumRecvFlood();
        s.recv_direct = sigurdos::mesh::getNumRecvDirect();
        s.recv_errors = d.packets_recv_errors;
    }
    size_t allowedRepeatFreqRanges(uint32_t* pairs, size_t max_pairs) const override {
        if (!pairs || max_pairs == 0) return 0;

        uint32_t frequency_khz = 0;
        if (!sigurdos::radio_profile_repeat_frequency_khz(
                sigurdos::prefs_get(), &frequency_khz)) {
            return 0;
        }

        pairs[0] = frequency_khz;
        pairs[1] = frequency_khz;
        return 1;
    }

    // ── Flood scope (companion regions) ──────────────────────
    bool getDefaultFloodScope(char* name_out, uint8_t* key_out) const override {
        const char* active = sigurdos::mesh::getActiveRegion();
        if (!active || !active[0]) return false;

        RegionEntry* r = sigurdos::mesh::findRegion(active);
        if (!r) return false;

        RegionMap* map = sigurdos::mesh::getRegionMap();
        if (!map) return false;

        TransportKey keys[1];
        int nk = map->getTransportKeysFor(*r, keys, 1);
        if (nk <= 0) return false;

        if (name_out) { memset(name_out, 0, 31); strncpy(name_out, r->name, 30); }
        if (key_out) {
            // Check if a persisted private key exists (for $private scopes)
            const sigurdos::NodePrefs& p = sigurdos::prefs_get();
            if (p.default_scope_key_hex[0] != '\0' && active[0] == '$') {
                sigurdos::mesh::scopeKeyHexDecode(p.default_scope_key_hex, key_out);
            } else {
                memcpy(key_out, keys[0].key, 16);
            }
        }
        return true;
    }
    void setDefaultFloodScope(const char* name, const uint8_t* key) override {
        if (!name || !name[0]) {
            sigurdos::mesh::setActiveRegion("");
            return;
        }

        // Ensure the region exists in RegionMap
        RegionEntry* r = sigurdos::mesh::findRegion(name);
        if (!r) {
            r = sigurdos::mesh::addRegion(name);
        }

        // If a key was provided for a $private region, persist it in NVS
        // so it survives reboot. The companion app provides it again on connect.
        if (key && name[0] == '$') {
            sigurdos::NodePrefs p = sigurdos::prefs_get();
            sigurdos::mesh::scopeKeyHexEncode(key, p.default_scope_key_hex);
            sigurdos::prefs_set(p);
        } else if (key) {
            // Public or #hashtag key — clear any persisted $private key
            sigurdos::NodePrefs p = sigurdos::prefs_get();
            p.default_scope_key_hex[0] = '\0';
            sigurdos::prefs_set(p);
        }

        sigurdos::mesh::setActiveRegion(name);
    }
    void setFloodScopeOverride(const uint8_t* key, bool unscoped) override {
        if (!mesh_ptr()) return;
        if (unscoped) mesh_ptr()->clearActiveScope();
        else if (key) mesh_ptr()->setActiveScope(key);
        else mesh_ptr()->clearActiveScope();
    }

    // ── Async requests ───────────────────────────────────────
    CompanionSendResult sendLogin(const uint8_t* pub_key, const char* password) override {
        CompanionSendResult r{};
        if (!meshRadioTxAllowed()) return r;
        if (!mesh_ptr() || !pub_key) return r;
        ::ContactInfo* c = mesh_ptr()->lookupContactByPubKey(pub_key, 32);
        if (!c) return r;
        uint32_t est_timeout = 0;
        int res = mesh_ptr()->sendLoginCompanion(*c, password ? password : "", est_timeout);
        if (res == MSG_SEND_FAILED) return r;
        r.ok = true;
        r.sent_flood = (res == MSG_SEND_SENT_FLOOD);
        memcpy(&r.expected_ack, c->id.pub_key, 4);  // login is matched by pubkey prefix
        r.est_timeout = est_timeout;
        return r;
    }
    CompanionSendResult sendStatusReq(const uint8_t* pub_key) override {
        return sendReqByPubKey(pub_key, /*telemetry=*/false);
    }
    CompanionSendResult sendTelemetryReq(const uint8_t* pub_key) override {
        return sendReqByPubKey(pub_key, /*telemetry=*/true);
    }
    CompanionSendResult sendBinaryReq(const uint8_t* pub_key,
                                      const uint8_t* data,
                                      uint8_t data_len) override {
        CompanionSendResult r{};
        if (!meshRadioTxAllowed() || !mesh_ptr() || !pub_key ||
            !data || data_len == 0) {
            return r;
        }
        ::ContactInfo* contact = mesh_ptr()->lookupContactByPubKey(pub_key, 32);
        if (!contact) return r;
        uint32_t tag = 0;
        uint32_t est_timeout = 0;
        const int result = mesh_ptr()->sendBinaryRequestCompanion(
            *contact, data, data_len, tag, est_timeout);
        if (result == MSG_SEND_FAILED) return r;
        r.ok = true;
        r.sent_flood = result == MSG_SEND_SENT_FLOOD;
        r.expected_ack = tag;
        r.est_timeout = est_timeout;
        return r;
    }
    void cancelBinaryReqs() override {
        if (mesh_ptr()) mesh_ptr()->cancelCompanionBinaryRequests();
    }
    CompanionSendResult sendTracePath(uint32_t tag, uint32_t auth, uint8_t flags,
                                      const uint8_t* path, uint8_t path_len) override {
        CompanionSendResult r{};
        if (!meshRadioTxAllowed()) return r;
        if (!mesh_ptr()) return r;
        uint32_t est_timeout = 0;
        if (!mesh_ptr()->sendTracePathRaw(tag, auth, flags, path, path_len, est_timeout)) return r;
        r.ok = true;
        r.sent_flood = false;
        r.expected_ack = tag;
        r.est_timeout = est_timeout;
        return r;
    }
    CompanionSendResult sendPathDiscovery(const uint8_t* pub_key) override {
        CompanionSendResult r{};
        if (!meshRadioTxAllowed()) return r;
        if (!mesh_ptr() || !pub_key) return r;
        ::ContactInfo* c = mesh_ptr()->lookupContactByPubKey(pub_key, 32);
        if (!c) return r;
        uint32_t tag = mesh_ptr()->sendPathDiscovery(c->name);
        if (tag == 0) return r;
        r.ok = true;
        r.sent_flood = true;
        r.expected_ack = tag;
        r.est_timeout = 0;
        return r;
    }
    void selfTelemetry(uint8_t* out, size_t* out_len) const override {
        // Build a minimal CayenneLPP-compatible telemetry blob.
        // Format: [channel=2, type=2 (analog), value=uint16 LE * 100]
        // This encodes battery voltage in hundredths of a volt.
        if (!out || !out_len) return;
        uint16_t batt_mv = batteryMilliVolts();
        if (batt_mv == 0) { *out_len = 0; return; }
        // CayenneLPP analog output: battery voltage in 0.01V units
        uint16_t val = (uint16_t)(batt_mv / 10);  // mV → 0.01V
        out[0] = 2;        // channel 2 (battery)
        out[1] = 2;        // type 2 = analog output (signed 0.01 units)
        out[2] = (uint8_t)(val >> 8);
        out[3] = (uint8_t)(val & 0xFF);
        *out_len = 4;
    }
    int getCustomVars(char* out, size_t out_cap) const override {
        const sigurdos::NodePrefs& p = sigurdos::prefs_get();
        int n = snprintf(out, out_cap, "gps=%d,gps_interval=%d",
                         p.gps_enabled ? 1 : 0, (int)p.gps_interval);
        return (n > 0 && (size_t)n < out_cap) ? n : 0;
    }
    bool setCustomVar(const char* name, const char* value) override {
        if (!name || !value) return false;
        sigurdos::NodePrefs p = sigurdos::prefs_get();
        if (strcmp(name, "gps") == 0) {
            p.gps_enabled = (value[0] == '1');
            sigurdos::prefs_set(p);
            return true;
        } else if (strcmp(name, "gps_interval") == 0) {
            int iv = atoi(value);
            if (iv >= 0 && iv <= 86400) {
                p.gps_interval = iv < 5 ? 5 : (uint16_t)iv;
                sigurdos::prefs_set(p);
                return true;
            }
        }
        return false;
    }
    int signData(const uint8_t* data, size_t len, uint8_t* sig_out) override {
        if (!mesh_ptr() || !sig_out) return 0;
        mesh_ptr()->self_id.sign(sig_out, data, len);
        return (int)sigurdos::comms::SIGURDOS_COMPANION_SIGNATURE_SIZE;
    }
    uint8_t getAdvertPath(const uint8_t* pub_key,
                          uint8_t* path_out, uint8_t max_path,
                          uint32_t* timestamp_out) const override {
        if (!mesh_ptr() || !pub_key) return 0;
        const auto* entry = mesh_ptr()->getAdvertPathByKey(pub_key);
        if (!entry) return 0;
        uint8_t plen = entry->path_len;
        if (path_out && plen > 0 && max_path > 0) {
            memcpy(path_out, entry->path, plen < max_path ? plen : max_path);
        }
        if (timestamp_out) *timestamp_out = entry->recv_timestamp;
        return plen;
    }

private:
    static uint32_t prefFloatToX1000(float value) {
        if (value <= 0.0f) return 0;
        return (uint32_t)(value * 1000.0f + 0.5f);
    }

    // Resolve a contact by pub key, fire a STATUS/TELEMETRY request via the
    // tested wrapper helpers, and read back the assigned tag for RESP_CODE_SENT.
    CompanionSendResult sendReqByPubKey(const uint8_t* pub_key, bool telemetry) {
        CompanionSendResult r{};
        if (!meshRadioTxAllowed()) return r;
        if (!mesh_ptr() || !pub_key) return r;
        ::ContactInfo* c = mesh_ptr()->lookupContactByPubKey(pub_key, 32);
        if (!c) return r;
        char name[32];
        strncpy(name, c->name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        bool ok = telemetry ? sigurdos::mesh::requestTelemetry(name)
                            : sigurdos::mesh::requestStatus(name);
        if (!ok) return r;
        uint8_t want = telemetry ? (uint8_t)REQ_TYPE_GET_TELEMETRY_DATA
                                 : (uint8_t)REQ_TYPE_GET_STATUS;
        for (int i = 0; i < SigurdMeshV2::MAX_PENDING_REQUESTS; i++) {
            if (mesh_ptr()->_pending_reqs[i].in_use &&
                mesh_ptr()->_pending_reqs[i].req_type == want &&
                strcmp(mesh_ptr()->_pending_reqs[i].dest_name, name) == 0) {
                r.expected_ack = mesh_ptr()->_pending_reqs[i].tag;
                break;
            }
        }
        r.ok = true;
        r.sent_flood = (c->out_path_len == 0xFF);
        r.est_timeout = 0;
        return r;
    }
};

static WrapperCompanionHost g_companion_host;

// ── Mesh → companion-bridge fan-out ──────────────────────────────────
// These forward live mesh events to the phone app. They take primitive args
// (so the declarations in mesh_wrapper.h stay free of MeshCore types) and look
// up the full contact via mesh_ptr() where the bridge needs a contact frame.

void sigurdos::mesh::mesh_v2_companion_advert_push(const void* contact_info, bool is_new)
{
    if (!g_companion_bridge_ptr || !contact_info) return;
    const ::ContactInfo* c = static_cast<const ::ContactInfo*>(contact_info);
    CompanionContact cc{};
    companionContactFromInfo(*c, cc);  // NEW_ADVERT carries the full contact;
    g_companion_bridge_ptr->pushAdvert(cc, is_new);  // ADVERT uses only pub_key.
}

void sigurdos::mesh::mesh_v2_companion_path_push(const uint8_t* pub_key)
{
    if (!g_companion_bridge_ptr || !pub_key) return;
    CompanionContact cc{};
    memcpy(cc.pub_key, pub_key, sizeof(cc.pub_key));
    g_companion_bridge_ptr->pushPathUpdated(cc);
}

void sigurdos::mesh::mesh_v2_companion_contact_deleted_push(const uint8_t* pub_key)
{
    if (g_companion_bridge_ptr) g_companion_bridge_ptr->pushContactDeleted(pub_key);
}

void sigurdos::mesh::mesh_v2_companion_contacts_full_push()
{
    if (g_companion_bridge_ptr) g_companion_bridge_ptr->pushContactsFull();
}

void sigurdos::mesh::mesh_v2_companion_login_push(const uint8_t* pub_key, bool success,
                                                  uint8_t permission, bool is_admin)
{
    if (g_companion_bridge_ptr)
        g_companion_bridge_ptr->pushLoginResult(pub_key, success, permission, is_admin);
}

void sigurdos::mesh::mesh_v2_companion_status_push(const uint8_t* pub_key,
                                                   const uint8_t* blob, size_t len)
{
    if (g_companion_bridge_ptr)
        g_companion_bridge_ptr->pushStatusResponse(pub_key, blob, len);
}

void sigurdos::mesh::mesh_v2_companion_telemetry_push(const uint8_t* pub_key,
                                                      const uint8_t* blob, size_t len)
{
    if (g_companion_bridge_ptr)
        g_companion_bridge_ptr->pushTelemetryResponse(pub_key, blob, len);
}

void sigurdos::mesh::mesh_v2_companion_binary_push(uint32_t tag,
                                                   const uint8_t* blob,
                                                   size_t len)
{
    if (g_companion_bridge_ptr)
        g_companion_bridge_ptr->pushBinaryResponse(tag, blob, len);
void sigurdos::mesh::mesh_v2_companion_raw_data_push(int8_t snr_quarters, int8_t rssi,
                                                     const uint8_t* payload,
                                                     size_t payload_len)
{
    if (g_companion_bridge_ptr)
        g_companion_bridge_ptr->pushRawData(snr_quarters, rssi, payload, payload_len);
}

void sigurdos::mesh::mesh_v2_companion_trace_push(uint32_t tag, uint32_t auth, uint8_t flags,
                                                  const uint8_t* path_hashes,
                                                  const uint8_t* path_snrs,
                                                  uint8_t path_len, int8_t final_snr_quarters)
{
    if (g_companion_bridge_ptr)
        g_companion_bridge_ptr->pushTraceData(tag, auth, flags, path_hashes,
                                              path_snrs, path_len, final_snr_quarters);
}

void sigurdos::mesh::mesh_v2_notify_send_confirmed(uint32_t ack, uint32_t trip_time_ms)
{
    // Only forward if the bridge already exists — never allocate it here just to
    // report an ACK (it is created lazily on the first incoming/companion path).
    if (g_companion_bridge_ptr) {
        g_companion_bridge_ptr->notifySendConfirmed(ack, trip_time_ms);
    }
}

void sigurdos::mesh::mesh_v2_group_data_push(uint8_t channel_index,
                              uint8_t path_len,
                              int8_t snr_quarters,
                              uint16_t data_type,
                              const uint8_t* data,
                              size_t data_len)
{
    if (data_len > sigurdos::comms::SIGURDOS_COMPANION_CHANNEL_DATA_MAX_PAYLOAD) return;
    if (CompanionBridge* b = companionBridge()) {
        b->enqueueChannelData(channel_index, snr_quarters, path_len, data_type, data, data_len);
    }
}

// ── BLE hardware-validation telemetry ────────────────────────────────
#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE && \
    defined(SIGURDOS_COMPANION_BLE_VALIDATION) && SIGURDOS_COMPANION_BLE_VALIDATION
static constexpr const char* BLE_VALIDATION_LOG_PATH = "/ble_hw.txt";
static uint32_t ble_validation_last_log_ms = 0;

static void bleValidationAppendLine(const char* line)
{
    if (!line) return;
    File f = SPIFFS.open(BLE_VALIDATION_LOG_PATH, FILE_APPEND);
    if (!f) return;
    f.println(line);
    f.close();
}

static void bleValidationEmit(bool force)
{
    uint32_t now = millis();
    if (!force && (uint32_t)(now - ble_validation_last_log_ms) < 5000u) return;
    ble_validation_last_log_ms = now;

    const sigurdos::comms::BleSerialObserverStats s = g_ble_serial.stats();
    char line[320];
    snprintf(line, sizeof(line),
             "@ble_hw|ms=%lu|begun=%u|en=%u|conn=%u|adv=%u|authok=%lu|authfail=%lu|connect=%lu|disconnect=%lu|mtu=%u|rxw=%lu|rxd=%lu|rx=%lu|tx=%lu|txd=%lu|lrx=%u|ltx=%u",
             (unsigned long)now,
             s.begun ? 1u : 0u,
             s.enabled ? 1u : 0u,
             s.connected ? 1u : 0u,
             s.advertising_expected ? 1u : 0u,
             (unsigned long)s.auth_success_count,
             (unsigned long)s.auth_failure_count,
             (unsigned long)s.connect_count,
             (unsigned long)s.disconnect_count,
             (unsigned int)s.last_mtu,
             (unsigned long)s.ble_write_count,
             (unsigned long)s.ble_write_drop_count,
             (unsigned long)s.rx_frame_count,
             (unsigned long)s.tx_frame_count,
             (unsigned long)s.tx_drop_count,
             (unsigned int)s.last_rx_code,
             (unsigned int)s.last_tx_code);
    // Route through SIG_LOG* so production sources stay clear of the raw
    // serial print CI gate. SPIFFS retains the bare validation line for parsers.
    SIG_LOGW("%s", line);
    bleValidationAppendLine(line);
}

static void bleValidationStartLog()
{
    SPIFFS.remove(BLE_VALIDATION_LOG_PATH);
    bleValidationAppendLine("[ble-validation] log-start");
    bleValidationEmit(true);
}
#elif defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
static void bleValidationEmit(bool) {}
static void bleValidationStartLog() {}
#else
static void bleValidationEmit(bool) {}
#endif

// ── Adapter lifecycle (called from mesh_wrapper.cpp) ─────────────────

void sigurdos::mesh::companionAdapterInit()
{
#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
    // Generate a random per-device BLE PIN on first boot if not configured.
    // Replaces the old hardcoded default of 123456 with a unique 6-digit PIN
    // derived from ESP32 hardware RNG.
    generate_random_ble_pin();

    char ble_name[32];
    strncpy(ble_name, meshOwnName(), sizeof(ble_name) - 1);
    ble_name[sizeof(ble_name) - 1] = '\0';
    g_ble_serial.begin("MeshCore-", ble_name, g_companion_host.blePin());
    if (CompanionBridge* b = companionBridge()) {
        b->begin(&g_ble_serial, &g_companion_host);
        if (sigurdos::prefs_get().ble_enabled) {
            bool enabled = b->setEnabled(true);
#if defined(SIGURDOS_DEBUG) || \
    (defined(SIGURDOS_COMPANION_BLE_VALIDATION) && SIGURDOS_COMPANION_BLE_VALIDATION)
            SIG_LOGW("[mesh] Companion BLE advertising %s as MeshCore-%s pin=%06lu",
                     enabled ? "enabled" : "failed", ble_name,
                     (unsigned long)g_companion_host.blePin());
#endif
            (void)enabled;
        } else {
#if defined(SIGURDOS_DEBUG) || \
    (defined(SIGURDOS_COMPANION_BLE_VALIDATION) && SIGURDOS_COMPANION_BLE_VALIDATION)
            SIG_LOGW("[mesh] Companion BLE advertising disabled by prefs pin=%06lu",
                     (unsigned long)g_companion_host.blePin());
#endif
        }
        bleValidationStartLog();
    }
#elif defined(SIGURDOS_COMPANION_USB) && SIGURDOS_COMPANION_USB
    g_usb_serial.begin(Serial);
    if (CompanionBridge* b = companionBridge()) {
        b->begin(&g_usb_serial, &g_companion_host);
        b->setEnabled(true);
    }
#endif
}

void sigurdos::mesh::companionAdapterLoop()
{
    if (g_companion_bridge_ptr) g_companion_bridge_ptr->loop();
    bleValidationEmit(false);
}

// ── Companion BLE public API (declared in mesh_wrapper.h) ────────────

namespace sigurdos {
namespace mesh {

bool companionBleAvailable() {
#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
    return true;
#else
    return false;
#endif
}

bool companionBleSetEnabled(bool enabled) {
#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
    CompanionBridge* b = companionBridge();
    if (!b || !b->setEnabled(enabled)) return false;
#endif
    // Only persist after successful enablement to avoid state mismatch
    sigurdos::NodePrefs p = sigurdos::prefs_get();
    p.ble_enabled = enabled;
    sigurdos::prefs_set(p);
#if defined(SIGURDOS_COMPANION_BLE) && SIGURDOS_COMPANION_BLE
    return true;
#else
    return false;
#endif
}

bool companionBleEnabled() { CompanionBridge* b = companionBridge(); return b && b->isEnabled(); }
bool companionBleConnected() { CompanionBridge* b = companionBridge(); return b && b->isConnected(); }
uint32_t companionBleLastSyncTime() { CompanionBridge* b = companionBridge(); return b ? b->lastSyncTime() : 0; }
uint32_t companionBlePin() { return g_companion_host.blePin(); }

} // namespace mesh
} // namespace sigurdos
