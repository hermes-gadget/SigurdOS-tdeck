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
using sigurdos::comms::CompanionOtherParams;
using sigurdos::comms::CompanionCoreStats;
using sigurdos::comms::CompanionRadioStats;
using sigurdos::comms::CompanionPacketStats;

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

        char conversation[sigurdos::mesh::SIGURDOS_MSG_CONVERSATION_LEN];
        static constexpr int kDmConversationNameMax =
            (int)sigurdos::mesh::SIGURDOS_MSG_CONVERSATION_LEN - 5;
        snprintf(conversation, sizeof(conversation), "DM: %.*s",
                 kDmConversationNameMax, contact->name);
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

    void setOtherParams(const CompanionOtherParams& op) override {
        sigurdos::NodePrefs p = sigurdos::prefs_get();
        // SigurdOS has no manual_add_contacts / telemetry-mode store; only the
        // fields with a backing pref are applied.
        if (op.loc_policy_present) p.share_location = (op.advert_loc_policy != 0);
        if (op.multi_acks_present) p.multi_acks = (op.multi_acks != 0);
        sigurdos::prefs_set(p);
    }
    bool setPathHashMode(uint8_t mode) override {
        // SigurdOS reports path_hash_mode 0 and does not vary it.
        return mode == 0;
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
        return (int)g_mesh->exportContact(*c, out);
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
    size_t allowedRepeatFreqRanges(uint32_t*, size_t) const override {
        return 0;  // SigurdOS does not gate client-repeat by frequency
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
        if (key_out) memcpy(key_out, keys[0].key, 16);
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

        // If a key was provided for a $private region, store it via TransportKeyStore
        // (The companion app sends the raw key bytes; we trust it)
        if (key && name[0] == '$') {
            RegionMap* map = sigurdos::mesh::getRegionMap();
            if (map && r) {
                TransportKey tk;
                memcpy(tk.key, key, 16);
                // Note: TransportKeyStore::putKey would be needed here
                // For now, we rely on the companion app re-sending the key
            }
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
        if (!g_mesh) return r;
        uint32_t est_timeout = 0;
        if (!g_mesh->sendTracePathRaw(tag, auth, flags, path, path_len, est_timeout)) return r;
        r.ok = true;
        r.sent_flood = false;
        r.expected_ack = tag;
        r.est_timeout = est_timeout;
        return r;
    }
    void selfTelemetry(uint8_t* out, size_t* out_len) const override {
        // SigurdOS has no companion telemetry-blob builder yet; report empty.
        (void)out;
        if (out_len) *out_len = 0;
    }
    int signData(const uint8_t* data, size_t len, uint8_t* sig_out) override {
        if (!g_mesh || !sig_out) return 0;
        g_mesh->self_id.sign(sig_out, data, len);
        return (int)sigurdos::comms::SIGURDOS_COMPANION_SIGNATURE_SIZE;
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
