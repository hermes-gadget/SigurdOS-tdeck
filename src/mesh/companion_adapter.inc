// Companion BLE bridge adapter for mesh_wrapper.cpp.
// This file is intentionally included from mesh_wrapper.cpp so it can access
// the existing singletons without widening the public mesh API.

#include "advert_blob.h"
#include <new>
#include <esp_heap_caps.h>
#include <esp_random.h>

static void saveIdentity(::mesh::LocalIdentity& id);

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
    Serial.printf("[mesh] Generated random BLE PIN: %06lu\n", (unsigned long)pin);
}

static void queue_push_outgoing(const char* conversation, const char* sender,
                                const char* text, uint32_t timestamp)
{
    if (!conversation || !sender || !text) return;
    if (msg_count >= MAX_QUEUED) {
        msg_drop_count++;
        return;
    }
    MeshMessage& m = msg_buf[msg_head];
    strncpy(m.sender, sender, sizeof(m.sender) - 1);
    m.sender[sizeof(m.sender) - 1] = '\0';
    strncpy(m.channel, conversation, sizeof(m.channel) - 1);
    m.channel[sizeof(m.channel) - 1] = '\0';
    strncpy(m.text, text, sizeof(m.text) - 1);
    m.text[sizeof(m.text) - 1] = '\0';
    m.timestamp = timestamp ? timestamp : rtc_clock.getCurrentTime();
    m.is_self = true;
    msg_head = (msg_head + 1) % MAX_QUEUED;
    msg_count++;
}

static void fillStoredPrefixForName(const char* name, uint8_t* out)
{
    if (!out) return;
    memset(out, 0, sigurdos::mesh::SIGURDOS_MSG_PREFIX_LEN);
    if (!g_mesh || !name) return;
    ::ContactInfo tmp;
    for (int i = 0; i < g_mesh->getNumContacts(); i++) {
        if (g_mesh->getContactByIdx((uint32_t)i, tmp) && strcmp(tmp.name, name) == 0) {
            memcpy(out, tmp.id.pub_key, sigurdos::mesh::SIGURDOS_MSG_PREFIX_LEN);
            return;
        }
    }
}

static void storeIncomingMessageForCompanion(const char* sender, const char* channel,
                                             const char* text, int rssi, float snr,
                                             uint32_t sender_timestamp = 0,
                                             uint8_t path_len = 0xFF,
                                             // Exact sender pubkey prefix from packet metadata.
                                             const uint8_t* sender_prefix = nullptr,
                                             // Companion text type (COMPANION_TXT_PLAIN=0,
                                             // COMPANION_TXT_CLI_DATA=1, COMPANION_TXT_SIGNED_PLAIN=2).
                                             uint8_t txt_type = sigurdos::comms::COMPANION_TXT_PLAIN,
                                             // Extra bytes (e.g. 4-byte sender prefix for signed msgs).
                                             const uint8_t* extra = nullptr,
                                             uint8_t extra_len = 0)
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
    msg.timestamp = sender_timestamp ? sender_timestamp : rtc_clock.getCurrentTime();
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
    sigurdos::mesh::messageStoreAppend(msg);
    if (CompanionBridge* b = companionBridge()) b->enqueueMessage(msg);
}

