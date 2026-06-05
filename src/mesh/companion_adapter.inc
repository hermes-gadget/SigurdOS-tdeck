// Companion BLE bridge adapter for mesh_wrapper.cpp.
// This file is intentionally included from mesh_wrapper.cpp so it can access
// the existing singletons without widening the public mesh API.

#include <new>
#include <esp_heap_caps.h>

static void saveIdentity(::mesh::LocalIdentity& id);

using sigurdos::comms::CompanionBridge;
using sigurdos::comms::CompanionBridgeHost;
using sigurdos::comms::CompanionChannel;
using sigurdos::comms::CompanionContact;
using sigurdos::comms::CompanionSelfInfo;
using sigurdos::comms::CompanionSendResult;

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
#endif

static constexpr uint32_t SIGURDOS_DEFAULT_BLE_PIN = 123456;

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
                                             uint8_t path_len = 0xFF)
{
    sigurdos::mesh::StoredMessage msg{};
    const bool is_channel = channel && channel[0];
    if (is_channel) {
        strncpy(msg.conversation, channel, sizeof(msg.conversation) - 1);
        snprintf(msg.text, sizeof(msg.text), "%s: %s", sender ? sender : "", text ? text : "");
    } else {
        snprintf(msg.conversation, sizeof(msg.conversation), "DM: %s", sender ? sender : "");
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
    fillStoredPrefixForName(sender, msg.sender_prefix);
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

class WrapperCompanionHost final : public CompanionBridgeHost {
public:
    uint32_t blePin() const override {
        uint32_t pin = sigurdos::prefs_get().device_pin;
        return pin ? pin : SIGURDOS_DEFAULT_BLE_PIN;
    }
    uint8_t clientRepeat() const override { return sigurdos::prefs_get().client_repeat; }
    uint8_t pathHashMode() const override { return 0; }

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
        out.telemetry_modes = 0;
        out.manual_add_contacts = 0;
        out.freq_khz = (uint32_t)(p.freq * 1000.0f);
        out.bw_hz = (uint32_t)(p.bw * 1000.0f);
        out.sf = p.sf;
        out.cr = p.cr;
    }

    uint32_t currentTime() const override { return sigurdos::mesh::getCurrentTime(); }
    bool setCurrentTime(uint32_t epoch) override { return sigurdos::mesh::setSystemTime(epoch); }
    uint16_t batteryMilliVolts() const override { return sigurdos_battery_mv(); }
    uint32_t storageUsedKb() const override { return (uint32_t)(SPIFFS.usedBytes() / 1024); }
    uint32_t storageTotalKb() const override { return (uint32_t)(SPIFFS.totalBytes() / 1024); }

    int contactCount() const override { return g_mesh ? g_mesh->getNumContacts() : 0; }
    bool getContact(int index, CompanionContact& out) const override {
        if (!g_mesh) return false;
        ::ContactInfo c{};
        if (!g_mesh->getContactByIdx((uint32_t)index, c)) return false;
        copyContact(c, out);
        return true;
    }
    bool getContactByPubKeyPrefix(const uint8_t* prefix, size_t prefix_len,
                                  CompanionContact& out) const override {
        if (!g_mesh || !prefix) return false;
        ::ContactInfo* c = g_mesh->lookupContactByPubKey(prefix, (int)prefix_len);
        if (!c) return false;
        copyContact(*c, out);
        return true;
    }

    int channelCount() const override { return g_mesh ? g_mesh->getChannelCount() : 0; }
    bool getChannel(int index, CompanionChannel& out) const override {
        if (!g_mesh) return false;
        const ChannelDetails* ch = g_mesh->getChannel(index);
        if (!ch) return false;
        memset(&out, 0, sizeof(out));
        strncpy(out.name, ch->name, sizeof(out.name) - 1);
        memcpy(out.secret, ch->channel.secret, sizeof(out.secret));
        return true;
    }
    bool setChannel(int index, const CompanionChannel& channel) override {
        if (!g_mesh || index < 0 || index >= MAX_GROUP_CHANNELS || !channel.name[0]) return false;
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
        if (!g_mesh || !prefix || !text) return result;
        ::ContactInfo* contact = g_mesh->lookupContactByPubKey(prefix, (int)prefix_len);
        if (!contact) return result;

        uint32_t ts = timestamp ? timestamp : rtc_clock.getCurrentTime();
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

        char conversation[32];
        snprintf(conversation, sizeof(conversation), "DM: %s", contact->name);
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
        if (!g_mesh || payload_len > sigurdos::comms::SIGURDOS_COMPANION_CHANNEL_DATA_MAX_PAYLOAD) {
            return false;
        }
        if (payload_len > 0 && !payload) return false;
        return g_mesh->sendGroupDataToChannel(channel_index, path, path_len,
                                              data_type, payload, (int)payload_len);
    }

    bool sendAdvert(bool flood) override {
        (void)flood;
        return sigurdos::mesh::sendAdvert();
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
        if (freq_khz < 400000 || freq_khz > 1000000) return false;
        if (bw_hz < 7800 || bw_hz > 500000) return false;
        if (sf < 6 || sf > 12) return false;
        if (cr < 5 || cr > 8) return false;
        if (client_repeat > 1) return false;

        sigurdos::NodePrefs p = sigurdos::prefs_get();
        const int tx_power = (p.tx_power_dbm >= 2 && p.tx_power_dbm <= 22)
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
        if (tx_power_dbm < 2 || tx_power_dbm > 22) return false;
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

    bool setBlePin(uint32_t pin) override {
        if (pin != 0 && (pin < 1000 || pin > 999999)) return false;
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
        return true;
    }

private:
    static void copyContact(const ::ContactInfo& c, CompanionContact& out) {
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
};

static WrapperCompanionHost g_companion_host;