static void storeOutgoingMessageForCompanion(const char* conversation, const char* text,
                                             uint32_t timestamp, bool is_channel)
{
    sigurdos::mesh::StoredMessage msg{};
    strncpy(msg.conversation, conversation ? conversation : "", sizeof(msg.conversation) - 1);
    strncpy(msg.sender, own_name, sizeof(msg.sender) - 1);
    strncpy(msg.text, text ? text : "", sizeof(msg.text) - 1);
    msg.timestamp = timestamp ? timestamp : rtc_clock.getCurrentTime();
    msg.is_self = true;
    msg.is_channel = is_channel;
    msg.acked = false;
    msg.rssi = 0;
    msg.snr_quarters = 0;
    msg.path_len = 0xFF;  // self-sent; never mirrored to the app, value unused
    if (g_mesh) memcpy(msg.sender_prefix, g_mesh->self_id.pub_key,
                       sigurdos::mesh::SIGURDOS_MSG_PREFIX_LEN);
    sigurdos::mesh::messageStoreAppend(msg);
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
        if (g_mesh) memcpy(out.pub_key, g_mesh->self_id.pub_key, sizeof(out.pub_key));
        strncpy(out.node_name, own_name, sizeof(out.node_name) - 1);
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
        // Reject backwards time changes to protect replay/timestamp logic
        uint32_t now = sigurdos::mesh::getCurrentTime();
        if (epoch < now && now > 0) return false;
        return sigurdos::mesh::setSystemTime(epoch);
    }
    uint16_t batteryMilliVolts() const override { return sigurdos_battery_mv(); }
    uint32_t storageUsedKb() const override { return (uint32_t)(SPIFFS.usedBytes() / 1024); }
    uint32_t storageTotalKb() const override { return (uint32_t)(SPIFFS.totalBytes() / 1024); }

    int contactCount() const override { return g_mesh ? g_mesh->getNumContacts() : 0; }
    bool getContact(int index, CompanionContact& out) const override {
        if (!g_mesh) return false;
        ::ContactInfo c{};
        if (!g_mesh->getContactByIdx((uint32_t)index, c)) return false;
        companionContactFromInfo(c, out);
        return true;
    }
    bool getContactByPubKeyPrefix(const uint8_t* prefix, size_t prefix_len,
                                  CompanionContact& out) const override {
        if (!g_mesh || !prefix) return false;
        ::ContactInfo* c = g_mesh->lookupContactByPubKey(prefix, (int)prefix_len);
        if (!c) return false;
        companionContactFromInfo(*c, out);
        return true;
    }

    // Companion protocol uses fixed slot indices 0..MAX_GROUP_CHANNELS-1.
    // Return the full slot count even for empty slots (upstream clients
    // expect every index to be addressable).
    int channelCount() const override { return MAX_GROUP_CHANNELS; }
    bool getChannel(int index, CompanionChannel& out) const override {
        if (!g_mesh || index < 0 || index >= MAX_GROUP_CHANNELS) return false;
        ChannelDetails cd{};
        // Use the slot-based BaseChatMesh accessor — returns true for any
        // valid index even when the slot is empty (name="" / secret=zeros).
        if (!g_mesh->BaseChatMesh::getChannel(index, cd)) return false;
        memset(&out, 0, sizeof(out));
        strncpy(out.name, cd.name, sizeof(out.name) - 1);
        memcpy(out.secret, cd.channel.secret, sizeof(out.secret));
        return true;
    }
    bool setChannel(int index, const CompanionChannel& channel) override {
        if (!g_mesh || index < 0 || index >= MAX_GROUP_CHANNELS) return false;
        ChannelDetails cd{};
        strncpy(cd.name, channel.name, sizeof(cd.name) - 1);
        memcpy(cd.channel.secret, channel.secret, sizeof(cd.channel.secret));
        if (!g_mesh->BaseChatMesh::setChannel(index, cd)) return false;
        sigurdos::mesh::saveChannels();
        sigurdos::mesh::syncRegionsFromChannels();
        return true;
    }

    CompanionSendResult sendTextByPubKeyPrefix(const uint8_t* prefix, size_t prefix_len,
                                               uint8_t txt_type, uint8_t attempt,
                                               uint32_t timestamp,
                                               const char* text) override {
        CompanionSendResult result{};
        if (!sigurdos_mesh_radio_tx_allowed()) return result;
        if (!g_mesh || !prefix || !text) return result;
        ::ContactInfo* contact = g_mesh->lookupContactByPubKey(prefix, (int)prefix_len);
        if (!contact) return result;

        uint32_t ts = 0;
        if (txt_type == sigurdos::comms::COMPANION_TXT_CLI_DATA) {
            // CLI data: always use the local unique RTC time to avoid replay
            // protection failures. The app-provided timestamp is ignored.
            ts = rtc_clock.getCurrentTimeUnique();
        } else {
            ts = timestamp ? timestamp : rtc_clock.getCurrentTime();
        }
        uint32_t expected_ack = 0;
        uint32_t est_timeout = 0;
        int send_result = MSG_SEND_FAILED;
        if (txt_type == sigurdos::comms::COMPANION_TXT_CLI_DATA) {
            send_result = g_mesh->sendCommandData(*contact, ts, attempt, text, est_timeout);
        } else {
            send_result = g_mesh->sendMessage(*contact, ts, attempt, text,
                                              expected_ack, est_timeout);
        }
        if (send_result == MSG_SEND_FAILED) return result;
        if (expected_ack) g_mesh->addPendingAck(contact->name, ts, expected_ack);

        char conversation[sigurdos::mesh::SIGURDOS_MSG_CONVERSATION_LEN];
        formatDmConversation(conversation, sizeof(conversation), contact->name);
        storeOutgoingMessageForCompanion(conversation, text, ts, false);
        queue_push_outgoing(conversation, own_name, text, ts);
        sigurdos::mesh::pushPacketLog(own_name, 0, 0.0f, "TX_DM");

        result.ok = true;
        result.sent_flood = send_result == MSG_SEND_SENT_FLOOD;
        result.expected_ack = expected_ack;
        result.est_timeout = est_timeout;
        return result;
    }

    CompanionSendResult sendChannelText(int channel_index, uint32_t timestamp,
                                        const char* text) override {
        CompanionSendResult result{};
        if (!sigurdos_mesh_radio_tx_allowed()) return result;
        if (!g_mesh || !text) return result;
        ChannelDetails cd;
        if (!g_mesh->BaseChatMesh::getChannel(channel_index, cd)) return result;
        uint32_t ts = timestamp ? timestamp : rtc_clock.getCurrentTime();
        if (!g_mesh->sendGroupMessage(ts, cd.channel, own_name, text, (int)strlen(text))) {
            return result;
        }
        storeOutgoingMessageForCompanion(cd.name, text, ts, true);
        queue_push_outgoing(cd.name, own_name, text, ts);
        sigurdos::mesh::pushPacketLog(own_name, 0, 0.0f, "TX_CHAN");
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
        if (!sigurdos_mesh_radio_tx_allowed()) return false;
        if (!g_mesh || payload_len > sigurdos::comms::SIGURDOS_COMPANION_CHANNEL_DATA_MAX_PAYLOAD) {
            return false;
        }
        if (payload_len > 0 && !payload) return false;
        return g_mesh->sendGroupDataToChannel(channel_index, path, path_len,
                                              data_type, payload, (int)payload_len);
    }

    bool sendAdvert(bool flood) override {
        if (!sigurdos_mesh_radio_tx_allowed()) return false;
        if (flood) {
            // Flood-scoped advert via existing broadcast path
            return sigurdos::mesh::sendAdvert();
        }
        // Zero-hop self advert — the upstream companion semantics
        if (!g_mesh) return false;
        const sigurdos::NodePrefs& p = sigurdos::prefs_get();
        ::mesh::Packet* pkt;
        if (p.share_location && sigurdos_gps_has_fix()) {
            pkt = g_mesh->createSelfAdvert(own_name,
                (double)sigurdos_gps_latitude(),
                (double)sigurdos_gps_longitude());
        } else if (p.share_location && p.advert_location_valid) {
            pkt = g_mesh->createSelfAdvert(own_name,
                (double)p.advert_lat / 1000000.0,
                (double)p.advert_lon / 1000000.0);
        } else {
            pkt = g_mesh->createSelfAdvert(own_name);
        }
        if (!pkt) return false;
        g_mesh->sendZeroHop(pkt);
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

        sigurdos::NodePrefs p = sigurdos::prefs_get();
        const int8_t tx_power = (p.tx_power_dbm >= -9 && p.tx_power_dbm <= 22)
            ? p.tx_power_dbm
            : LORA_TX_PWR;
        const float freq = (float)freq_khz / 1000.0f;
        const float bw = (float)bw_hz / 1000.0f;
        if (!sigurdos::mesh::applyRadioParams(freq, bw, sf, cr,
                                              tx_power, p.rx_boosted_gain)) {
            return false;
        }

        p.configured = true;
        p.freq = freq;
        p.bw = bw;
        p.sf = sf;
        p.cr = cr;
        p.tx_power_dbm = (int8_t)tx_power;
        p.client_repeat = client_repeat;
        sigurdos::prefs_set(p);
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
        if (!g_mesh || !out64) return false;
        return g_mesh->self_id.writeTo(out64, PRV_KEY_SIZE) >= PRV_KEY_SIZE;
    }

    bool importPrivateKey(const uint8_t* key64) override {
        if (!g_mesh || !key64) return false;
        if (!::mesh::LocalIdentity::validatePrivateKey(key64)) return false;
        g_mesh->self_id.readFrom(key64, PRV_KEY_SIZE);
        saveIdentity(g_mesh->self_id);
        // Invalidate ECDH shared secrets derived from the old identity and
        // reload persisted contacts with the new identity (matches upstream
        // MyMesh::CMD_IMPORT_PRIVATE_KEY — resetContacts + loadContacts).
        g_mesh->resetAllContacts();
        sigurdos::mesh::loadContacts();
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
        if (!g_mesh || !pub_key) return false;
        ::ContactInfo* c = g_mesh->lookupContactByPubKey(
            pub_key, (int)sigurdos::comms::SIGURDOS_COMPANION_PUB_KEY_SIZE);
        if (!c) return false;
        companionContactFromInfo(*c, out);
        return true;
    }
    bool addOrUpdateContact(const CompanionContact& c) override {
        if (!g_mesh) return false;
        ::ContactInfo* existing = g_mesh->lookupContactByPubKey((const uint8_t*)c.pub_key, 32);
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
        else ok = g_mesh->addContact(ci);
        if (ok) sigurdos::mesh::saveContacts();
        return ok;
    }
    bool removeContactByPubKey(const uint8_t* pub_key) override {
        if (!g_mesh || !pub_key) return false;
        ::ContactInfo* c = g_mesh->lookupContactByPubKey(pub_key, 32);
        if (!c) return false;
        bool ok = g_mesh->BaseChatMesh::removeContact(*c);
        if (ok) sigurdos::mesh::saveContacts();
        return ok;
    }
    bool resetPathByPubKey(const uint8_t* pub_key) override {
        if (!g_mesh || !pub_key) return false;
        ::ContactInfo* c = g_mesh->lookupContactByPubKey(pub_key, 32);
        if (!c) return false;
        g_mesh->BaseChatMesh::resetPathTo(*c);
        sigurdos::mesh::saveContacts();
        return true;
    }
    bool shareContactByPubKey(const uint8_t* pub_key) override {
        if (!g_mesh || !pub_key) return false;
        ::ContactInfo* c = g_mesh->lookupContactByPubKey(pub_key, 32);
        if (!c) return false;
        return g_mesh->shareContactZeroHop(*c);
    }
    int exportContactByPubKey(const uint8_t* pub_key, uint8_t* out, size_t out_cap) override {
        if (!g_mesh || !out) return 0;
        if (!pub_key) return g_mesh->exportSelfContact(own_name, out, out_cap);
        ::ContactInfo* c = g_mesh->lookupContactByPubKey(pub_key, 32);
        if (!c) return 0;
        return g_mesh->exportContactBounded(*c, out, out_cap);
    }
    bool importContact(const uint8_t* data, size_t len) override {
        if (!g_mesh || !data) return false;
        bool ok = g_mesh->importContact(data, (uint8_t)len);
        if (ok) sigurdos::mesh::saveContacts();
        return ok;
    }
    bool hasConnectionTo(const uint8_t* pub_key) const override {
        return g_mesh && pub_key && g_mesh->companionHasConnection(pub_key);
    }
    void logout(const uint8_t* pub_key) override {
        if (g_mesh && pub_key) g_mesh->companionStopConnection(pub_key);
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
        s.noise_floor = (int16_t)sigurdos::mesh::getNoiseFloor();
        s.last_rssi = (int8_t)(radio_driver ? radio_driver->getLastRSSI() : 0);
        s.last_snr_quarters = (int8_t)((radio_driver ? radio_driver->getLastSNR() : 0.0f) * 4.0f);
        s.tx_air_secs = (uint32_t)(sigurdos::mesh::getTotalTxAirtimeMs() / 1000);
        s.rx_air_secs = (uint32_t)(sigurdos::mesh::getTotalRxAirtimeMs() / 1000);
    }
    void packetStats(CompanionPacketStats& s) const override {
        s.recv = radio_driver ? radio_driver->getPacketsRecv() : 0;
        s.sent = radio_driver ? radio_driver->getPacketsSent() : 0;
        s.sent_flood = sigurdos::mesh::getNumSentFlood();
        s.sent_direct = sigurdos::mesh::getNumSentDirect();
        s.recv_flood = sigurdos::mesh::getNumRecvFlood();
        s.recv_direct = sigurdos::mesh::getNumRecvDirect();
        s.recv_errors = radio_driver ? radio_driver->getPacketsRecvErrors() : 0;
    }
    size_t allowedRepeatFreqRanges(uint32_t* pairs, size_t max_pairs) const override {
        // Return the actual supported frequency range(s) for client-repeat.
        // For SX1262 on T-Deck: 150 MHz – 960 MHz (usable range).
        if (pairs && max_pairs >= 1) {
            pairs[0] = 150000;   // 150 MHz in kHz (lower)
            pairs[1] = 960000;   // 960 MHz in kHz (upper)
            return 1;            // 1 range pair
        }
        return 0;
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
                // Hex-decode the stored key
                for (int i = 0; i < 16; i++) {
                    char hi = p.default_scope_key_hex[i * 2];
                    char lo = p.default_scope_key_hex[i * 2 + 1];
                    uint8_t b = 0;
                    if (hi >= '0' && hi <= '9') b = (hi - '0') << 4;
                    else if (hi >= 'a' && hi <= 'f') b = (hi - 'a' + 10) << 4;
                    else if (hi >= 'A' && hi <= 'F') b = (hi - 'A' + 10) << 4;
                    if (lo >= '0' && lo <= '9') b |= (lo - '0');
                    else if (lo >= 'a' && lo <= 'f') b |= (lo - 'a' + 10);
                    else if (lo >= 'A' && lo <= 'F') b |= (lo - 'A' + 10);
                    key_out[i] = b;
                }
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
            static const char hex[] = "0123456789abcdef";
            for (int i = 0; i < 16; i++) {
                p.default_scope_key_hex[i * 2] = hex[key[i] >> 4];
                p.default_scope_key_hex[i * 2 + 1] = hex[key[i] & 0x0F];
            }
            p.default_scope_key_hex[32] = '\0';
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
        if (!g_mesh) return;
        if (unscoped) g_mesh->clearActiveScope();
        else if (key) g_mesh->setActiveScope(key);
        else g_mesh->clearActiveScope();
    }

    // ── Async requests ───────────────────────────────────────
    CompanionSendResult sendLogin(const uint8_t* pub_key, const char* password) override {
        CompanionSendResult r{};
        if (!sigurdos_mesh_radio_tx_allowed()) return r;
        if (!g_mesh || !pub_key) return r;
        ::ContactInfo* c = g_mesh->lookupContactByPubKey(pub_key, 32);
        if (!c) return r;
        uint32_t est_timeout = 0;
        int res = g_mesh->sendLoginCompanion(*c, password ? password : "", est_timeout);
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
    CompanionSendResult sendTracePath(uint32_t tag, uint32_t auth, uint8_t flags,
                                      const uint8_t* path, uint8_t path_len) override {
        CompanionSendResult r{};
        if (!sigurdos_mesh_radio_tx_allowed()) return r;
        if (!g_mesh) return r;
        uint32_t est_timeout = 0;
        if (!g_mesh->sendTracePathRaw(tag, auth, flags, path, path_len, est_timeout)) return r;
        r.ok = true;
        r.sent_flood = false;
        r.expected_ack = tag;
        r.est_timeout = est_timeout;
        return r;
    }
    CompanionSendResult sendPathDiscovery(const uint8_t* pub_key) override {
        CompanionSendResult r{};
        if (!sigurdos_mesh_radio_tx_allowed()) return r;
        if (!g_mesh || !pub_key) return r;
        ::ContactInfo* c = g_mesh->lookupContactByPubKey(pub_key, 32);
        if (!c) return r;
        uint32_t tag = g_mesh->sendPathDiscovery(c->name);
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
                p.gps_interval = (uint16_t)iv;
                sigurdos::prefs_set(p);
                return true;
            }
        }
        return false;
    }
    int signData(const uint8_t* data, size_t len, uint8_t* sig_out) override {
        if (!g_mesh || !sig_out) return 0;
        g_mesh->self_id.sign(sig_out, data, len);
        return (int)sigurdos::comms::SIGURDOS_COMPANION_SIGNATURE_SIZE;
    }
    uint8_t getAdvertPath(const uint8_t* pub_key,
                          uint8_t* path_out, uint8_t max_path,
                          uint32_t* timestamp_out) const override {
        if (!g_mesh || !pub_key) return 0;
        const auto* entry = g_mesh->getAdvertPathByKey(pub_key);
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
        if (!sigurdos_mesh_radio_tx_allowed()) return r;
        if (!g_mesh || !pub_key) return r;
        ::ContactInfo* c = g_mesh->lookupContactByPubKey(pub_key, 32);
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
            if (g_mesh->_pending_reqs[i].in_use &&
                g_mesh->_pending_reqs[i].req_type == want &&
                strcmp(g_mesh->_pending_reqs[i].dest_name, name) == 0) {
                r.expected_ack = g_mesh->_pending_reqs[i].tag;
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
// up the full contact via g_mesh where the bridge needs a contact frame.

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

void sigurdos::mesh::mesh_v2_companion_trace_push(uint32_t tag, uint32_t auth, uint8_t flags,
                                                  const uint8_t* path_hashes,
                                                  const uint8_t* path_snrs,
                                                  uint8_t path_len, int8_t final_snr_quarters)
{
    if (g_companion_bridge_ptr)
        g_companion_bridge_ptr->pushTraceData(tag, auth, flags, path_hashes,
                                              path_snrs, path_len, final_snr_quarters);
}
