#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "comms/companion_bridge.h"
#include "mesh/companion_ble_pin.h"

namespace {

class MockSerial final : public BaseSerialInterface {
public:
    bool enabled = false;
    bool connected = true;
    bool busy = false;
    uint32_t generation = 1;
    int fail_writes = 0;
    std::vector<std::vector<uint8_t>> writes;

    void enable() override { enabled = true; }
    void disable() override { enabled = false; }
    bool isEnabled() const override { return enabled; }
    bool isConnected() const override { return connected; }
    uint32_t connectionGeneration() const override {
        return connected ? generation : 0;
    }
    bool isWriteBusy() const override { return busy; }
    size_t writeFrame(const uint8_t src[], size_t len) override {
        if (!connected || busy || fail_writes > 0) {
            if (fail_writes > 0) --fail_writes;
            return 0;
        }
        writes.emplace_back(src, src + len);
        return len;
    }
    size_t checkRecvFrame(uint8_t dest[]) override {
        (void)dest;
        return 0;
    }
};

class FakeHost final : public sigurdos::comms::CompanionBridgeHost {
public:
    bool sent_dm = false;
    bool sent_binary = false;
    bool sent_anon = false;
    int binary_send_calls = 0;
    int anon_send_calls = 0;
    uint32_t next_binary_tag = 0x10203040u;
    uint32_t next_anon_tag = 0x50607080u;
    uint32_t binary_timeout_ms = 3000;
    uint32_t anon_timeout_ms = 4500;
    bool cancelled_binary = false;
    std::vector<uint32_t> cancelled_binary_tags;
    std::vector<uint8_t> last_binary_data;
    std::vector<uint8_t> last_anon_data;
    uint8_t last_anon_key[32]{};
    bool sent_channel_data = false;
    bool sent_raw_data = false;
    bool sent_control_data = false;
    uint8_t last_prefix[6]{};
    int last_channel_data_index = -1;
    uint8_t last_channel_data_path_len = 0;
    uint16_t last_channel_data_type = 0;
    std::vector<uint8_t> last_channel_data_path;
    std::vector<uint8_t> last_channel_data_payload;
    std::vector<uint8_t> last_raw_path;
    std::vector<uint8_t> last_raw_payload;
    std::vector<uint8_t> last_control_payload;
    bool set_advert_name_called = false;
    char advert_name[32]{};
    bool set_advert_latlon_called = false;
    int32_t advert_lat = 0;
    int32_t advert_lon = 0;
    bool set_radio_params_called = false;
    uint32_t radio_freq_khz = 0;
    uint32_t radio_bw_hz = 0;
    uint8_t radio_sf = 0;
    uint8_t radio_cr = 0;
    uint8_t radio_client_repeat = 0;
    bool set_radio_tx_power_called = false;
    int8_t radio_tx_power_dbm = 0;
    bool set_tuning_params_called = false;
    uint32_t tuning_rx_delay_base_x1000 = 10000;
    uint32_t tuning_tx_delay_factor_x1000 = 1000;
    uint32_t allowed_repeat_ranges[4]{};
    size_t allowed_repeat_range_count = 0;
    uint32_t now = 1234;
    uint32_t now_ms = 0;
    uint8_t self_multi_acks = 0;
    uint8_t self_advert_loc_policy = 0;

    uint8_t path_hash_mode = 0;
    bool advert_path_found = false;
    uint8_t advert_path_descriptor = 0;
    uint32_t advert_path_timestamp = 0;
    std::vector<uint8_t> advert_path_bytes;
    std::string custom_vars;
    bool custom_var_result = false;
    int set_custom_var_calls = 0;
    std::string custom_var_name;
    std::string custom_var_value;
    int set_ble_pin_calls = 0;
    uint32_t last_ble_pin = 0;
    bool set_ble_pin_result = true;
    bool path_discovery_called = false;
    uint8_t path_discovery_key[32] = {};
    sigurdos::comms::CompanionSendResult path_discovery_result{
        false, false, 0, 0};

    uint32_t blePin() const override { return 123456; }
    uint8_t clientRepeat() const override { return 0; }
    uint8_t pathHashMode() const override { return path_hash_mode; }
    void selfInfo(sigurdos::comms::CompanionSelfInfo& out) const override {
        std::memset(&out, 0, sizeof(out));
        for (int i = 0; i < 32; i++) out.pub_key[i] = (uint8_t)i;
        std::strncpy(out.node_name, "SigurdOS", sizeof(out.node_name) - 1);
        out.advert_type = 1;
        out.tx_power_dbm = 22;
        out.max_tx_power_dbm = 22;
        out.freq_khz = 869618;
        out.bw_hz = 62500;
        out.sf = 8;
        out.cr = 5;
        out.multi_acks = self_multi_acks;
        out.advert_loc_policy = self_advert_loc_policy;
    }

    uint32_t currentTime() const override { return now; }
    uint32_t monotonicMillis() const override { return now_ms; }
    bool setCurrentTime(uint32_t epoch) override { now = epoch; return true; }
    uint16_t batteryMilliVolts() const override { return 4100; }
    uint32_t storageUsedKb() const override { return 10; }
    uint32_t storageTotalKb() const override { return 100; }

    int contactCount() const override { return 1; }
    bool getContact(int index, sigurdos::comms::CompanionContact& out) const override {
        if (index != 0) return false;
        std::memset(&out, 0, sizeof(out));
        for (int i = 0; i < 32; i++) out.pub_key[i] = (uint8_t)(0xA0 + i);
        out.type = 1;
        out.out_path_len = 0xFF;
        std::strncpy(out.name, "Alice", sizeof(out.name) - 1);
        out.lastmod = 55;
        return true;
    }
    bool getContactByPubKeyPrefix(const uint8_t* prefix, size_t prefix_len,
                                  sigurdos::comms::CompanionContact& out) const override {
        if (!prefix || prefix_len != 6) return false;
        for (int i = 0; i < 6; i++) {
            if (prefix[i] != (uint8_t)(0xA0 + i)) return false;
        }
        return getContact(0, out);
    }

    int channelCount() const override { return channel_count; }
    bool getChannel(int index, sigurdos::comms::CompanionChannel& out) const override {
        if (index < 0 || index >= channel_count) return false;
        std::memset(&out, 0, sizeof(out));
        if (index == 0) std::strncpy(out.name, "#test", sizeof(out.name) - 1);
        return true;
    }
    bool setChannel(int, const sigurdos::comms::CompanionChannel&) override { return true; }
    int channel_count = 1;

    sigurdos::comms::CompanionSendResult sendTextByPubKeyPrefix(
        const uint8_t* prefix, size_t prefix_len, uint8_t, uint8_t,
        uint32_t, const char*) override {
        sent_dm = true;
        std::memcpy(last_prefix, prefix, prefix_len < 6 ? prefix_len : 6);
        return {true, true, 0x12345678, 900};
    }
    sigurdos::comms::CompanionSendResult sendChannelText(int, uint32_t, const char*) override {
        return {true, true, 0, 0};
    }
    bool sendChannelData(int channel_index, const uint8_t* path, uint8_t path_len,
                         uint16_t data_type, const uint8_t* payload,
                         size_t payload_len) override {
        sent_channel_data = true;
        last_channel_data_index = channel_index;
        last_channel_data_path_len = path_len;
        last_channel_data_type = data_type;
        last_channel_data_path.clear();
        last_channel_data_payload.clear();
        if (path && path_len != 0xFF) {
            size_t path_bytes = (size_t)(path_len & 63) * (size_t)((path_len >> 6) + 1);
            last_channel_data_path.assign(path, path + path_bytes);
        }
        if (payload && payload_len > 0) {
            last_channel_data_payload.assign(payload, payload + payload_len);
        }
        return channel_index == 0;
    }
    bool sendRawData(const uint8_t* path, uint8_t path_len,
                     const uint8_t* payload, size_t payload_len) override {
        sent_raw_data = true;
        last_raw_path.clear();
        last_raw_payload.clear();
        if (path && path_len > 0) last_raw_path.assign(path, path + path_len);
        if (payload && payload_len > 0) {
            last_raw_payload.assign(payload, payload + payload_len);
        }
        return last_send_ok;
    }
    bool sendControlData(const uint8_t* payload, size_t payload_len) override {
        sent_control_data = true;
        last_control_payload.clear();
        if (payload && payload_len > 0) {
            last_control_payload.assign(payload, payload + payload_len);
        }
        return last_send_ok;
    }
    bool sendAdvert(bool) override { return true; }
    bool setAdvertName(const char* name) override {
        set_advert_name_called = true;
        if (!name || !name[0]) return false;
        std::strncpy(advert_name, name, sizeof(advert_name) - 1);
        advert_name[sizeof(advert_name) - 1] = '\0';
        std::strncpy(last_advert_name, name, sizeof(last_advert_name) - 1);
        last_advert_name[sizeof(last_advert_name) - 1] = '\0';
        return true;
    }
    bool setAdvertLatLon(int32_t lat, int32_t lon) override {
        set_advert_latlon_called = true;
        advert_lat = lat;
        advert_lon = lon;
        last_lat = lat;
        last_lon = lon;
        return lat >= -90000000 && lat <= 90000000 &&
               lon >= -180000000 && lon <= 180000000;
    }
    bool setRadioParams(uint32_t freq_khz, uint32_t bw_hz, uint8_t sf,
                        uint8_t cr, uint8_t client_repeat) override {
        set_radio_params_called = true;
        radio_freq_khz = freq_khz;
        radio_bw_hz = bw_hz;
        radio_sf = sf;
        radio_cr = cr;
        radio_client_repeat = client_repeat;
        last_freq_khz = freq_khz;
        last_bw_hz = bw_hz;
        last_sf = sf;
        last_cr = cr;
        last_repeat = client_repeat;
        return radio_params_ok &&
               freq_khz >= 400000 && freq_khz <= 1000000 &&
               bw_hz >= 7800 && bw_hz <= 500000 &&
               sf >= 6 && sf <= 12 &&
               cr >= 5 && cr <= 8 &&
               client_repeat <= 1;
    }
    bool setRadioTxPower(int8_t tx_power_dbm) override {
        set_radio_tx_power_called = true;
        radio_tx_power_dbm = tx_power_dbm;
        last_tx_power = tx_power_dbm;
        return tx_power_dbm >= 2 && tx_power_dbm <= 22;
    }
    void tuningParams(uint32_t& rx_delay_base_x1000,
                      uint32_t& airtime_factor_x1000) const override {
        rx_delay_base_x1000 = tuning_rx_delay_base_x1000;
        airtime_factor_x1000 = tuning_tx_delay_factor_x1000;
    }
    bool setTuningParams(uint32_t rx_delay_base_x1000,
                         uint32_t airtime_factor_x1000) override {
        set_tuning_params_called = true;
        tuning_rx_delay_base_x1000 = rx_delay_base_x1000;
        tuning_tx_delay_factor_x1000 = airtime_factor_x1000;
        last_rx_base = rx_delay_base_x1000;
        last_airtime = airtime_factor_x1000;
        return rx_delay_base_x1000 <= 20000 && airtime_factor_x1000 <= 99000;
    }
    bool setBlePin(uint32_t pin) override {
        set_ble_pin_calls++;
        last_ble_pin = pin;
        return set_ble_pin_result;
    }
    bool exportPrivateKey(uint8_t* out64) const override {
        std::memset(out64, 0x42, 64);
        return true;
    }
    bool importPrivateKey(const uint8_t*) override { return true; }

    // ── Recording state for the extended command surface ──────
    uint32_t last_freq_khz = 0, last_bw_hz = 0;
    uint8_t  last_sf = 0, last_cr = 0, last_repeat = 0;
    int8_t   last_tx_power = 0;
    uint32_t last_rx_base = 0, last_airtime = 0;
    sigurdos::comms::CompanionOtherParams last_other{};
    uint8_t  autoadd_cfg = 0x1E, autoadd_hops = 5;
    bool     radio_params_ok = true;
    char     last_advert_name[32]{};
    int32_t  last_lat = 0, last_lon = 0;
    bool     contact_found = true;
    bool     add_contact_ok = true;
    bool     add_contact_called = false;
    sigurdos::comms::CompanionContact last_added{};
    bool     has_connection = false;
    bool     rebooted = false, factory_reset_called = false;
    bool     scope_is_set = false;
    char     last_scope_name[32]{};
    bool     scope_unscoped = false, scope_cleared = false;
    bool     scope_write_result = true, scope_override_result = true;
    bool     scope_key_present = false;
    uint8_t  last_scope_key[16]{};
    bool     last_send_ok = true;
    uint32_t last_trace_tag = 0; uint8_t last_trace_path_len = 0;
    int      sign_len_seen = -1;

    void setOtherParams(const sigurdos::comms::CompanionOtherParams& p) override { last_other = p; }
    bool setPathHashMode(uint8_t mode) override {
        if (mode > 2) return false;
        path_hash_mode = mode;
        return true;
    }
    void getAutoAddConfig(uint8_t* cfg, uint8_t* hops) const override {
        if (cfg) *cfg = autoadd_cfg; if (hops) *hops = autoadd_hops;
    }
    void setAutoAddConfig(uint8_t cfg, uint8_t hops) override { autoadd_cfg = cfg; autoadd_hops = hops; }
    int8_t maxTxPowerDbm() const override { return 22; }
    bool getContactByPubKey(const uint8_t* pub_key, sigurdos::comms::CompanionContact& out) const override {
        if (!contact_found) return false;
        std::memset(&out, 0, sizeof(out));
        std::memcpy(out.pub_key, pub_key, 32);
        std::strncpy(out.name, "Found", sizeof(out.name) - 1);
        return true;
    }
    bool addOrUpdateContact(const sigurdos::comms::CompanionContact& c) override {
        add_contact_called = true; last_added = c; return add_contact_ok;
    }
    bool removeContactByPubKey(const uint8_t*) override { return contact_found; }
    bool resetPathByPubKey(const uint8_t*) override { return contact_found; }
    bool shareContactByPubKey(const uint8_t*) override { return contact_found; }
    int  exportContactByPubKey(const uint8_t* pub_key, uint8_t* out, size_t cap) override {
        if (!pub_key) { if (cap >= 3) { out[0] = 1; out[1] = 2; out[2] = 3; return 3; } return 0; }
        if (!contact_found) return 0;
        if (cap >= 2) { out[0] = 0xAA; out[1] = 0xBB; return 2; }
        return 0;
    }
    bool importContact(const uint8_t*, size_t) override { return contact_found; }
    bool hasConnectionTo(const uint8_t*) const override { return has_connection; }
    void logout(const uint8_t*) override {}
    void reboot() override { rebooted = true; }
    bool factoryReset() override { factory_reset_called = true; return true; }
    void coreStats(sigurdos::comms::CompanionCoreStats& s) const override {
        s.batt_mv = 4100; s.uptime_secs = 1234; s.err_flags = 0; s.queue_len = 2;
    }
    void radioStats(sigurdos::comms::CompanionRadioStats& s) const override {
        s.noise_floor = -120; s.last_rssi = -80; s.last_snr_quarters = -8;
        s.tx_air_secs = 10; s.rx_air_secs = 20;
    }
    void packetStats(sigurdos::comms::CompanionPacketStats& s) const override {
        s.recv = 100; s.sent = 50; s.sent_flood = 5; s.sent_direct = 45;
        s.recv_flood = 60; s.recv_direct = 40; s.recv_errors = 3;
    }
    size_t allowedRepeatFreqRanges(uint32_t* pairs, size_t max_pairs) const override {
        const size_t count = allowed_repeat_range_count < max_pairs
            ? allowed_repeat_range_count
            : max_pairs;
        if (pairs && count > 0) {
            std::memcpy(pairs, allowed_repeat_ranges,
                        count * 2 * sizeof(uint32_t));
        }
        return count;
    }
    bool getDefaultFloodScope(char* name, uint8_t* key) const override {
        if (!scope_is_set) return false;
        if (name) { std::memset(name, 0, 31); std::strncpy(name, "#test", 30); }
        if (key) std::memset(key, 0x7E, 16);
        return true;
    }
    bool setDefaultFloodScope(const char* name, const uint8_t* key) override {
        if (!name || !name[0]) { scope_cleared = true; last_scope_name[0] = '\0'; }
        else std::strncpy(last_scope_name, name, sizeof(last_scope_name) - 1);
        scope_key_present = key != nullptr;
        if (key) std::memcpy(last_scope_key, key, sizeof(last_scope_key));
        return scope_write_result;
    }
    bool setFloodScopeOverride(const uint8_t* key, bool unscoped) override {
        scope_unscoped = unscoped; scope_cleared = (!unscoped && !key);
        scope_key_present = key != nullptr;
        if (key) std::memcpy(last_scope_key, key, sizeof(last_scope_key));
        return scope_override_result;
    }
    sigurdos::comms::CompanionSendResult sendLogin(const uint8_t*, const char*) override {
        return {last_send_ok, true, 0x11223344u, 5000};
    }
    sigurdos::comms::CompanionSendResult sendStatusReq(const uint8_t*) override {
        return {last_send_ok, false, 0xAAu, 3000};
    }
    sigurdos::comms::CompanionSendResult sendTelemetryReq(const uint8_t*) override {
        return {last_send_ok, false, 0xBBu, 3000};
    }
    sigurdos::comms::CompanionSendResult sendBinaryReq(
        const uint8_t*, const uint8_t* data, uint8_t data_len) override {
        sent_binary = true;
        ++binary_send_calls;
        last_binary_data.assign(data, data + data_len);
        if (!last_send_ok) return {false, false, 0, 0};
        return {true, false, next_binary_tag++, binary_timeout_ms};
    }
    sigurdos::comms::CompanionSendResult sendAnonReq(
        const uint8_t* pub_key, const uint8_t* data, uint8_t data_len) override {
        sent_anon = true;
        ++anon_send_calls;
        std::memcpy(last_anon_key, pub_key, sizeof(last_anon_key));
        last_anon_data.assign(data, data + data_len);
        if (!last_send_ok) return {false, false, 0, 0};
        return {true, true, next_anon_tag++, anon_timeout_ms};
    }
    void cancelBinaryReq(uint32_t tag) override { cancelled_binary_tags.push_back(tag); }
    void cancelBinaryReqs() override { cancelled_binary = true; }
    sigurdos::comms::CompanionSendResult sendTracePath(uint32_t tag, uint32_t, uint8_t,
                                                       const uint8_t*, uint8_t path_len) override {
        last_trace_tag = tag; last_trace_path_len = path_len;
        return {last_send_ok, false, tag, 4000};
    }
    void selfTelemetry(uint8_t*, size_t* out_len) const override { if (out_len) *out_len = 0; }
    int getCustomVars(char* out, size_t out_cap) const override {
        if (custom_vars.size() > out_cap) return 0;
        if (!custom_vars.empty()) std::memcpy(out, custom_vars.data(), custom_vars.size());
        return (int)custom_vars.size();
    }
    bool setCustomVar(const char* name, const char* value) override {
        ++set_custom_var_calls;
        custom_var_name = name ? name : "";
        custom_var_value = value ? value : "";
        return custom_var_result;
    }
    int signData(const uint8_t*, size_t len, uint8_t* sig_out) override {
        sign_len_seen = (int)len;
        std::memset(sig_out, 0xAB, 64);
        return 64;
    }
    sigurdos::comms::CompanionSendResult sendPathDiscovery(
        const uint8_t* pub_key) override {
        path_discovery_called = true;
        std::memcpy(path_discovery_key, pub_key, sizeof(path_discovery_key));
        return path_discovery_result;
    }
    bool getAdvertPath(const uint8_t*, uint8_t* path_out, size_t path_capacity,
                       uint8_t* descriptor_out, size_t* path_bytes_out,
                       uint32_t* timestamp_out) const override {
        if (!advert_path_found || advert_path_bytes.size() > path_capacity) return false;
        if (!advert_path_bytes.empty() && !path_out) return false;
        if (!advert_path_bytes.empty()) {
            std::memcpy(path_out, advert_path_bytes.data(), advert_path_bytes.size());
        }
        if (descriptor_out) *descriptor_out = advert_path_descriptor;
        if (path_bytes_out) *path_bytes_out = advert_path_bytes.size();
        if (timestamp_out) *timestamp_out = advert_path_timestamp;
        return true;
    }
};

class CompanionProtocolTest : public ::testing::Test {
protected:
    char store_path[128]{};
    MockSerial serial;
    FakeHost host;
    sigurdos::comms::CompanionBridge bridge;

    void SetUp() override {
        std::snprintf(store_path, sizeof(store_path),
                      "/tmp/sigurdos_companion_protocol_%d.bin",
                      ::testing::UnitTest::GetInstance()->random_seed());
        sigurdos::mesh::messageStoreSetNativePath(store_path);
        std::remove(store_path);
        ASSERT_TRUE(sigurdos::mesh::messageStoreBegin());
        ASSERT_TRUE(sigurdos::mesh::messageStoreClear());
        bridge.begin(&serial, &host);
        serial.writes.clear();
    }

    void TearDown() override {
        std::remove(store_path);
    }
};

std::vector<uint8_t> binaryRequestFrame(std::initializer_list<uint8_t> payload)
{
    std::vector<uint8_t> frame{sigurdos::comms::CMD_SEND_BINARY_REQ};
    for (int i = 0; i < 32; ++i) frame.push_back((uint8_t)(0xA0 + i));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

std::vector<uint8_t> anonRequestFrame(std::initializer_list<uint8_t> payload)
{
    std::vector<uint8_t> frame{sigurdos::comms::CMD_SEND_ANON_REQ};
    for (int i = 0; i < 32; ++i) frame.push_back((uint8_t)(0xC0 + i));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

TEST(CompanionProtocolConstants, AsyncPushCodesMatchPinnedStockProtocol)
{
    namespace cc = sigurdos::comms;
    EXPECT_EQ(cc::PUSH_CODE_LOG_RX_DATA, 0x88);
    EXPECT_EQ(cc::PUSH_CODE_TRACE_DATA, 0x89);
    EXPECT_EQ(cc::PUSH_CODE_NEW_ADVERT, 0x8A);
    EXPECT_EQ(cc::PUSH_CODE_TELEMETRY_RESPONSE, 0x8B);
    EXPECT_EQ(cc::PUSH_CODE_BINARY_RESPONSE, 0x8C);
    EXPECT_EQ(cc::PUSH_CODE_PATH_DISCOVERY_RESPONSE, 0x8D);
    EXPECT_EQ(cc::PUSH_CODE_CONTROL_DATA, 0x8E);
}

TEST_F(CompanionProtocolTest, ResponseAndPushCodesMatchPinnedMeshCore) {
    namespace cc = sigurdos::comms;
    struct CodeCheck {
        const char* name;
        uint8_t actual;
        uint8_t expected;
    };
    const CodeCheck checks[] = {
        {"RESP_CODE_OK", cc::RESP_CODE_OK, 0},
        {"RESP_CODE_ERR", cc::RESP_CODE_ERR, 1},
        {"RESP_CODE_CONTACTS_START", cc::RESP_CODE_CONTACTS_START, 2},
        {"RESP_CODE_CONTACT", cc::RESP_CODE_CONTACT, 3},
        {"RESP_CODE_END_OF_CONTACTS", cc::RESP_CODE_END_OF_CONTACTS, 4},
        {"RESP_CODE_SELF_INFO", cc::RESP_CODE_SELF_INFO, 5},
        {"RESP_CODE_SENT", cc::RESP_CODE_SENT, 6},
        {"RESP_CODE_CONTACT_MSG_RECV", cc::RESP_CODE_CONTACT_MSG_RECV, 7},
        {"RESP_CODE_CHANNEL_MSG_RECV", cc::RESP_CODE_CHANNEL_MSG_RECV, 8},
        {"RESP_CODE_CURR_TIME", cc::RESP_CODE_CURR_TIME, 9},
        {"RESP_CODE_NO_MORE_MESSAGES", cc::RESP_CODE_NO_MORE_MESSAGES, 10},
        {"RESP_CODE_EXPORT_CONTACT", cc::RESP_CODE_EXPORT_CONTACT, 11},
        {"RESP_CODE_BATT_AND_STORAGE", cc::RESP_CODE_BATT_AND_STORAGE, 12},
        {"RESP_CODE_DEVICE_INFO", cc::RESP_CODE_DEVICE_INFO, 13},
        {"RESP_CODE_PRIVATE_KEY", cc::RESP_CODE_PRIVATE_KEY, 14},
        {"RESP_CODE_DISABLED", cc::RESP_CODE_DISABLED, 15},
        {"RESP_CODE_CONTACT_MSG_RECV_V3", cc::RESP_CODE_CONTACT_MSG_RECV_V3, 16},
        {"RESP_CODE_CHANNEL_MSG_RECV_V3", cc::RESP_CODE_CHANNEL_MSG_RECV_V3, 17},
        {"RESP_CODE_CHANNEL_INFO", cc::RESP_CODE_CHANNEL_INFO, 18},
        {"RESP_CODE_SIGN_START", cc::RESP_CODE_SIGN_START, 19},
        {"RESP_CODE_SIGNATURE", cc::RESP_CODE_SIGNATURE, 20},
        {"RESP_CODE_CUSTOM_VARS", cc::RESP_CODE_CUSTOM_VARS, 21},
        {"RESP_CODE_ADVERT_PATH", cc::RESP_CODE_ADVERT_PATH, 22},
        {"RESP_CODE_TUNING_PARAMS", cc::RESP_CODE_TUNING_PARAMS, 23},
        {"RESP_CODE_STATS", cc::RESP_CODE_STATS, 24},
        {"RESP_CODE_AUTOADD_CONFIG", cc::RESP_CODE_AUTOADD_CONFIG, 25},
        {"RESP_ALLOWED_REPEAT_FREQ", cc::RESP_ALLOWED_REPEAT_FREQ, 26},
        {"RESP_CODE_CHANNEL_DATA_RECV", cc::RESP_CODE_CHANNEL_DATA_RECV, 27},
        {"RESP_CODE_DEFAULT_FLOOD_SCOPE", cc::RESP_CODE_DEFAULT_FLOOD_SCOPE, 28},
        {"PUSH_CODE_ADVERT", cc::PUSH_CODE_ADVERT, 0x80},
        {"PUSH_CODE_PATH_UPDATED", cc::PUSH_CODE_PATH_UPDATED, 0x81},
        {"PUSH_CODE_SEND_CONFIRMED", cc::PUSH_CODE_SEND_CONFIRMED, 0x82},
        {"PUSH_CODE_MSG_WAITING", cc::PUSH_CODE_MSG_WAITING, 0x83},
        {"PUSH_CODE_RAW_DATA", cc::PUSH_CODE_RAW_DATA, 0x84},
        {"PUSH_CODE_LOGIN_SUCCESS", cc::PUSH_CODE_LOGIN_SUCCESS, 0x85},
        {"PUSH_CODE_LOGIN_FAIL", cc::PUSH_CODE_LOGIN_FAIL, 0x86},
        {"PUSH_CODE_STATUS_RESPONSE", cc::PUSH_CODE_STATUS_RESPONSE, 0x87},
        {"PUSH_CODE_LOG_RX_DATA", cc::PUSH_CODE_LOG_RX_DATA, 0x88},
        {"PUSH_CODE_TRACE_DATA", cc::PUSH_CODE_TRACE_DATA, 0x89},
        {"PUSH_CODE_NEW_ADVERT", cc::PUSH_CODE_NEW_ADVERT, 0x8A},
        {"PUSH_CODE_TELEMETRY_RESPONSE", cc::PUSH_CODE_TELEMETRY_RESPONSE, 0x8B},
        {"PUSH_CODE_BINARY_RESPONSE", cc::PUSH_CODE_BINARY_RESPONSE, 0x8C},
        {"PUSH_CODE_PATH_DISCOVERY_RESPONSE", cc::PUSH_CODE_PATH_DISCOVERY_RESPONSE, 0x8D},
        {"PUSH_CODE_CONTROL_DATA", cc::PUSH_CODE_CONTROL_DATA, 0x8E},
        {"PUSH_CODE_CONTACT_DELETED", cc::PUSH_CODE_CONTACT_DELETED, 0x8F},
        {"PUSH_CODE_CONTACTS_FULL", cc::PUSH_CODE_CONTACTS_FULL, 0x90},
    };

    for (const auto& check : checks) {
        EXPECT_EQ(check.actual, check.expected) << check.name;
    }
}

TEST_F(CompanionProtocolTest, DeviceQueryFrameMatchesOfficialShape) {
    uint8_t query[] = {sigurdos::comms::CMD_DEVICE_QUERY, 3};
    ASSERT_TRUE(bridge.handleFrame(query, sizeof(query)));
    ASSERT_EQ(serial.writes.size(), 1u);
    const auto& out = serial.writes[0];
    // Official companion-radio DEVICE_INFO frame is exactly 82 bytes:
    // code(1) + ver(1) + max_contacts/2(1) + max_channels(1) + ble_pin(4)
    // + build_date(12) + manufacturer(40) + firmware_version(20)
    // + client_repeat(1) + path_hash_mode(1). See MeshCore
    // examples/companion_radio/MyMesh.cpp CMD_DEVICE_QUERY handler.
    ASSERT_EQ(out.size(), 82u);
    EXPECT_EQ(out[0], sigurdos::comms::RESP_CODE_DEVICE_INFO);
    EXPECT_EQ(out[1], sigurdos::comms::SIGURDOS_COMPANION_FIRMWARE_VER_CODE);
    EXPECT_EQ(out[2], MAX_CONTACTS / 2);
    EXPECT_EQ(out[3], 8);
    uint32_t pin = 0;
    std::memcpy(&pin, &out[4], 4);
    EXPECT_EQ(pin, 123456u);
    // Last two bytes: client_repeat (v9+), path_hash_mode (v10+).
    EXPECT_EQ(out[81], host.pathHashMode());
}

TEST_F(CompanionProtocolTest, DeviceQueryReportsConfiguredPathHashMode) {
    host.path_hash_mode = 2;  // 3-byte path hash
    uint8_t query[] = {sigurdos::comms::CMD_DEVICE_QUERY, 3};
    ASSERT_TRUE(bridge.handleFrame(query, sizeof(query)));
    ASSERT_EQ(serial.writes.size(), 1u);
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 82u);
    EXPECT_EQ(out[81], 2);
}

TEST_F(CompanionProtocolTest, DeviceTimeRejectsBackwardUpdates) {
    host.now = 2000;
    uint8_t frame[5] = {sigurdos::comms::CMD_SET_DEVICE_TIME};
    uint32_t requested = 1999;
    std::memcpy(&frame[1], &requested, sizeof(requested));

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    EXPECT_EQ(host.now, 2000u);
    EXPECT_EQ(serial.writes[0], (std::vector<uint8_t>{
        sigurdos::comms::RESP_CODE_ERR, sigurdos::comms::ERR_CODE_ILLEGAL_ARG}));

    serial.writes.clear();
    requested = 2001;
    std::memcpy(&frame[1], &requested, sizeof(requested));
    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    EXPECT_EQ(host.now, 2001u);
    EXPECT_EQ(serial.writes[0],
              (std::vector<uint8_t>{sigurdos::comms::RESP_CODE_OK}));
}

TEST_F(CompanionProtocolTest, CongestedCommandResponseIsRetriedBeforeNextCommand) {
    serial.enabled = true;
    serial.fail_writes = 1;
    const uint8_t first[] = {sigurdos::comms::CMD_SET_ADVERT_NAME,
                             'f', 'i', 'r', 's', 't'};
    ASSERT_TRUE(bridge.handleFrame(first, sizeof(first)));
    EXPECT_TRUE(host.set_advert_name_called);
    EXPECT_STREQ(host.advert_name, "first");
    EXPECT_TRUE(serial.writes.empty());

    host.set_advert_name_called = false;
    const uint8_t second[] = {sigurdos::comms::CMD_SET_ADVERT_NAME,
                              's', 'e', 'c', 'o', 'n', 'd'};
    EXPECT_FALSE(bridge.handleFrame(second, sizeof(second)));
    EXPECT_FALSE(host.set_advert_name_called);

    bridge.loop();
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0],
              (std::vector<uint8_t>{sigurdos::comms::RESP_CODE_OK}));

    ASSERT_TRUE(bridge.handleFrame(second, sizeof(second)));
    EXPECT_TRUE(host.set_advert_name_called);
    EXPECT_STREQ(host.advert_name, "second");
}

TEST_F(CompanionProtocolTest, CongestedLowPriorityPushReportsDrop) {
    serial.busy = true;
    EXPECT_FALSE(bridge.pushContactsFull());
    EXPECT_EQ(bridge.pushDropCount(), 1u);

    serial.busy = false;
    EXPECT_TRUE(bridge.pushContactsFull());
    EXPECT_EQ(bridge.pushDropCount(), 1u);
}

TEST_F(CompanionProtocolTest, DisconnectResetsVersionAndPreencodedOfflineFrames) {
    serial.enabled = true;
    bridge.loop();  // observe the connected session

    const uint8_t legacy_query[] = {sigurdos::comms::CMD_DEVICE_QUERY, 2};
    ASSERT_TRUE(bridge.handleFrame(legacy_query, sizeof(legacy_query)));
    EXPECT_EQ(bridge.appTargetVersion(), 2);

    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "new session", sizeof(msg.text) - 1);
    msg.timestamp = 77;
    for (int i = 0; i < 6; ++i) msg.sender_prefix[i] = (uint8_t)(0xA0 + i);
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg));

    const uint8_t start[8] = {sigurdos::comms::CMD_APP_START};
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    serial.connected = false;
    bridge.loop();
    EXPECT_EQ(bridge.appTargetVersion(), 3);

    serial.connected = true;
    serial.writes.clear();
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    const uint8_t sync[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
    ASSERT_FALSE(serial.writes.empty());
    EXPECT_EQ(serial.writes.back(), (std::vector<uint8_t>{
        sigurdos::comms::RESP_CODE_ERR, sigurdos::comms::ERR_CODE_BAD_STATE}));

    const uint8_t v3_query[] = {sigurdos::comms::CMD_DEVICE_QUERY, 3};
    ASSERT_TRUE(bridge.handleFrame(v3_query, sizeof(v3_query)));
    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
    ASSERT_FALSE(serial.writes.empty());
    EXPECT_EQ(serial.writes.back()[0],
              sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV_V3);
}

TEST_F(CompanionProtocolTest, UndersizedKnownCommandsReturnIllegalArgument) {
    namespace cc = sigurdos::comms;
    const uint8_t commands[] = {
        cc::CMD_DEVICE_QUERY, cc::CMD_APP_START, cc::CMD_SEND_TXT_MSG,
        cc::CMD_SEND_CHANNEL_TXT_MSG, cc::CMD_SEND_CHANNEL_DATA,
        cc::CMD_SET_DEVICE_TIME, cc::CMD_SET_ADVERT_NAME,
        cc::CMD_SET_ADVERT_LATLON, cc::CMD_SET_RADIO_PARAMS,
        cc::CMD_SET_RADIO_TX_POWER, cc::CMD_SET_TUNING_PARAMS,
        cc::CMD_GET_CHANNEL, cc::CMD_SET_CHANNEL, cc::CMD_SET_DEVICE_PIN,
        cc::CMD_IMPORT_PRIVATE_KEY, cc::CMD_SET_OTHER_PARAMS,
        cc::CMD_SET_PATH_HASH_MODE, cc::CMD_SET_AUTOADD_CONFIG,
        cc::CMD_GET_CONTACT_BY_KEY, cc::CMD_RESET_PATH, cc::CMD_REMOVE_CONTACT,
        cc::CMD_SHARE_CONTACT, cc::CMD_HAS_CONNECTION, cc::CMD_LOGOUT,
        cc::CMD_ADD_UPDATE_CONTACT, cc::CMD_IMPORT_CONTACT, cc::CMD_REBOOT,
        cc::CMD_FACTORY_RESET, cc::CMD_GET_STATS, cc::CMD_SET_CUSTOM_VAR,
        cc::CMD_GET_ADVERT_PATH, cc::CMD_SEND_PATH_DISCOVERY_REQ,
        cc::CMD_SET_FLOOD_SCOPE_KEY, cc::CMD_SIGN_DATA, cc::CMD_SEND_LOGIN,
        cc::CMD_SEND_STATUS_REQ, cc::CMD_SEND_TELEMETRY_REQ,
        cc::CMD_SEND_BINARY_REQ, cc::CMD_SEND_ANON_REQ, cc::CMD_SEND_TRACE_PATH,
        cc::CMD_SEND_RAW_DATA, cc::CMD_SEND_CONTROL_DATA,
    };

    for (uint8_t command : commands) {
        serial.writes.clear();
        ASSERT_TRUE(bridge.handleFrame(&command, 1)) << "command " << (int)command;
        ASSERT_EQ(serial.writes.size(), 1U) << "command " << (int)command;
        EXPECT_EQ(serial.writes[0], (std::vector<uint8_t>{
            cc::RESP_CODE_ERR, cc::ERR_CODE_ILLEGAL_ARG})) << "command " << (int)command;
    }
}

TEST_F(CompanionProtocolTest, SetPathHashModeAcceptsValidModes) {
    for (uint8_t mode = 0; mode <= 2; mode++) {
        serial.writes.clear();
        uint8_t cmd[] = {sigurdos::comms::CMD_SET_PATH_HASH_MODE, 0, mode};
        ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
        ASSERT_EQ(serial.writes.size(), 1u);
        EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_OK);
        EXPECT_EQ(host.pathHashMode(), mode);
    }
}

TEST_F(CompanionProtocolTest, SetPathHashModeRejectsReservedMode) {
    host.path_hash_mode = 1;
    uint8_t cmd[] = {sigurdos::comms::CMD_SET_PATH_HASH_MODE, 0, 3};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
    EXPECT_EQ(host.pathHashMode(), 1);  // unchanged
}

TEST_F(CompanionProtocolTest, AppStartReturnsSelfInfo) {
    uint8_t start[8] = {sigurdos::comms::CMD_APP_START};
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    ASSERT_EQ(serial.writes.size(), 1u);
    const auto& out = serial.writes[0];
    ASSERT_GT(out.size(), 55u);
    EXPECT_EQ(out[0], sigurdos::comms::RESP_CODE_SELF_INFO);
    EXPECT_EQ(out[1], 1);
    EXPECT_EQ(out[2], 22);
    EXPECT_EQ(out[4], 0);
    EXPECT_EQ(out[35], 31);
}

TEST_F(CompanionProtocolTest, AppStartPreservesMultiAckCountAndLocationPolicy) {
    host.self_multi_acks = 7;
    host.self_advert_loc_policy = 3;
    uint8_t start[8] = {sigurdos::comms::CMD_APP_START};

    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    ASSERT_EQ(serial.writes.size(), 1U);
    ASSERT_GT(serial.writes[0].size(), 45U);
    EXPECT_EQ(serial.writes[0][44], 7);
    EXPECT_EQ(serial.writes[0][45], 3);
}

TEST_F(CompanionProtocolTest, AppStartSeedsPersistedMessagesForSync) {
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "persisted", sizeof(msg.text) - 1);
    msg.timestamp = 88;
    msg.is_channel = false;
    for (int i = 0; i < 6; i++) msg.sender_prefix[i] = (uint8_t)(0xA0 + i);
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg));

    uint8_t start[8] = {sigurdos::comms::CMD_APP_START};
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    ASSERT_EQ(serial.writes.size(), 2u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_SELF_INFO);
    EXPECT_EQ(serial.writes[1][0], sigurdos::comms::PUSH_CODE_MSG_WAITING);

    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 3u);
    EXPECT_EQ(serial.writes[2][0], sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV_V3);

    sigurdos::mesh::StoredMessage stored{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&stored, 1), 1);
    EXPECT_EQ(stored.store_id, 1U);
    EXPECT_FALSE(stored.companion_sent);

    // The next request from this authenticated session acknowledges the frame.
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&stored, 1), 1);
    EXPECT_TRUE(stored.companion_sent);
}

TEST_F(CompanionProtocolTest, RepeatedAppStartDoesNotDuplicateSnapshotMessages) {
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "persisted", sizeof(msg.text) - 1);
    msg.timestamp = 88;
    for (int i = 0; i < 6; ++i) msg.sender_prefix[i] = (uint8_t)(0xA0 + i);
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg));

    uint8_t start[8] = {sigurdos::comms::CMD_APP_START};
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    ASSERT_EQ(serial.writes.size(), 3U);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_SELF_INFO);
    EXPECT_EQ(serial.writes[1][0], sigurdos::comms::PUSH_CODE_MSG_WAITING);
    EXPECT_EQ(serial.writes[2][0], sigurdos::comms::RESP_CODE_SELF_INFO);

    const uint8_t sync[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
    ASSERT_EQ(serial.writes.size(), 5U);
    EXPECT_EQ(serial.writes[3][0], sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV_V3);
    EXPECT_EQ(serial.writes[4][0], sigurdos::comms::RESP_CODE_NO_MORE_MESSAGES);
}

TEST_F(CompanionProtocolTest, SyncRefillsUntilCompletePersistentBacklogDrains) {
    constexpr uint32_t backlog_size = 33;
    for (uint32_t i = 0; i < backlog_size; ++i) {
        sigurdos::mesh::StoredMessage msg{};
        std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
        std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
        std::snprintf(msg.text, sizeof(msg.text), "backlog-%lu", (unsigned long)i);
        msg.timestamp = 1000 + i;
        for (int p = 0; p < 6; ++p) msg.sender_prefix[p] = (uint8_t)(0xA0 + p);
        ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg));
    }

    const uint8_t start[8] = {sigurdos::comms::CMD_APP_START};
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    ASSERT_EQ(serial.writes.size(), 2u);  // SELF_INFO + MSG_WAITING

    const uint8_t sync[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    for (uint32_t i = 0; i < backlog_size; ++i) {
        ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
        ASSERT_EQ(serial.writes.size(), 3u + i);
        const auto& frame = serial.writes[2 + i];
        ASSERT_GE(frame.size(), 16u);
        EXPECT_EQ(frame[0], sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV_V3);
        uint32_t timestamp = 0;
        std::memcpy(&timestamp, &frame[12], sizeof(timestamp));
        EXPECT_EQ(timestamp, 1000u + i);
    }

    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
    ASSERT_EQ(serial.writes.size(), 3u + backlog_size);
    EXPECT_EQ(serial.writes.back()[0],
              sigurdos::comms::RESP_CODE_NO_MORE_MESSAGES);

    std::vector<sigurdos::mesh::StoredMessage> stored(backlog_size);
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(
                  stored.data(), static_cast<int>(stored.size())),
              static_cast<int>(backlog_size));
    for (const auto& msg : stored) EXPECT_TRUE(msg.companion_sent);
}

TEST_F(CompanionProtocolTest,
       DeterministicStatefulSyncPreservesOrderAcrossRetriesAndReconnects) {
    constexpr uint32_t backlog_size = 64;
    for (uint32_t i = 0; i < backlog_size; ++i) {
        sigurdos::mesh::StoredMessage msg{};
        std::strncpy(msg.conversation, "DM: Alice",
                     sizeof(msg.conversation) - 1);
        std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
        std::snprintf(msg.text, sizeof(msg.text), "sequence-%lu",
                      (unsigned long)i);
        msg.timestamp = 5000 + i;
        for (int p = 0; p < 6; ++p) {
            msg.sender_prefix[p] = (uint8_t)(0xA0 + p);
        }
        ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg));
    }

    const uint8_t start[8] = {sigurdos::comms::CMD_APP_START};
    const uint8_t sync[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    const uint8_t query[] = {sigurdos::comms::CMD_DEVICE_QUERY, 3};
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));

    uint32_t sequence = 0x5EED1234u;
    uint32_t retry_count = 0;
    uint32_t reconnect_count = 0;
    uint32_t restart_count = 0;
    for (uint32_t expected = 0; expected < backlog_size; ++expected) {
        // Fixed-seed scheduling makes this repeatable while exercising
        // interactions between page refill, repeated APP_START, failed writes,
        // and transport disconnects instead of testing each event in isolation.
        sequence = sequence * 1664525u + 1013904223u;

        if (sequence & 0x01u) {
            ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
            ++restart_count;
        }
        if (sequence & 0x02u) {
            const size_t before = serial.writes.size();
            serial.fail_writes = 1;
            ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
            EXPECT_EQ(serial.writes.size(), before);
            ++retry_count;
        }
        if (sequence & 0x04u) {
            const size_t before = serial.writes.size();
            serial.connected = false;
            bridge.loop();
            EXPECT_EQ(serial.writes.size(), before);
            serial.connected = true;
            serial.generation++;
            ASSERT_TRUE(bridge.handleFrame(query, sizeof(query)));
            ASSERT_EQ(serial.writes.size(), before + 1);
            EXPECT_EQ(serial.writes.back()[0],
                      sigurdos::comms::RESP_CODE_DEVICE_INFO);
            ++reconnect_count;
        }

        // A reconnect before the next SYNC deliberately replays the prior
        // in-flight record. Consume that at-least-once duplicate, then require
        // the next unique record to remain in strict order.
        for (;;) {
            const size_t before = serial.writes.size();
            ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
            ASSERT_EQ(serial.writes.size(), before + 1);
            const auto& frame = serial.writes.back();
            ASSERT_GE(frame.size(), 16U);
            EXPECT_EQ(frame[0],
                      sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV_V3);
            uint32_t timestamp = 0;
            std::memcpy(&timestamp, &frame[12], sizeof(timestamp));
            if (timestamp == 5000u + expected) break;
            ASSERT_GT(expected, 0U);
            EXPECT_EQ(timestamp, 5000u + expected - 1U);
        }
    }

    EXPECT_GT(retry_count, 0U);
    EXPECT_GT(reconnect_count, 0U);
    EXPECT_GT(restart_count, 0U);
    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
    ASSERT_FALSE(serial.writes.empty());
    EXPECT_EQ(serial.writes.back()[0],
              sigurdos::comms::RESP_CODE_NO_MORE_MESSAGES);

    std::vector<sigurdos::mesh::StoredMessage> stored(backlog_size);
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(
                  stored.data(), static_cast<int>(stored.size())),
              static_cast<int>(backlog_size));
    for (const auto& msg : stored) EXPECT_TRUE(msg.companion_sent);
}

TEST_F(CompanionProtocolTest, AppStartDoesNotEchoSelfSentMessages) {
    // A message the device sent itself (is_self) must never be mirrored back to
    // the app: the app already has the ones it sent, and the protocol has no
    // device-originated-send frame, so an echo arrives as a bogus *incoming*
    // message. Regression test for the channel self-echo bug.
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "Public", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "SigurdOS T-Deck", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "my own channel message", sizeof(msg.text) - 1);
    msg.timestamp = 88;
    msg.is_channel = true;
    msg.is_self = true;
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg));

    uint8_t start[8] = {sigurdos::comms::CMD_APP_START};
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    // Only SELF_INFO — no PUSH_CODE_MSG_WAITING tickle for a self message.
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_SELF_INFO);

    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 2u);
    EXPECT_EQ(serial.writes[1][0], sigurdos::comms::RESP_CODE_NO_MORE_MESSAGES);
}

TEST_F(CompanionProtocolTest, EnqueueRejectsSelfSentMessage) {
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "SigurdOS T-Deck", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "outgoing dm", sizeof(msg.text) - 1);
    msg.timestamp = 99;
    msg.is_self = true;
    EXPECT_FALSE(bridge.enqueueMessage(msg));
    EXPECT_EQ(serial.writes.size(), 0u);
}

TEST_F(CompanionProtocolTest, EmptySyncReturnsNoMoreMessages) {
    host.now = 4242;
    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_NO_MORE_MESSAGES);
    EXPECT_EQ(bridge.lastSyncTime(), 4242u);
}

TEST_F(CompanionProtocolTest, EnqueuedMessageTicklesAndDrains) {
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "hello", sizeof(msg.text) - 1);
    msg.timestamp = 77;
    msg.is_channel = false;
    for (int i = 0; i < 6; i++) msg.sender_prefix[i] = (uint8_t)(0xA0 + i);

    ASSERT_TRUE(bridge.enqueueMessage(msg));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::PUSH_CODE_MSG_WAITING);

    host.now = 4343;
    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 2u);
    EXPECT_EQ(serial.writes[1][0], sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV_V3);
    EXPECT_EQ(bridge.lastSyncTime(), 4343u);
}

TEST_F(CompanionProtocolTest, FailedWriteKeepsOfflineFrameForRetry) {
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "retry me", sizeof(msg.text) - 1);
    msg.timestamp = 77;
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg, &msg.store_id));
    ASSERT_TRUE(bridge.enqueueMessage(msg));
    ASSERT_EQ(serial.writes.size(), 1U); // waiting tickle

    serial.fail_writes = 1;
    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    EXPECT_EQ(serial.writes.size(), 1U);

    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 2U);
    EXPECT_EQ(serial.writes[1][0], sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV_V3);
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 3U);
    EXPECT_EQ(serial.writes[2][0], sigurdos::comms::RESP_CODE_NO_MORE_MESSAGES);
}

TEST_F(CompanionProtocolTest, DistinctStoreIdsDoNotDeduplicateIdenticalFrames) {
    sigurdos::mesh::StoredMessage first{};
    std::strncpy(first.conversation, "DM: Alice", sizeof(first.conversation) - 1);
    std::strncpy(first.sender, "Alice", sizeof(first.sender) - 1);
    std::strncpy(first.text, "same bytes", sizeof(first.text) - 1);
    first.timestamp = 77;
    first.store_id = 101;
    auto second = first;
    second.store_id = 102;

    EXPECT_TRUE(bridge.enqueueMessage(first));
    EXPECT_TRUE(bridge.enqueueMessage(second));
}

TEST_F(CompanionProtocolTest, VolatileChannelDataCannotEvictDurableBacklog) {
    for (uint32_t i = 1; i <= 16; ++i) {
        sigurdos::mesh::StoredMessage msg{};
        std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
        std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
        std::snprintf(msg.text, sizeof(msg.text), "durable-%lu", (unsigned long)i);
        msg.timestamp = i;
        msg.store_id = i;
        ASSERT_TRUE(bridge.enqueueMessage(msg));
    }
    const uint8_t payload[] = {0xAA};
    EXPECT_FALSE(bridge.enqueueChannelData(0, 0, 0xFF, 1, payload, sizeof(payload)));
}

TEST_F(CompanionProtocolTest, MarkFailureRetainsDeliveredFrameForRetry) {
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "mark retry", sizeof(msg.text) - 1);
    msg.timestamp = 79;
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg, &msg.store_id));
    ASSERT_TRUE(bridge.enqueueMessage(msg));
    serial.writes.clear();

    ASSERT_EQ(std::remove(store_path), 0);  // force mark-by-ID failure
    const uint8_t sync[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
    ASSERT_EQ(serial.writes.size(), 2U);
    EXPECT_EQ(serial.writes[0], serial.writes[1]);
}

TEST_F(CompanionProtocolTest, DisconnectDuringInflightDeliveryRetriesForNewSession) {
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "reconnect", sizeof(msg.text) - 1);
    msg.timestamp = 78;
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg, &msg.store_id));
    ASSERT_TRUE(bridge.enqueueMessage(msg));
    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};

    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 2U); // tickle + first-session frame

    serial.connected = false;
    bridge.loop();
    serial.connected = true;
    serial.generation++;
    const uint8_t query[] = {sigurdos::comms::CMD_DEVICE_QUERY, 3};
    ASSERT_TRUE(bridge.handleFrame(query, sizeof(query)));
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 4U); // query response + durable retry
    EXPECT_EQ(serial.writes[1], serial.writes[3]);

    sigurdos::mesh::StoredMessage stored{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&stored, 1), 1);
    EXPECT_FALSE(stored.companion_sent);

    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&stored, 1), 1);
    EXPECT_TRUE(stored.companion_sent);
}

TEST_F(CompanionProtocolTest, ChannelFrameCarriesRealPathLenAndTimestamp) {
    // Regression: the V3 channel frame used to hardcode the path-length byte to
    // 0xFF, which the app decodes as 63 hops / 4-byte hashes, and dropped the
    // sender timestamp, surfacing every message as "1970". The frame must now
    // carry the stored path_len and the real timestamp.
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "Public", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "Alice: hi", sizeof(msg.text) - 1);
    msg.timestamp = 0x11223344u;  // a real 2026-era epoch, not 0
    msg.is_channel = true;
    msg.path_len = 0x02;  // 2 hops, 1-byte hashes

    ASSERT_TRUE(bridge.enqueueMessage(msg));
    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 2u);
    const auto& out = serial.writes[1];
    ASSERT_GE(out.size(), 11u);
    EXPECT_EQ(out[0], sigurdos::comms::RESP_CODE_CHANNEL_MSG_RECV_V3);
    EXPECT_EQ(out[5], 0x02);  // path_len, not 0xFF
    uint32_t ts = 0;
    std::memcpy(&ts, &out[7], 4);
    EXPECT_EQ(ts, 0x11223344u);
}

TEST_F(CompanionProtocolTest, ContactFrameCarriesRealPathLen) {
    // The DM (contact) V3 frame must also forward the stored path_len rather
    // than the old hardcoded 0xFF placeholder.
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Bob", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Bob", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "yo", sizeof(msg.text) - 1);
    msg.timestamp = 0x0A0B0C0Du;
    msg.is_channel = false;
    msg.path_len = 0x00;  // received directly, zero hops
    for (int i = 0; i < 6; i++) msg.sender_prefix[i] = (uint8_t)(0xC0 + i);

    ASSERT_TRUE(bridge.enqueueMessage(msg));
    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 2u);
    const auto& out = serial.writes[1];
    ASSERT_GE(out.size(), 16u);
    EXPECT_EQ(out[0], sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV_V3);
    // [4..9] sender_prefix, [10] path_len, [11] txt_type, [12..15] timestamp
    EXPECT_EQ(out[10], 0x00);
    uint32_t ts = 0;
    std::memcpy(&ts, &out[12], 4);
    EXPECT_EQ(ts, 0x0A0B0C0Du);
}

TEST_F(CompanionProtocolTest, NotifySendConfirmedEmitsPushFrame) {
    // PUSH_CODE_SEND_CONFIRMED frame: [code][ack:4][trip_time_ms:4] = 9 bytes.
    // Regression: this path existed but was never wired from the ACK handler,
    // so messages the app sent through the device never showed as delivered.
    EXPECT_TRUE(bridge.notifySendConfirmed(0xAABBCCDDu, 0x11223344u));
    ASSERT_EQ(serial.writes.size(), 1u);
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 9u);
    EXPECT_EQ(out[0], sigurdos::comms::PUSH_CODE_SEND_CONFIRMED);
    uint32_t ack = 0, trip = 0;
    std::memcpy(&ack, &out[1], 4);
    std::memcpy(&trip, &out[5], 4);
    EXPECT_EQ(ack, 0xAABBCCDDu);
    EXPECT_EQ(trip, 0x11223344u);
}

TEST_F(CompanionProtocolTest, SendTextDispatchesToHostAndReturnsSent) {
    uint8_t frame[32]{};
    int i = 0;
    frame[i++] = sigurdos::comms::CMD_SEND_TXT_MSG;
    frame[i++] = sigurdos::comms::COMPANION_TXT_PLAIN;
    frame[i++] = 0;
    uint32_t ts = 99;
    std::memcpy(&frame[i], &ts, 4);
    i += 4;
    for (int p = 0; p < 6; p++) frame[i++] = (uint8_t)(0xA0 + p);
    std::memcpy(&frame[i], "hi", 2);
    i += 2;

    ASSERT_TRUE(bridge.handleFrame(frame, i));
    ASSERT_TRUE(host.sent_dm);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_SENT);
    uint32_t ack = 0;
    std::memcpy(&ack, &serial.writes[0][2], 4);
    EXPECT_EQ(ack, 0x12345678u);
}

namespace cc = sigurdos::comms;

TEST_F(CompanionProtocolTest, SetRadioParamsValidAndInvalid) {
    uint8_t f[12] = { cc::CMD_SET_RADIO_PARAMS };
    uint32_t freq = 869525, bw = 250000;  // kHz, Hz
    std::memcpy(&f[1], &freq, 4);
    std::memcpy(&f[5], &bw, 4);
    f[9] = 11; f[10] = 5; f[11] = 0;
    ASSERT_TRUE(bridge.handleFrame(f, sizeof(f)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    EXPECT_EQ(host.last_freq_khz, 869525u);
    EXPECT_EQ(host.last_sf, 11);

    serial.writes.clear();
    f[9] = 99;  // invalid SF
    ASSERT_TRUE(bridge.handleFrame(f, sizeof(f)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, SetRadioTxPowerRange) {
    uint8_t ok[2] = { cc::CMD_SET_RADIO_TX_POWER, (uint8_t)20 };
    ASSERT_TRUE(bridge.handleFrame(ok, sizeof(ok)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    EXPECT_EQ(host.last_tx_power, 20);

    serial.writes.clear();
    uint8_t bad[2] = { cc::CMD_SET_RADIO_TX_POWER, (uint8_t)100 };  // > max 22
    ASSERT_TRUE(bridge.handleFrame(bad, sizeof(bad)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
}

TEST_F(CompanionProtocolTest, TuningParamsRoundTrip) {
    uint8_t set[9] = { cc::CMD_SET_TUNING_PARAMS };
    uint32_t rx = 1500, af = 1200;
    std::memcpy(&set[1], &rx, 4);
    std::memcpy(&set[5], &af, 4);
    ASSERT_TRUE(bridge.handleFrame(set, sizeof(set)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);

    serial.writes.clear();
    uint8_t get[1] = { cc::CMD_GET_TUNING_PARAMS };
    ASSERT_TRUE(bridge.handleFrame(get, sizeof(get)));
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 9u);
    EXPECT_EQ(out[0], cc::RESP_CODE_TUNING_PARAMS);
    uint32_t got_rx = 0, got_af = 0;
    std::memcpy(&got_rx, &out[1], 4);
    std::memcpy(&got_af, &out[5], 4);
    EXPECT_EQ(got_rx, 1500u);
    EXPECT_EQ(got_af, 1200u);
}

TEST_F(CompanionProtocolTest, OtherParamsPreservePackedModesAndMultiAckCount) {
    const uint8_t frame[] = {
        cc::CMD_SET_OTHER_PARAMS,
        1,     // manual-add mode
        0x26,  // env=2, loc=1, base=2
        1,     // advert location policy
        3,     // extra ACK count (must not collapse to bool)
    };

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    EXPECT_EQ(host.last_other.manual_add_contacts, 1);
    EXPECT_TRUE(host.last_other.telemetry_present);
    EXPECT_EQ(host.last_other.telemetry_modes, 0x26);
    EXPECT_TRUE(host.last_other.multi_acks_present);
    EXPECT_EQ(host.last_other.multi_acks, 3);
}

TEST_F(CompanionProtocolTest, AutoAddConfigRoundTrip) {
    uint8_t set[3] = { cc::CMD_SET_AUTOADD_CONFIG, 0x0A, 7 };
    ASSERT_TRUE(bridge.handleFrame(set, sizeof(set)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    EXPECT_EQ(host.autoadd_cfg, 0x0A);
    EXPECT_EQ(host.autoadd_hops, 7);

    serial.writes.clear();
    uint8_t get[1] = { cc::CMD_GET_AUTOADD_CONFIG };
    ASSERT_TRUE(bridge.handleFrame(get, sizeof(get)));
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], cc::RESP_CODE_AUTOADD_CONFIG);
    EXPECT_EQ(out[1], 0x0A);
    EXPECT_EQ(out[2], 7);
}

TEST_F(CompanionProtocolTest, SetAdvertNameAndLatLon) {
    uint8_t name[6] = { cc::CMD_SET_ADVERT_NAME, 'N', 'o', 'd', 'e', '1' };
    ASSERT_TRUE(bridge.handleFrame(name, sizeof(name)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    EXPECT_STREQ(host.last_advert_name, "Node1");

    serial.writes.clear();
    uint8_t ll[9] = { cc::CMD_SET_ADVERT_LATLON };
    int32_t lat = 51500000, lon = -120000;
    std::memcpy(&ll[1], &lat, 4);
    std::memcpy(&ll[5], &lon, 4);
    ASSERT_TRUE(bridge.handleFrame(ll, sizeof(ll)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    EXPECT_EQ(host.last_lat, 51500000);

    serial.writes.clear();
    int32_t bad_lat = 99000000;  // > 90e6
    std::memcpy(&ll[1], &bad_lat, 4);
    ASSERT_TRUE(bridge.handleFrame(ll, sizeof(ll)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
}

TEST_F(CompanionProtocolTest, AddUpdateContactParsesFrame) {
    std::vector<uint8_t> f(1 + 32 + 2 + 1 + 64 + 32 + 4, 0);
    f[0] = cc::CMD_ADD_UPDATE_CONTACT;
    for (int i = 0; i < 32; i++) f[1 + i] = (uint8_t)(0x10 + i);
    f[33] = 1;     // type
    f[34] = 0;     // flags
    f[35] = 0xFF;  // out_path_len
    std::strcpy((char*)&f[1 + 32 + 2 + 1 + 64], "Bob");  // name field
    ASSERT_TRUE(bridge.handleFrame(f.data(), f.size()));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    EXPECT_EQ(host.last_added.type, 1);
    EXPECT_EQ(host.last_added.pub_key[0], 0x10);
    EXPECT_STREQ(host.last_added.name, "Bob");
}

TEST_F(CompanionProtocolTest, AddUpdateContactRejectsInvalidEncodedPathBeforeMutation) {
    std::vector<uint8_t> f(1 + 32 + 2 + 1 + 64 + 32 + 4, 0);
    f[0] = cc::CMD_ADD_UPDATE_CONTACT;
    f[35] = 0x61;  // 33 two-byte hashes require 66 bytes

    ASSERT_TRUE(bridge.handleFrame(f.data(), f.size()));
    EXPECT_FALSE(host.add_contact_called);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, AdvertPathCopiesPhysicalBytesNotEncodedValue) {
    host.advert_path_found = true;
    host.advert_path_descriptor = 0x41;  // one two-byte hash
    host.advert_path_bytes = {0xAA, 0xBB};
    host.advert_path_timestamp = 0x10203040;
    std::vector<uint8_t> f(2 + 32, 0);
    f[0] = cc::CMD_GET_ADVERT_PATH;

    ASSERT_TRUE(bridge.handleFrame(f.data(), f.size()));
    ASSERT_EQ(serial.writes.size(), 1u);
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 8u);  // code + timestamp + encoding + 2 path bytes
    EXPECT_EQ(out[0], cc::RESP_CODE_ADVERT_PATH);
    EXPECT_EQ(out[5], 0x41);
    EXPECT_EQ(out[6], 0xAA);
    EXPECT_EQ(out[7], 0xBB);
}

TEST_F(CompanionProtocolTest, AdvertPathRejectsMismatchedCopiedByteCount) {
    host.advert_path_found = true;
    host.advert_path_descriptor = 0x41;  // requires two physical bytes
    host.advert_path_bytes = {0xAA};
    std::vector<uint8_t> f(2 + 32, 0);
    f[0] = cc::CMD_GET_ADVERT_PATH;

    ASSERT_TRUE(bridge.handleFrame(f.data(), f.size()));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_NOT_FOUND);
}

TEST_F(CompanionProtocolTest, AdvertPathSupportsMaximumPathsInEveryHashMode) {
    const struct {
        uint8_t encoded_len;
        size_t byte_count;
    } cases[] = {
        {0x3F, 63},  // 63 one-byte hashes
        {0x60, 64},  // 32 two-byte hashes
        {0x95, 63},  // 21 three-byte hashes
    };
    std::vector<uint8_t> f(2 + 32, 0);
    f[0] = cc::CMD_GET_ADVERT_PATH;
    host.advert_path_found = true;

    for (const auto& c : cases) {
        serial.writes.clear();
        host.advert_path_descriptor = c.encoded_len;
        host.advert_path_bytes.assign(c.byte_count, 0xA5);
        ASSERT_TRUE(bridge.handleFrame(f.data(), f.size()));
        ASSERT_EQ(serial.writes.size(), 1u);
        EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ADVERT_PATH);
        EXPECT_EQ(serial.writes[0][5], c.encoded_len);
        EXPECT_EQ(serial.writes[0].size(), c.byte_count + 6);
    }
}

TEST_F(CompanionProtocolTest, GetContactByKeyAndNotFound) {
    uint8_t f[1 + 32] = { cc::CMD_GET_CONTACT_BY_KEY };
    for (int i = 0; i < 32; i++) f[1 + i] = (uint8_t)i;
    host.contact_found = true;
    ASSERT_TRUE(bridge.handleFrame(f, sizeof(f)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_CONTACT);

    serial.writes.clear();
    host.contact_found = false;
    ASSERT_TRUE(bridge.handleFrame(f, sizeof(f)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_NOT_FOUND);
}

TEST_F(CompanionProtocolTest, ExportContactSelfAndByKey) {
    uint8_t self[1] = { cc::CMD_EXPORT_CONTACT };  // no key → self
    ASSERT_TRUE(bridge.handleFrame(self, sizeof(self)));
    const auto& out = serial.writes[0];
    EXPECT_EQ(out[0], cc::RESP_CODE_EXPORT_CONTACT);
    ASSERT_EQ(out.size(), 4u);  // code + 3 self bytes

    serial.writes.clear();
    uint8_t bykey[1 + 32] = { cc::CMD_EXPORT_CONTACT };
    host.contact_found = true;
    ASSERT_TRUE(bridge.handleFrame(bykey, sizeof(bykey)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_EXPORT_CONTACT);
    ASSERT_EQ(serial.writes[0].size(), 3u);  // code + 2 contact bytes
}

TEST_F(CompanionProtocolTest, HasConnectionAndLogout) {
    uint8_t f[1 + 32] = { cc::CMD_HAS_CONNECTION };
    host.has_connection = false;
    ASSERT_TRUE(bridge.handleFrame(f, sizeof(f)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);

    serial.writes.clear();
    host.has_connection = true;
    ASSERT_TRUE(bridge.handleFrame(f, sizeof(f)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);

    serial.writes.clear();
    uint8_t lo[1 + 32] = { cc::CMD_LOGOUT };
    ASSERT_TRUE(bridge.handleFrame(lo, sizeof(lo)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
}

TEST_F(CompanionProtocolTest, SetDevicePinAcceptsZeroAndSixDigitBounds) {
    const uint32_t accepted[] = {
        0,
        cc::SIGURDOS_COMPANION_BLE_PIN_MIN,
        cc::SIGURDOS_COMPANION_BLE_PIN_MAX,
    };

    for (uint32_t pin : accepted) {
        serial.writes.clear();
        uint8_t cmd[5] = {cc::CMD_SET_DEVICE_PIN};
        std::memcpy(&cmd[1], &pin, sizeof(pin));
        ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
        ASSERT_EQ(serial.writes.size(), 1u);
        EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
        EXPECT_EQ(host.last_ble_pin, pin);
    }
    EXPECT_EQ(host.set_ble_pin_calls, 3);
}

TEST_F(CompanionProtocolTest, SetDevicePinRejectsNonSixDigitValues) {
    const uint32_t rejected[] = {
        cc::SIGURDOS_COMPANION_BLE_PIN_MIN - 1,
        cc::SIGURDOS_COMPANION_BLE_PIN_MAX + 1,
    };

    for (uint32_t pin : rejected) {
        serial.writes.clear();
        uint8_t cmd[5] = {cc::CMD_SET_DEVICE_PIN};
        std::memcpy(&cmd[1], &pin, sizeof(pin));
        ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
        ASSERT_EQ(serial.writes.size(), 1u);
        ASSERT_EQ(serial.writes[0].size(), 2u);
        EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
        EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_ILLEGAL_ARG);
    }
    EXPECT_EQ(host.set_ble_pin_calls, 0);
}

TEST_F(CompanionProtocolTest, SetDevicePinReportsPersistenceFailure) {
    host.set_ble_pin_result = false;
    uint32_t pin = 654321;
    uint8_t cmd[5] = {cc::CMD_SET_DEVICE_PIN};
    std::memcpy(&cmd[1], &pin, sizeof(pin));

    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 1u);
    ASSERT_EQ(serial.writes[0].size(), 2u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_ILLEGAL_ARG);
    EXPECT_EQ(host.set_ble_pin_calls, 1);
}

TEST(CompanionBlePinPrefs, UpdatesPairingPinWithoutChangingDeviceLockPin) {
    sigurdos::NodePrefs prefs;
    prefs.device_pin = 4321;
    prefs.ble_pin = 123456;

    ASSERT_TRUE(sigurdos::mesh::applyCompanionBlePin(prefs, 654321));
    EXPECT_EQ(prefs.ble_pin, 654321u);
    EXPECT_EQ(prefs.device_pin, 4321u);
    EXPECT_TRUE(prefs.ble_bond_reset_pending);

    prefs.ble_bond_reset_pending = false;
    EXPECT_FALSE(sigurdos::mesh::applyCompanionBlePin(prefs, 99999));
    EXPECT_EQ(prefs.ble_pin, 654321u);
    EXPECT_EQ(prefs.device_pin, 4321u);
    EXPECT_FALSE(prefs.ble_bond_reset_pending);
}

TEST_F(CompanionProtocolTest, RebootAndFactoryResetGuarded) {
    uint8_t rb[7] = { cc::CMD_REBOOT, 'r','e','b','o','o','t' };
    ASSERT_TRUE(bridge.handleFrame(rb, sizeof(rb)));
    EXPECT_TRUE(host.rebooted);

    uint8_t fr[6] = { cc::CMD_FACTORY_RESET, 'r','e','s','e','t' };
    ASSERT_TRUE(bridge.handleFrame(fr, sizeof(fr)));
    EXPECT_TRUE(host.factory_reset_called);
    // OK frame queued after the (mocked, non-rebooting) reset.
    EXPECT_EQ(serial.writes.back()[0], cc::RESP_CODE_OK);

    serial.writes.clear();
    uint8_t bad[6] = { cc::CMD_FACTORY_RESET, 'n','o','p','e','!' };
    host.factory_reset_called = false;
    ASSERT_TRUE(bridge.handleFrame(bad, sizeof(bad)));
    EXPECT_FALSE(host.factory_reset_called);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);  // unsupported (bad magic)
}

TEST_F(CompanionProtocolTest, GetStatsAllTypes) {
    uint8_t core[2] = { cc::CMD_GET_STATS, cc::STATS_TYPE_CORE };
    ASSERT_TRUE(bridge.handleFrame(core, sizeof(core)));
    const auto& c = serial.writes[0];
    EXPECT_EQ(c[0], cc::RESP_CODE_STATS);
    EXPECT_EQ(c[1], cc::STATS_TYPE_CORE);
    uint16_t mv = 0; std::memcpy(&mv, &c[2], 2);
    EXPECT_EQ(mv, 4100);

    serial.writes.clear();
    uint8_t radio[2] = { cc::CMD_GET_STATS, cc::STATS_TYPE_RADIO };
    ASSERT_TRUE(bridge.handleFrame(radio, sizeof(radio)));
    EXPECT_EQ(serial.writes[0][1], cc::STATS_TYPE_RADIO);

    serial.writes.clear();
    uint8_t pkts[2] = { cc::CMD_GET_STATS, cc::STATS_TYPE_PACKETS };
    ASSERT_TRUE(bridge.handleFrame(pkts, sizeof(pkts)));
    EXPECT_EQ(serial.writes[0][1], cc::STATS_TYPE_PACKETS);
    uint32_t recv = 0; std::memcpy(&recv, &serial.writes[0][2], 4);
    EXPECT_EQ(recv, 100u);
}

TEST_F(CompanionProtocolTest, DefaultFloodScopeGetSet) {
    uint8_t get[1] = { cc::CMD_GET_DEFAULT_FLOOD_SCOPE };
    host.scope_is_set = false;
    ASSERT_TRUE(bridge.handleFrame(get, sizeof(get)));
    EXPECT_EQ(serial.writes[0].size(), 1u);  // null scope
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_DEFAULT_FLOOD_SCOPE);

    serial.writes.clear();
    host.scope_is_set = true;
    ASSERT_TRUE(bridge.handleFrame(get, sizeof(get)));
    EXPECT_EQ(serial.writes[0].size(), (size_t)(1 + 31 + 16));
    EXPECT_STREQ(reinterpret_cast<const char*>(&serial.writes[0][1]), "#test");
    for (size_t i = 1 + 31; i < 1 + 31 + 16; ++i) {
        EXPECT_EQ(serial.writes[0][i], 0x7E);
    }

    serial.writes.clear();
    std::vector<uint8_t> set(1 + 31 + 16, 0);
    set[0] = cc::CMD_SET_DEFAULT_FLOOD_SCOPE;
    std::strcpy((char*)&set[1], "#crew");
    for (int i = 0; i < 16; ++i) set[1 + 31 + i] = (uint8_t)(0xA0 + i);
    ASSERT_TRUE(bridge.handleFrame(set.data(), set.size()));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    EXPECT_STREQ(host.last_scope_name, "#crew");
    ASSERT_TRUE(host.scope_key_present);
    EXPECT_EQ(std::memcmp(host.last_scope_key, &set[1 + 31], 16), 0);

    serial.writes.clear();
    uint8_t clr[1] = { cc::CMD_SET_DEFAULT_FLOOD_SCOPE };  // <1+31+16 → clear
    ASSERT_TRUE(bridge.handleFrame(clr, sizeof(clr)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    EXPECT_TRUE(host.scope_cleared);
}

TEST_F(CompanionProtocolTest, FloodScopeKeyOverride) {
    uint8_t unscoped[2] = { cc::CMD_SET_FLOOD_SCOPE_KEY, 1 };
    ASSERT_TRUE(bridge.handleFrame(unscoped, sizeof(unscoped)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    EXPECT_TRUE(host.scope_unscoped);

    serial.writes.clear();
    uint8_t setkey[2 + 16] = { cc::CMD_SET_FLOOD_SCOPE_KEY, 0 };
    for (int i = 0; i < 16; ++i) setkey[2 + i] = (uint8_t)(0x30 + i);
    ASSERT_TRUE(bridge.handleFrame(setkey, sizeof(setkey)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    ASSERT_TRUE(host.scope_key_present);
    EXPECT_EQ(std::memcmp(host.last_scope_key, &setkey[2], 16), 0);

    serial.writes.clear();
    uint8_t reset[2] = { cc::CMD_SET_FLOOD_SCOPE_KEY, 0 };
    ASSERT_TRUE(bridge.handleFrame(reset, sizeof(reset)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    EXPECT_TRUE(host.scope_cleared);
    EXPECT_FALSE(host.scope_key_present);
}

TEST_F(CompanionProtocolTest, FloodScopeCommandsRejectMalformedFrames) {
    uint8_t short_default[2] = { cc::CMD_SET_DEFAULT_FLOOD_SCOPE, '#' };
    ASSERT_TRUE(bridge.handleFrame(short_default, sizeof(short_default)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_ILLEGAL_ARG);

    serial.writes.clear();
    uint8_t short_key[3] = { cc::CMD_SET_FLOOD_SCOPE_KEY, 0, 0x11 };
    ASSERT_TRUE(bridge.handleFrame(short_key, sizeof(short_key)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_ILLEGAL_ARG);

    serial.writes.clear();
    uint8_t bad_flag[2] = { cc::CMD_SET_FLOOD_SCOPE_KEY, 2 };
    ASSERT_TRUE(bridge.handleFrame(bad_flag, sizeof(bad_flag)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, FloodScopePersistenceFailuresAreReported) {
    host.scope_write_result = false;
    uint8_t clear_default[] = { cc::CMD_SET_DEFAULT_FLOOD_SCOPE };
    ASSERT_TRUE(bridge.handleFrame(clear_default, sizeof(clear_default)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_FILE_IO_ERROR);

    serial.writes.clear();
    host.scope_override_result = false;
    uint8_t unscoped[] = { cc::CMD_SET_FLOOD_SCOPE_KEY, 1 };
    ASSERT_TRUE(bridge.handleFrame(unscoped, sizeof(unscoped)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_FILE_IO_ERROR);
}

TEST_F(CompanionProtocolTest, SignFlow) {
    uint8_t start[1] = { cc::CMD_SIGN_START };
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    const auto& s = serial.writes[0];
    ASSERT_EQ(s.size(), 6u);
    EXPECT_EQ(s[0], cc::RESP_CODE_SIGN_START);
    uint32_t maxlen = 0; std::memcpy(&maxlen, &s[2], 4);
    EXPECT_EQ(maxlen, cc::SIGURDOS_COMPANION_MAX_SIGN_DATA);

    serial.writes.clear();
    uint8_t data[5] = { cc::CMD_SIGN_DATA, 'a', 'b', 'c', 'd' };
    ASSERT_TRUE(bridge.handleFrame(data, sizeof(data)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);

    serial.writes.clear();
    uint8_t fin[1] = { cc::CMD_SIGN_FINISH };
    ASSERT_TRUE(bridge.handleFrame(fin, sizeof(fin)));
    ASSERT_EQ(serial.writes[0].size(), 1u + 64u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_SIGNATURE);
    EXPECT_EQ(host.sign_len_seen, 4);
}

TEST_F(CompanionProtocolTest, IdentityChangeClearsSigningSession) {
    uint8_t start[1] = { cc::CMD_SIGN_START };
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));

    serial.writes.clear();
    uint8_t data[2] = { cc::CMD_SIGN_DATA, 'x' };
    ASSERT_TRUE(bridge.handleFrame(data, sizeof(data)));

    bridge.onIdentityChanged();
    serial.writes.clear();
    uint8_t finish[1] = { cc::CMD_SIGN_FINISH };
    ASSERT_TRUE(bridge.handleFrame(finish, sizeof(finish)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_BAD_STATE);
}

TEST_F(CompanionProtocolTest, SignDataBeforeStartFails) {
    uint8_t data[3] = { cc::CMD_SIGN_DATA, 'x', 'y' };
    ASSERT_TRUE(bridge.handleFrame(data, sizeof(data)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_BAD_STATE);
}

TEST_F(CompanionProtocolTest, SignFlowAcceptsAdvertisedEightKiBCapacity) {
    const uint8_t start[] = {cc::CMD_SIGN_START};
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    serial.writes.clear();

    std::vector<uint8_t> data(129, 0x5A);
    data[0] = cc::CMD_SIGN_DATA;
    for (size_t offset = 0; offset < cc::SIGURDOS_COMPANION_MAX_SIGN_DATA;
         offset += data.size() - 1) {
        ASSERT_TRUE(bridge.handleFrame(data.data(), data.size()));
        ASSERT_EQ(serial.writes.back(), (std::vector<uint8_t>{cc::RESP_CODE_OK}));
    }

    serial.writes.clear();
    const uint8_t finish[] = {cc::CMD_SIGN_FINISH};
    ASSERT_TRUE(bridge.handleFrame(finish, sizeof(finish)));
    EXPECT_EQ(host.sign_len_seen, cc::SIGURDOS_COMPANION_MAX_SIGN_DATA);
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0].size(), 1U + cc::SIGURDOS_COMPANION_SIGNATURE_SIZE);
}

TEST_F(CompanionProtocolTest, SendLoginReturnsSent) {
    uint8_t f[1 + 32] = { cc::CMD_SEND_LOGIN };
    host.last_send_ok = true;
    ASSERT_TRUE(bridge.handleFrame(f, sizeof(f)));
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 10u);
    EXPECT_EQ(out[0], cc::RESP_CODE_SENT);
    uint32_t ack = 0; std::memcpy(&ack, &out[2], 4);
    EXPECT_EQ(ack, 0x11223344u);

    serial.writes.clear();
    host.last_send_ok = false;
    ASSERT_TRUE(bridge.handleFrame(f, sizeof(f)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
}

TEST_F(CompanionProtocolTest, SendTracePathValidatesAndReturnsSent) {
    // flags=0 (path_sz 0) → path is N 1-byte hops. 2 hops.
    std::vector<uint8_t> f(10 + 2, 0);
    f[0] = cc::CMD_SEND_TRACE_PATH;
    uint32_t tag = 0xCAFEBABE, auth = 0;
    std::memcpy(&f[1], &tag, 4);
    std::memcpy(&f[5], &auth, 4);
    f[9] = 0;       // flags
    f[10] = 0xAA; f[11] = 0xBB;  // path
    ASSERT_TRUE(bridge.handleFrame(f.data(), f.size()));
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 10u);
    EXPECT_EQ(out[0], cc::RESP_CODE_SENT);
    uint32_t got_tag = 0; std::memcpy(&got_tag, &out[2], 4);
    EXPECT_EQ(got_tag, 0xCAFEBABEu);
    EXPECT_EQ(host.last_trace_path_len, 2);
}

TEST_F(CompanionProtocolTest, SendRawDataDirectDispatchesAndReturnsOk) {
    uint8_t frame[] = {
        cc::CMD_SEND_RAW_DATA,
        2,
        0xA1, 0xB2,
        0x10, 0x20, 0x30, 0x40,
    };

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_TRUE(host.sent_raw_data);
    ASSERT_EQ(host.last_raw_path.size(), 2u);
    EXPECT_EQ(host.last_raw_path[0], 0xA1);
    ASSERT_EQ(host.last_raw_payload.size(), 4u);
    EXPECT_EQ(host.last_raw_payload[3], 0x40);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
}

TEST_F(CompanionProtocolTest, SendRawDataRejectsFloodAndMalformedFrames) {
    uint8_t flood[] = {
        cc::CMD_SEND_RAW_DATA, 0xFF,
        0x10, 0x20, 0x30, 0x40,
    };
    ASSERT_TRUE(bridge.handleFrame(flood, sizeof(flood)));
    ASSERT_FALSE(host.sent_raw_data);
    ASSERT_EQ(serial.writes[0].size(), 2u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_UNSUPPORTED_CMD);

    serial.writes.clear();
    uint8_t short_payload[] = {
        cc::CMD_SEND_RAW_DATA, 1, 0xA1,
        0x10, 0x20, 0x30,
    };
    ASSERT_TRUE(bridge.handleFrame(short_payload, sizeof(short_payload)));
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_ILLEGAL_ARG);

    serial.writes.clear();
    uint8_t encoded_path[] = {
        cc::CMD_SEND_RAW_DATA, 0x40,
        0x10, 0x20, 0x30, 0x40,
    };
    ASSERT_TRUE(bridge.handleFrame(encoded_path, sizeof(encoded_path)));
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, SendRawDataReportsPacketAllocationFailure) {
    host.last_send_ok = false;
    uint8_t frame[] = {
        cc::CMD_SEND_RAW_DATA, 0,
        0x10, 0x20, 0x30, 0x40,
    };

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_TRUE(host.sent_raw_data);
    ASSERT_EQ(serial.writes[0].size(), 2u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_TABLE_FULL);
}

TEST_F(CompanionProtocolTest, SendControlDataDispatchesHighBitPayloadZeroHop) {
    uint8_t frame[] = {
        cc::CMD_SEND_CONTROL_DATA,
        0x80, 0x11, 0x22, 0x33,
    };

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_TRUE(host.sent_control_data);
    ASSERT_EQ(host.last_control_payload.size(), 4u);
    EXPECT_EQ(host.last_control_payload[0], 0x80);
    EXPECT_EQ(host.last_control_payload[3], 0x33);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
}

TEST_F(CompanionProtocolTest, SendControlDataRejectsMissingOrClearFlag) {
    uint8_t missing[] = {cc::CMD_SEND_CONTROL_DATA};
    ASSERT_TRUE(bridge.handleFrame(missing, sizeof(missing)));
    ASSERT_FALSE(host.sent_control_data);
    ASSERT_EQ(serial.writes[0].size(), 2u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_ILLEGAL_ARG);

    serial.writes.clear();
    uint8_t clear_flag[] = {cc::CMD_SEND_CONTROL_DATA, 0x01, 0xAA};
    ASSERT_TRUE(bridge.handleFrame(clear_flag, sizeof(clear_flag)));
    ASSERT_FALSE(host.sent_control_data);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, SendControlDataReportsPacketAllocationFailure) {
    host.last_send_ok = false;
    uint8_t frame[] = {cc::CMD_SEND_CONTROL_DATA, 0x80};

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_TRUE(host.sent_control_data);
    ASSERT_EQ(serial.writes[0].size(), 2u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_TABLE_FULL);
}

TEST_F(CompanionProtocolTest, GetCustomVarsEmptyAndAllowedFreq) {
    uint8_t cv[1] = { cc::CMD_GET_CUSTOM_VARS };
    ASSERT_TRUE(bridge.handleFrame(cv, sizeof(cv)));
    ASSERT_EQ(serial.writes[0].size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_CUSTOM_VARS);

    serial.writes.clear();
    uint8_t rf[1] = { cc::CMD_GET_ALLOWED_REPEAT_FREQ };
    ASSERT_TRUE(bridge.handleFrame(rf, sizeof(rf)));
    ASSERT_EQ(serial.writes[0].size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_ALLOWED_REPEAT_FREQ);
}

TEST_F(CompanionProtocolTest, AdvertPathUsesOfficialDescriptorAndFieldOrder) {
    struct AdvertPathCase {
        uint8_t descriptor;
        size_t path_bytes;
    };
    const AdvertPathCase cases[] = {
        {0x00, 0},  // zero hop
        {0x01, 1},  // one 1-byte hash
        {0x41, 2},  // one 2-byte hash
        {0x60, 64}, // maximum encoded path: 32 two-byte hashes
    };

    host.advert_path_found = true;
    host.advert_path_timestamp = 0x44332211U;
    for (const auto& test_case : cases) {
        host.advert_path_descriptor = test_case.descriptor;
        host.advert_path_bytes.resize(test_case.path_bytes);
        for (size_t i = 0; i < test_case.path_bytes; ++i) {
            host.advert_path_bytes[i] = (uint8_t)(0x80 + i);
        }
        serial.writes.clear();
        uint8_t frame[2 + cc::SIGURDOS_COMPANION_PUB_KEY_SIZE]{};
        frame[0] = cc::CMD_GET_ADVERT_PATH;

        ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
        ASSERT_EQ(serial.writes.size(), 1U);
        const auto& out = serial.writes[0];
        ASSERT_EQ(out.size(), 6U + test_case.path_bytes);
        EXPECT_EQ(out[0], cc::RESP_CODE_ADVERT_PATH);
        uint32_t timestamp = 0;
        std::memcpy(&timestamp, &out[1], sizeof(timestamp));
        EXPECT_EQ(timestamp, host.advert_path_timestamp);
        EXPECT_EQ(out[5], test_case.descriptor);
        EXPECT_EQ(std::vector<uint8_t>(out.begin() + 6, out.end()),
                  host.advert_path_bytes);
    }
}

TEST_F(CompanionProtocolTest, AdvertPathMissingEntryReturnsNotFound) {
    uint8_t frame[2 + cc::SIGURDOS_COMPANION_PUB_KEY_SIZE]{};
    frame[0] = cc::CMD_GET_ADVERT_PATH;

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0],
              (std::vector<uint8_t>{cc::RESP_CODE_ERR, cc::ERR_CODE_NOT_FOUND}));
}

TEST_F(CompanionProtocolTest, PathDiscoveryCommandUsesFullKeyAndReturnsSent) {
    host.path_discovery_result = {true, true, 0x12345678U, 4321U};
    std::vector<uint8_t> frame(2 + cc::SIGURDOS_COMPANION_PUB_KEY_SIZE, 0);
    frame[0] = cc::CMD_SEND_PATH_DISCOVERY_REQ;
    for (size_t i = 0; i < cc::SIGURDOS_COMPANION_PUB_KEY_SIZE; ++i) {
        frame[2 + i] = static_cast<uint8_t>(0x80 + i);
    }

    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    ASSERT_TRUE(host.path_discovery_called);
    EXPECT_EQ(std::memcmp(host.path_discovery_key, &frame[2],
                          sizeof(host.path_discovery_key)), 0);
    ASSERT_EQ(serial.writes.size(), 1U);
    ASSERT_EQ(serial.writes[0].size(), 10U);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_SENT);
    EXPECT_EQ(serial.writes[0][1], 1);
    uint32_t tag = 0;
    uint32_t timeout = 0;
    std::memcpy(&tag, &serial.writes[0][2], sizeof(tag));
    std::memcpy(&timeout, &serial.writes[0][6], sizeof(timeout));
    EXPECT_EQ(tag, 0x12345678U);
    EXPECT_EQ(timeout, 4321U);
}

TEST_F(CompanionProtocolTest, PathDiscoveryCommandRejectsNonzeroReservedByte) {
    std::vector<uint8_t> frame(2 + cc::SIGURDOS_COMPANION_PUB_KEY_SIZE, 0);
    frame[0] = cc::CMD_SEND_PATH_DISCOVERY_REQ;
    frame[1] = 1;

    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    EXPECT_FALSE(host.path_discovery_called);
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, CustomVarsUseOfficialNameValueWireFormat) {
    host.custom_vars = "gps:1,gps_interval:30";
    uint8_t get_frame[] = {cc::CMD_GET_CUSTOM_VARS};

    ASSERT_TRUE(bridge.handleFrame(get_frame, sizeof(get_frame)));
    ASSERT_EQ(serial.writes.size(), 1U);
    std::vector<uint8_t> expected{cc::RESP_CODE_CUSTOM_VARS};
    expected.insert(expected.end(), host.custom_vars.begin(), host.custom_vars.end());
    EXPECT_EQ(serial.writes[0], expected);

    serial.writes.clear();
    host.custom_var_result = true;
    const uint8_t set_frame[] = {
        cc::CMD_SET_CUSTOM_VAR, 'g', 'p', 's', '_', 'i', 'n', 't', 'e', 'r', 'v', 'a', 'l',
        ':', '3', '0'
    };
    ASSERT_TRUE(bridge.handleFrame(set_frame, sizeof(set_frame)));
    EXPECT_EQ(host.set_custom_var_calls, 1);
    EXPECT_EQ(host.custom_var_name, "gps_interval");
    EXPECT_EQ(host.custom_var_value, "30");
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0], (std::vector<uint8_t>{cc::RESP_CODE_OK}));
}

TEST_F(CompanionProtocolTest, SetCustomVarRejectsMissingSeparator) {
    const uint8_t frame[] = {cc::CMD_SET_CUSTOM_VAR, 'g', 'p', 's'};

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    EXPECT_EQ(host.set_custom_var_calls, 0);
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0],
              (std::vector<uint8_t>{cc::RESP_CODE_ERR, cc::ERR_CODE_ILLEGAL_ARG}));
}

TEST_F(CompanionProtocolTest, BinaryRequestReturnsStockSentFrame)
{
    const auto frame = binaryRequestFrame({0x03, 0xAA, 0x55});
    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    ASSERT_TRUE(host.sent_binary);
    ASSERT_EQ(host.last_binary_data, (std::vector<uint8_t>{0x03, 0xAA, 0x55}));
    ASSERT_EQ(serial.writes.size(), 1U);
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 10U);
    EXPECT_EQ(out[0], sigurdos::comms::RESP_CODE_SENT);
    EXPECT_EQ(out[1], 0);
    uint32_t tag = 0;
    uint32_t timeout = 0;
    std::memcpy(&tag, &out[2], sizeof(tag));
    std::memcpy(&timeout, &out[6], sizeof(timeout));
    EXPECT_EQ(tag, 0x10203040U);
    EXPECT_EQ(timeout, 3000U);
}

TEST_F(CompanionProtocolTest, BinaryResponsePushCarriesTagAndPayload)
{
    const auto frame = binaryRequestFrame({0x03});
    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    serial.writes.clear();

    const uint8_t response[] = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_TRUE(bridge.pushBinaryResponse(
        0x10203040U, response, sizeof(response)));
    ASSERT_EQ(serial.writes.size(), 1U);
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 10U);
    EXPECT_EQ(out[0], 0x8C);
    EXPECT_EQ(out[1], 0);
    uint32_t tag = 0;
    std::memcpy(&tag, &out[2], sizeof(tag));
    EXPECT_EQ(tag, 0x10203040U);
    EXPECT_EQ(std::vector<uint8_t>(out.begin() + 6, out.end()),
              (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));

    EXPECT_FALSE(bridge.pushBinaryResponse(
        0x10203040U, response, sizeof(response)));
    EXPECT_EQ(serial.writes.size(), 1U);
}

TEST_F(CompanionProtocolTest, BinaryResponseRejectsUnmatchedTag)
{
    const uint8_t response[] = {0x01};
    EXPECT_FALSE(bridge.pushBinaryResponse(
        0x55667788U, response, sizeof(response)));
    EXPECT_TRUE(serial.writes.empty());
}

TEST_F(CompanionProtocolTest, DisconnectClearsPendingBinaryTags)
{
    const auto frame = binaryRequestFrame({0x03});
    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    serial.enabled = true;
    bridge.loop();
    serial.connected = false;
    bridge.loop();
    serial.connected = true;
    serial.writes.clear();
    EXPECT_TRUE(host.cancelled_binary);

    const uint8_t response[] = {0x01};
    EXPECT_FALSE(bridge.pushBinaryResponse(
        0x10203040U, response, sizeof(response)));
    EXPECT_TRUE(serial.writes.empty());
}

TEST_F(CompanionProtocolTest, DisablingBridgeClearsPendingBinaryTags)
{
    const auto frame = binaryRequestFrame({0x03});
    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    serial.enabled = true;
    ASSERT_TRUE(bridge.setEnabled(false));
    EXPECT_TRUE(host.cancelled_binary);
    serial.connected = true;
    serial.writes.clear();

    const uint8_t response[] = {0x01};
    EXPECT_FALSE(bridge.pushBinaryResponse(
        0x10203040U, response, sizeof(response)));
    EXPECT_TRUE(serial.writes.empty());
}

TEST_F(CompanionProtocolTest, BinaryRequestRequiresKnownContact)
{
    host.contact_found = false;
    const auto frame = binaryRequestFrame({0x03});
    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    EXPECT_FALSE(host.sent_binary);
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_NOT_FOUND);
}

TEST_F(CompanionProtocolTest, BinaryRequestRejectsMissingPayload)
{
    const auto frame = binaryRequestFrame({});
    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    EXPECT_FALSE(host.sent_binary);
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, BinaryRequestMapsMeshSendFailureToTableFull)
{
    host.last_send_ok = false;
    const auto frame = binaryRequestFrame({0x03});
    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_TABLE_FULL);
}

TEST_F(CompanionProtocolTest, BinaryPendingSlotsExpireAndRejectLateResponses)
{
    host.now_ms = 100;
    for (int i = 0; i < 4; ++i) {
        const auto frame = binaryRequestFrame({(uint8_t)i});
        ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    }

    host.now_ms = 3099;
    const auto before_deadline = binaryRequestFrame({0xFE});
    ASSERT_TRUE(bridge.handleFrame(before_deadline.data(), before_deadline.size()));
    EXPECT_EQ(host.binary_send_calls, 4);
    EXPECT_TRUE(host.cancelled_binary_tags.empty());

    host.now_ms = 3100;
    const auto replacement = binaryRequestFrame({0xFF});
    ASSERT_TRUE(bridge.handleFrame(replacement.data(), replacement.size()));
    EXPECT_EQ(host.binary_send_calls, 5);
    EXPECT_EQ(host.cancelled_binary_tags,
              (std::vector<uint32_t>{0x10203040U, 0x10203041U,
                                     0x10203042U, 0x10203043U}));

    serial.writes.clear();
    const uint8_t response[] = {0x01};
    EXPECT_FALSE(bridge.pushBinaryResponse(
        0x10203040U, response, sizeof(response)));
    EXPECT_TRUE(serial.writes.empty());
    EXPECT_TRUE(bridge.pushBinaryResponse(
        0x10203044U, response, sizeof(response)));
}

TEST_F(CompanionProtocolTest, BinaryFallbackTimeoutHandlesMillisWraparound)
{
    host.binary_timeout_ms = 0;
    host.now_ms = UINT32_MAX - 999U;
    const auto frame = binaryRequestFrame({0x03});
    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));

    host.now_ms = host.now_ms + 30000U - 1U;
    serial.enabled = true;
    bridge.loop();
    EXPECT_TRUE(host.cancelled_binary_tags.empty());

    ++host.now_ms;
    bridge.loop();
    EXPECT_EQ(host.cancelled_binary_tags,
              (std::vector<uint32_t>{0x10203040U}));
    const uint8_t response[] = {0x01};
    EXPECT_FALSE(bridge.pushBinaryResponse(
        0x10203040U, response, sizeof(response)));
}

TEST_F(CompanionProtocolTest, BinaryPendingTableIsBounded)
{
    for (int i = 0; i < 4; ++i) {
        const auto frame = binaryRequestFrame({(uint8_t)i});
        ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
        ASSERT_EQ(serial.writes.back()[0], sigurdos::comms::RESP_CODE_SENT);
    }
    const auto fifth = binaryRequestFrame({0xFF});
    ASSERT_TRUE(bridge.handleFrame(fifth.data(), fifth.size()));
    EXPECT_EQ(host.binary_send_calls, 4);
    ASSERT_EQ(serial.writes.back().size(), 2U);
    EXPECT_EQ(serial.writes.back()[0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes.back()[1], sigurdos::comms::ERR_CODE_TABLE_FULL);

    serial.writes.clear();
    const uint8_t response[] = {0x01};
    ASSERT_TRUE(bridge.pushBinaryResponse(
        0x10203040U, response, sizeof(response)));
    const auto replacement = binaryRequestFrame({0xEE});
    ASSERT_TRUE(bridge.handleFrame(replacement.data(), replacement.size()));
    EXPECT_EQ(host.binary_send_calls, 5);
    EXPECT_EQ(serial.writes.back()[0], sigurdos::comms::RESP_CODE_SENT);
}

TEST_F(CompanionProtocolTest, AllowedRepeatFrequencySerializesRangePairs) {
    host.allowed_repeat_range_count = 2;
    host.allowed_repeat_ranges[0] = 868000;
    host.allowed_repeat_ranges[1] = 868000;
    host.allowed_repeat_ranges[2] = 869525;
    host.allowed_repeat_ranges[3] = 869525;

    uint8_t frame[1] = { cc::CMD_GET_ALLOWED_REPEAT_FREQ };
    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));

    ASSERT_EQ(serial.writes.size(), 1u);
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 1u + 4u * sizeof(uint32_t));
    EXPECT_EQ(out[0], cc::RESP_ALLOWED_REPEAT_FREQ);
    uint32_t ranges[4]{};
    std::memcpy(ranges, &out[1], sizeof(ranges));
    EXPECT_EQ(ranges[0], 868000u);
    EXPECT_EQ(ranges[1], 868000u);
    EXPECT_EQ(ranges[2], 869525u);
    EXPECT_EQ(ranges[3], 869525u);
}

TEST_F(CompanionProtocolTest, AnonRequestReturnsStockSentFrame)
{
    const auto frame = anonRequestFrame({0x01, 0xAA, 0x55});
    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    ASSERT_TRUE(host.sent_anon);
    EXPECT_EQ(host.last_anon_key[0], 0xC0);
    EXPECT_EQ(host.last_anon_key[31], 0xDF);
    EXPECT_EQ(host.last_anon_data,
              (std::vector<uint8_t>{0x01, 0xAA, 0x55}));
    ASSERT_EQ(serial.writes.size(), 1U);
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 10U);
    EXPECT_EQ(out[0], sigurdos::comms::RESP_CODE_SENT);
    EXPECT_EQ(out[1], 1);  // fake host reports a flood send
    uint32_t tag = 0;
    uint32_t timeout = 0;
    std::memcpy(&tag, &out[2], sizeof(tag));
    std::memcpy(&timeout, &out[6], sizeof(timeout));
    EXPECT_EQ(tag, 0x50607080U);
    EXPECT_EQ(timeout, 4500U);
}

TEST_F(CompanionProtocolTest, AnonResponseUsesMatchedBinaryPush)
{
    const auto frame = anonRequestFrame({0x01});
    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    serial.writes.clear();

    const uint8_t response[] = {0x10, 0x20};
    ASSERT_TRUE(bridge.pushBinaryResponse(
        0x50607080U, response, sizeof(response)));
    ASSERT_EQ(serial.writes.size(), 1U);
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 8U);
    EXPECT_EQ(out[0], 0x8C);
    EXPECT_EQ(out[1], 0);
    uint32_t tag = 0;
    std::memcpy(&tag, &out[2], sizeof(tag));
    EXPECT_EQ(tag, 0x50607080U);
    EXPECT_EQ(out[6], 0x10);
    EXPECT_EQ(out[7], 0x20);
}

TEST_F(CompanionProtocolTest, AnonRequestRejectsMissingPayload)
{
    const auto frame = anonRequestFrame({});
    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    EXPECT_FALSE(host.sent_anon);
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, AnonRequestMapsAllocationOrSendFailureToTableFull)
{
    host.last_send_ok = false;
    const auto frame = anonRequestFrame({0x01});
    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_TABLE_FULL);
}

// ── Live / async pushes ───────────────────────────────────────
TEST_F(CompanionProtocolTest, PushAdvertNewVsUpdate) {
    cc::CompanionContact c{};
    for (int i = 0; i < 32; i++) c.pub_key[i] = (uint8_t)(0x30 + i);
    std::strcpy(c.name, "Zed");
    EXPECT_TRUE(bridge.pushAdvert(c, true));   // connected by default
    EXPECT_EQ(serial.writes[0][0], cc::PUSH_CODE_NEW_ADVERT);

    serial.writes.clear();
    EXPECT_TRUE(bridge.pushAdvert(c, false));
    ASSERT_EQ(serial.writes[0].size(), 1u + 32u);
    EXPECT_EQ(serial.writes[0][0], cc::PUSH_CODE_ADVERT);
}

TEST_F(CompanionProtocolTest, PushPathDeletedFull) {
    cc::CompanionContact c{};
    for (int i = 0; i < 32; i++) c.pub_key[i] = (uint8_t)i;
    EXPECT_TRUE(bridge.pushPathUpdated(c));
    EXPECT_EQ(serial.writes[0][0], cc::PUSH_CODE_PATH_UPDATED);

    serial.writes.clear();
    uint8_t key[32]; std::memset(key, 7, 32);
    EXPECT_TRUE(bridge.pushContactDeleted(key));
    EXPECT_EQ(serial.writes[0][0], cc::PUSH_CODE_CONTACT_DELETED);

    serial.writes.clear();
    EXPECT_TRUE(bridge.pushContactsFull());
    ASSERT_EQ(serial.writes[0].size(), 1u);
    EXPECT_EQ(serial.writes[0][0], cc::PUSH_CODE_CONTACTS_FULL);
}

TEST_F(CompanionProtocolTest, PushPathDiscoveryResponseMatchesGoldenLayout) {
    const uint8_t prefix[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t inbound[2] = {0xA1, 0xA2};
    const uint8_t outbound[3] = {0xB1, 0xB2, 0xB3};

    ASSERT_TRUE(bridge.pushPathDiscoveryResponse(
        prefix, inbound, 2, outbound, 3));

    const std::vector<uint8_t> expected = {
        cc::PUSH_CODE_PATH_DISCOVERY_RESPONSE, 0,
        1, 2, 3, 4, 5, 6,
        3, 0xB1, 0xB2, 0xB3,
        2, 0xA1, 0xA2
    };
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0], expected);
}

TEST_F(CompanionProtocolTest, PathDiscoveryPushRejectsInvalidPaths) {
    const uint8_t prefix[6] = {};
    const uint8_t path[1] = {};

    EXPECT_FALSE(bridge.pushPathDiscoveryResponse(
        prefix, path, 0xC1, path, 1));
    EXPECT_FALSE(bridge.pushPathDiscoveryResponse(
        prefix, nullptr, 1, path, 1));
    EXPECT_TRUE(serial.writes.empty());
}

TEST_F(CompanionProtocolTest, PushLoginStatusTelemetryTrace) {
    uint8_t prefix[6] = { 1, 2, 3, 4, 5, 6 };
    EXPECT_TRUE(bridge.pushLoginResult(prefix, true, 3, 0x12345678u, 0xA5, 13));
    const std::vector<uint8_t> expected_login = {
        cc::PUSH_CODE_LOGIN_SUCCESS, 3, 1, 2, 3, 4, 5, 6,
        0x78, 0x56, 0x34, 0x12, 0xA5, 13,
    };
    EXPECT_EQ(serial.writes[0], expected_login);

    serial.writes.clear();
    EXPECT_TRUE(bridge.pushLoginResult(prefix, false, 0, 0, 0, 0));
    const std::vector<uint8_t> expected_failure = {
        cc::PUSH_CODE_LOGIN_FAIL, 0, 1, 2, 3, 4, 5, 6,
    };
    EXPECT_EQ(serial.writes[0], expected_failure);

    serial.writes.clear();
    EXPECT_TRUE(bridge.pushLoginResult(prefix, true, 0, 0, 0, 0,
                                       /*legacy_success=*/true));
    const std::vector<uint8_t> expected_legacy = {
        cc::PUSH_CODE_LOGIN_SUCCESS, 0, 1, 2, 3, 4, 5, 6,
    };
    EXPECT_EQ(serial.writes[0], expected_legacy);

    serial.writes.clear();
    uint8_t blob[3] = { 0xDE, 0xAD, 0xBE };
    EXPECT_TRUE(bridge.pushStatusResponse(prefix, blob, sizeof(blob)));
    EXPECT_EQ(serial.writes[0][0], cc::PUSH_CODE_STATUS_RESPONSE);
    EXPECT_EQ(serial.writes[0].size(), (size_t)(2 + 6 + 3));

    serial.writes.clear();
    EXPECT_TRUE(bridge.pushTelemetryResponse(prefix, blob, sizeof(blob)));
    EXPECT_EQ(serial.writes[0][0], cc::PUSH_CODE_TELEMETRY_RESPONSE);

    serial.writes.clear();
    uint8_t hashes[2] = { 0x11, 0x22 };
    uint8_t snrs[2] = { 0x40, 0x41 };
    EXPECT_TRUE(bridge.pushTraceData(0x12345678u, 0, 0, hashes, snrs, 2, -8));
    const auto& t = serial.writes[0];
    EXPECT_EQ(t[0], cc::PUSH_CODE_TRACE_DATA);
    EXPECT_EQ(t[2], 2);     // path_len
    EXPECT_EQ(t[3], 0);     // flags
    // [4..7] tag, [8..11] auth, [12..13] hashes, [14..15] snrs, [16] final snr
    ASSERT_EQ(t.size(), 17u);
    EXPECT_EQ((int8_t)t[16], -8);
}

TEST_F(CompanionProtocolTest, StatusAndTelemetryPushesRejectOversizedBlobs) {
    uint8_t prefix[cc::SIGURDOS_COMPANION_PUB_KEY_PREFIX_SIZE] =
        {1, 2, 3, 4, 5, 6};
    std::vector<uint8_t> maximum(cc::SIGURDOS_COMPANION_PUSH_BLOB_MAX_PAYLOAD,
                                 0xA5);

    ASSERT_TRUE(bridge.pushStatusResponse(prefix, maximum.data(), maximum.size()));
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0].size(), (size_t)MAX_FRAME_SIZE);
    EXPECT_EQ(std::vector<uint8_t>(serial.writes[0].begin() + 8,
                                   serial.writes[0].end()), maximum);

    serial.writes.clear();
    ASSERT_TRUE(bridge.pushTelemetryResponse(prefix, maximum.data(), maximum.size()));
    ASSERT_EQ(serial.writes.size(), 1U);
    EXPECT_EQ(serial.writes[0].size(), (size_t)MAX_FRAME_SIZE);

    maximum.push_back(0x5A);
    serial.writes.clear();
    EXPECT_FALSE(bridge.pushStatusResponse(prefix, maximum.data(), maximum.size()));
    EXPECT_FALSE(bridge.pushTelemetryResponse(prefix, maximum.data(), maximum.size()));
    EXPECT_FALSE(bridge.pushStatusResponse(prefix, nullptr, 1));
    EXPECT_FALSE(bridge.pushTelemetryResponse(prefix, nullptr, 1));
    EXPECT_TRUE(serial.writes.empty());
}

TEST_F(CompanionProtocolTest, TracePushUsesHashBytesAndHopSnrCountSeparately) {
    uint8_t hashes[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t snrs[4] = {10, 11, 12, 13};

    ASSERT_TRUE(bridge.pushTraceData(1, 2, 1, hashes, snrs, 8, -4));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0].size(), 25u);  // 12 + 8 hashes + 4 SNRs + final

    serial.writes.clear();
    ASSERT_TRUE(bridge.pushTraceData(1, 2, 2, hashes, snrs, 8, -4));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0].size(), 23u);  // 12 + 8 hashes + 2 SNRs + final

    serial.writes.clear();
    EXPECT_FALSE(bridge.pushTraceData(1, 2, 1, hashes, snrs, 7, -4));
    EXPECT_TRUE(serial.writes.empty());
    EXPECT_FALSE(bridge.pushTraceData(1, 2, 0, hashes, nullptr, 1, -4));
}

TEST_F(CompanionProtocolTest, PushRawDataMatchesStockFrameLayout) {
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    ASSERT_TRUE(bridge.pushRawData(-8, -91, payload, sizeof(payload)));
    ASSERT_EQ(serial.writes.size(), 1u);
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 8u);
    EXPECT_EQ(out[0], cc::PUSH_CODE_RAW_DATA);
    EXPECT_EQ((int8_t)out[1], -8);
    EXPECT_EQ((int8_t)out[2], -91);
    EXPECT_EQ(out[3], 0xFF);
    EXPECT_EQ(out[4], 0xDE);

    serial.connected = false;
    EXPECT_FALSE(bridge.pushRawData(0, 0, payload, sizeof(payload)));
}

TEST_F(CompanionProtocolTest, PushControlDataMatchesPinnedFrameLayout) {
    EXPECT_EQ((uint8_t)cc::PUSH_CODE_LOG_RX_DATA, 0x88);
    EXPECT_EQ((uint8_t)cc::PUSH_CODE_BINARY_RESPONSE, 0x8C);
    EXPECT_EQ((uint8_t)cc::PUSH_CODE_PATH_DISCOVERY_RESPONSE, 0x8D);
    EXPECT_EQ((uint8_t)cc::PUSH_CODE_CONTROL_DATA, 0x8E);

    uint8_t payload[] = {0x80, 0x01, 0x02};
    ASSERT_TRUE(bridge.pushControlData(12, -70, 0, payload, sizeof(payload)));
    ASSERT_EQ(serial.writes.size(), 1u);
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 7u);
    EXPECT_EQ(out[0], 0x8E);
    EXPECT_EQ((int8_t)out[1], 12);
    EXPECT_EQ((int8_t)out[2], -70);
    EXPECT_EQ(out[3], 0);
    EXPECT_EQ(out[4], 0x80);
}

TEST_F(CompanionProtocolTest, SendChannelDataFloodDispatchesToHostAndReturnsOk) {
    uint8_t frame[] = {
        sigurdos::comms::CMD_SEND_CHANNEL_DATA,
        0,
        0xFF,
        0xFF, 0xFF,
        0xA1, 0xB2, 0xC3,
    };

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_TRUE(host.sent_channel_data);
    EXPECT_EQ(host.last_channel_data_index, 0);
    EXPECT_EQ(host.last_channel_data_path_len, 0xFF);
    EXPECT_EQ(host.last_channel_data_type, 0xFFFF);
    ASSERT_EQ(host.last_channel_data_payload.size(), 3u);
    EXPECT_EQ(host.last_channel_data_payload[0], 0xA1);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_OK);
}

TEST_F(CompanionProtocolTest, SetAdvertNameDispatchesUnterminatedPayload) {
    uint8_t frame[16]{};
    frame[0] = sigurdos::comms::CMD_SET_ADVERT_NAME;
    std::memcpy(&frame[1], "TrailNode", 9);

    ASSERT_TRUE(bridge.handleFrame(frame, 10));
    ASSERT_TRUE(host.set_advert_name_called);
    EXPECT_STREQ(host.advert_name, "TrailNode");
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_OK);
}

TEST_F(CompanionProtocolTest, SetAdvertNameRejectsEmptyName) {
    uint8_t frame[] = {sigurdos::comms::CMD_SET_ADVERT_NAME, 0};

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_TRUE(host.set_advert_name_called);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, SetAdvertLatLonDispatchesFixedPointCoordinates) {
    uint8_t frame[9]{};
    frame[0] = sigurdos::comms::CMD_SET_ADVERT_LATLON;
    int32_t lat = 45123456;
    int32_t lon = -73543210;
    std::memcpy(&frame[1], &lat, 4);
    std::memcpy(&frame[5], &lon, 4);

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_TRUE(host.set_advert_latlon_called);
    EXPECT_EQ(host.advert_lat, lat);
    EXPECT_EQ(host.advert_lon, lon);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_OK);
}

TEST_F(CompanionProtocolTest, SetAdvertLatLonRejectsShortPayload) {
    uint8_t frame[] = {sigurdos::comms::CMD_SET_ADVERT_LATLON, 0, 0, 0};

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_FALSE(host.set_advert_latlon_called);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, SetRadioParamsDispatchesOfficialPayload) {
    uint8_t frame[12]{};
    int i = 0;
    frame[i++] = sigurdos::comms::CMD_SET_RADIO_PARAMS;
    uint32_t freq_khz = 869525;
    uint32_t bw_hz = 250000;
    std::memcpy(&frame[i], &freq_khz, 4);
    i += 4;
    std::memcpy(&frame[i], &bw_hz, 4);
    i += 4;
    frame[i++] = 10;
    frame[i++] = 5;
    frame[i++] = 1;

    ASSERT_TRUE(bridge.handleFrame(frame, i));
    ASSERT_TRUE(host.set_radio_params_called);
    EXPECT_EQ(host.radio_freq_khz, freq_khz);
    EXPECT_EQ(host.radio_bw_hz, bw_hz);
    EXPECT_EQ(host.radio_sf, 10);
    EXPECT_EQ(host.radio_cr, 5);
    EXPECT_EQ(host.radio_client_repeat, 1);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_OK);
}

TEST_F(CompanionProtocolTest, SetRadioParamsRejectsShortPayload) {
    uint8_t frame[] = {sigurdos::comms::CMD_SET_RADIO_PARAMS, 0, 0, 0};

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_FALSE(host.set_radio_params_called);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, SetRadioParamsRejectsInvalidRange) {
    uint8_t frame[11]{};
    int i = 0;
    frame[i++] = sigurdos::comms::CMD_SET_RADIO_PARAMS;
    uint32_t freq_khz = 399999;
    uint32_t bw_hz = 250000;
    std::memcpy(&frame[i], &freq_khz, 4);
    i += 4;
    std::memcpy(&frame[i], &bw_hz, 4);
    i += 4;
    frame[i++] = 10;
    frame[i++] = 5;

    ASSERT_TRUE(bridge.handleFrame(frame, i));
    ASSERT_TRUE(host.set_radio_params_called);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, SetRadioTxPowerDispatchesSignedByte) {
    uint8_t frame[] = {sigurdos::comms::CMD_SET_RADIO_TX_POWER, 22};

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_TRUE(host.set_radio_tx_power_called);
    EXPECT_EQ(host.radio_tx_power_dbm, 22);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_OK);
}

TEST_F(CompanionProtocolTest, SetRadioTxPowerRejectsMissingPower) {
    uint8_t frame[] = {sigurdos::comms::CMD_SET_RADIO_TX_POWER};

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_FALSE(host.set_radio_tx_power_called);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, SetRadioTxPowerRejectsInvalidPower) {
    uint8_t frame[] = {sigurdos::comms::CMD_SET_RADIO_TX_POWER, 23};

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_TRUE(host.set_radio_tx_power_called);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, GetTuningParamsReturnsOfficialFrame) {
    host.tuning_rx_delay_base_x1000 = 15000;
    host.tuning_tx_delay_factor_x1000 = 1500;
    uint8_t frame[] = {sigurdos::comms::CMD_GET_TUNING_PARAMS};

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_EQ(serial.writes.size(), 1u);
    const auto& out = serial.writes[0];
    ASSERT_EQ(out.size(), 9u);
    EXPECT_EQ(out[0], sigurdos::comms::RESP_CODE_TUNING_PARAMS);
    uint32_t rx_delay_base_x1000 = 0;
    uint32_t tx_delay_factor_x1000 = 0;
    std::memcpy(&rx_delay_base_x1000, &out[1], 4);
    std::memcpy(&tx_delay_factor_x1000, &out[5], 4);
    EXPECT_EQ(rx_delay_base_x1000, 15000u);
    EXPECT_EQ(tx_delay_factor_x1000, 1500u);
}

TEST_F(CompanionProtocolTest, SetTuningParamsDispatchesOfficialPayload) {
    uint8_t frame[9]{};
    frame[0] = sigurdos::comms::CMD_SET_TUNING_PARAMS;
    uint32_t rx_delay_base_x1000 = 12000;
    uint32_t tx_delay_factor_x1000 = 500;
    std::memcpy(&frame[1], &rx_delay_base_x1000, 4);
    std::memcpy(&frame[5], &tx_delay_factor_x1000, 4);

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_TRUE(host.set_tuning_params_called);
    EXPECT_EQ(host.tuning_rx_delay_base_x1000, rx_delay_base_x1000);
    EXPECT_EQ(host.tuning_tx_delay_factor_x1000, tx_delay_factor_x1000);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_OK);
}

TEST_F(CompanionProtocolTest, SetTuningParamsRejectsShortPayload) {
    uint8_t frame[] = {sigurdos::comms::CMD_SET_TUNING_PARAMS, 0, 0, 0};

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_FALSE(host.set_tuning_params_called);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, SetTuningParamsRejectsInvalidRange) {
    uint8_t frame[9]{};
    frame[0] = sigurdos::comms::CMD_SET_TUNING_PARAMS;
    uint32_t rx_delay_base_x1000 = 20001;
    uint32_t tx_delay_factor_x1000 = 1000;
    std::memcpy(&frame[1], &rx_delay_base_x1000, 4);
    std::memcpy(&frame[5], &tx_delay_factor_x1000, 4);

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_TRUE(host.set_tuning_params_called);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, SendChannelDataDirectPathDispatchesToHost) {
    uint8_t frame[] = {
        sigurdos::comms::CMD_SEND_CHANNEL_DATA,
        0,
        0x02,
        0x11, 0x22,
        0x34, 0x12,
        0x99,
    };

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_TRUE(host.sent_channel_data);
    EXPECT_EQ(host.last_channel_data_path_len, 0x02);
    ASSERT_EQ(host.last_channel_data_path.size(), 2u);
    EXPECT_EQ(host.last_channel_data_path[0], 0x11);
    EXPECT_EQ(host.last_channel_data_path[1], 0x22);
    EXPECT_EQ(host.last_channel_data_type, 0x1234);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_OK);
}

TEST_F(CompanionProtocolTest, SendChannelDataRejectsReservedDataType) {
    uint8_t frame[] = {
        sigurdos::comms::CMD_SEND_CHANNEL_DATA,
        0,
        0xFF,
        0x00, 0x00,
    };

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_FALSE(host.sent_channel_data);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, SendChannelDataRejectsInvalidPathEncoding) {
    uint8_t frame[] = {
        sigurdos::comms::CMD_SEND_CHANNEL_DATA,
        0,
        0xC1,
        0xAA,
        0xFF, 0xFF,
    };

    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_FALSE(host.sent_channel_data);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, SendChannelDataRejectsOversizePayload) {
    std::vector<uint8_t> frame;
    frame.push_back(sigurdos::comms::CMD_SEND_CHANNEL_DATA);
    frame.push_back(0);
    frame.push_back(0xFF);
    frame.push_back(0xFF);
    frame.push_back(0xFF);
    frame.resize(5 + sigurdos::comms::SIGURDOS_COMPANION_CHANNEL_DATA_MAX_PAYLOAD + 1, 0x55);

    ASSERT_TRUE(bridge.handleFrame(frame.data(), frame.size()));
    ASSERT_FALSE(host.sent_channel_data);
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], sigurdos::comms::ERR_CODE_ILLEGAL_ARG);
}

TEST_F(CompanionProtocolTest, EnqueuedChannelDataTicklesAndDrains) {
    uint8_t payload[] = {0xDE, 0xAD};
    ASSERT_TRUE(bridge.enqueueChannelData(1, -8, 0xFF, 0xBEEF, payload, sizeof(payload)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::PUSH_CODE_MSG_WAITING);

    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 2u);
    const auto& out = serial.writes[1];
    ASSERT_EQ(out.size(), 11u);
    EXPECT_EQ(out[0], sigurdos::comms::RESP_CODE_CHANNEL_DATA_RECV);
    EXPECT_EQ((int8_t)out[1], -8);
    EXPECT_EQ(out[4], 1);
    EXPECT_EQ(out[5], 0xFF);
    EXPECT_EQ(out[6], 0xEF);
    EXPECT_EQ(out[7], 0xBE);
    EXPECT_EQ(out[8], 2);
    EXPECT_EQ(out[9], 0xDE);
    EXPECT_EQ(out[10], 0xAD);
}

TEST_F(CompanionProtocolTest, EnqueuedChannelDataRejectsReservedTypeAndInvalidPath) {
    uint8_t payload[] = {0x01};
    EXPECT_FALSE(bridge.enqueueChannelData(1, 0, 0xFF, 0x0000, payload, sizeof(payload)));
    EXPECT_FALSE(bridge.enqueueChannelData(1, 0, 0xC1, 0xBEEF, payload, sizeof(payload)));
    EXPECT_TRUE(serial.writes.empty());
}

// ── Sync per-record marking ─────────────────────────────────────

TEST_F(CompanionProtocolTest, SyncDrainMarksPerRecordNotAll) {
    // Enqueue two messages, drain one. Only the drained record should be marked.
    sigurdos::mesh::StoredMessage m1{};
    std::strncpy(m1.conversation, "DM: Alice", sizeof(m1.conversation) - 1);
    std::strncpy(m1.sender, "Alice", sizeof(m1.sender) - 1);
    std::strncpy(m1.text, "hello", sizeof(m1.text) - 1);
    m1.timestamp = 1;
    for (int i = 0; i < 6; i++) m1.sender_prefix[i] = (uint8_t)(0xA0 + i);

    sigurdos::mesh::StoredMessage m2{};
    std::strncpy(m2.conversation, "DM: Bob", sizeof(m2.conversation) - 1);
    std::strncpy(m2.sender, "Bob", sizeof(m2.sender) -  1);
    std::strncpy(m2.text, "world", sizeof(m2.text) - 1);
    m2.timestamp = 2;
    for (int i = 0; i < 6; i++) m2.sender_prefix[i] = (uint8_t)(0xB0 + i);

    // Store both and mark neither as sent. Verify store_id is assigned.
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(m1));
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(m2));

    sigurdos::mesh::StoredMessage stored[4]{};
    int n = sigurdos::mesh::messageStoreLoadAll(stored, 4);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(stored[0].store_id, 1u);
    ASSERT_EQ(stored[1].store_id, 2u);

    uint8_t start[8] = {sigurdos::comms::CMD_APP_START};
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    uint8_t sync[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));

    sigurdos::mesh::StoredMessage verify[4]{};
    n = sigurdos::mesh::messageStoreLoadAll(verify, 4);
    ASSERT_EQ(n, 2);
    EXPECT_FALSE(verify[0].companion_sent);
    EXPECT_FALSE(verify[1].companion_sent);

    // Asking for the next record acknowledges only the prior in-flight one.
    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
    n = sigurdos::mesh::messageStoreLoadAll(verify, 4);
    ASSERT_EQ(n, 2);
    EXPECT_TRUE(verify[0].companion_sent);
    EXPECT_FALSE(verify[1].companion_sent);
}

// ── Frame metadata ─────────────────────────────────────────────

TEST_F(CompanionProtocolTest, SignedMessageV3FrameMatchesGoldenBytes) {
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Carol", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Carol", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "secret", sizeof(msg.text) - 1);
    msg.timestamp = 0x01020304u;
    msg.snr_quarters = -8;
    msg.path_len = 0xFF;
    msg.txt_type = 2;  // COMPANION_TXT_SIGNED_PLAIN
    msg.extra_len = 4;
    msg.extra[0] = 0xAA; msg.extra[1] = 0xBB; msg.extra[2] = 0xCC; msg.extra[3] = 0xDD;
    for (int i = 0; i < 6; i++) msg.sender_prefix[i] = (uint8_t)(0xE0 + i);

    ASSERT_TRUE(bridge.enqueueMessage(msg));
    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_GE(serial.writes.size(), 2u);
    const auto& out = serial.writes[1];
    const std::vector<uint8_t> expected = {
        sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV_V3,
        0xF8, 0, 0, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xFF, 2,
        0x04, 0x03, 0x02, 0x01, 0xAA, 0xBB, 0xCC, 0xDD,
        's', 'e', 'c', 'r', 'e', 't',
    };
    EXPECT_EQ(out, expected);
}

TEST_F(CompanionProtocolTest, PersistedSignedMessageV2ReplayMatchesGoldenBytes) {
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Carol", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Carol", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "secret", sizeof(msg.text) - 1);
    msg.timestamp = 0x01020304u;
    msg.path_len = 0xFF;
    msg.txt_type = 2;
    msg.extra_len = 4;
    msg.extra[0] = 0xAA;
    msg.extra[1] = 0xBB;
    msg.extra[2] = 0xCC;
    msg.extra[3] = 0xDD;
    for (int i = 0; i < 6; i++) msg.sender_prefix[i] = (uint8_t)(0xE0 + i);
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg));

    const uint8_t query[] = {sigurdos::comms::CMD_DEVICE_QUERY, 2};
    ASSERT_TRUE(bridge.handleFrame(query, sizeof(query)));
    serial.writes.clear();
    const uint8_t start[8] = {sigurdos::comms::CMD_APP_START};
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    const uint8_t sync[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
    ASSERT_EQ(serial.writes.size(), 3u);
    const std::vector<uint8_t> expected = {
        sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV,
        0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xFF, 2,
        0x04, 0x03, 0x02, 0x01, 0xAA, 0xBB, 0xCC, 0xDD,
        's', 'e', 'c', 'r', 'e', 't',
    };
    EXPECT_EQ(serial.writes[2], expected);
}

TEST_F(CompanionProtocolTest, CliDataFrameEmitsCorrectTxtType) {
    // CLI messages should emit txt_type=1 in the frame.
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "ping", sizeof(msg.text) - 1);
    msg.timestamp = 42;
    msg.txt_type = 1;  // COMPANION_TXT_CLI_DATA
    for (int i = 0; i < 6; i++) msg.sender_prefix[i] = (uint8_t)(0xA0 + i);

    ASSERT_TRUE(bridge.enqueueMessage(msg));
    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_GE(serial.writes.size(), 2u);
    const auto& out = serial.writes[1];
    EXPECT_EQ(out[11], 1u);  // txt_type == COMPANION_TXT_CLI_DATA
}

TEST_F(CompanionProtocolTest, PersistedCliDataSurvivesOfflineV3Sync) {
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "version 1.2", sizeof(msg.text) - 1);
    msg.timestamp = 0x01020304u;
    msg.txt_type = sigurdos::comms::COMPANION_TXT_CLI_DATA;
    for (int i = 0; i < 6; i++) msg.sender_prefix[i] = (uint8_t)(0xA0 + i);
    ASSERT_TRUE(sigurdos::mesh::messageStoreAppend(msg));

    uint8_t start[8] = {sigurdos::comms::CMD_APP_START};
    ASSERT_TRUE(bridge.handleFrame(start, sizeof(start)));
    ASSERT_EQ(serial.writes.size(), 2u);
    EXPECT_EQ(serial.writes[1][0], sigurdos::comms::PUSH_CODE_MSG_WAITING);

    uint8_t sync[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
    ASSERT_EQ(serial.writes.size(), 3u);
    const auto& out = serial.writes[2];
    ASSERT_GE(out.size(), 16u);
    EXPECT_EQ(out[0], sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV_V3);
    EXPECT_EQ(out[11], sigurdos::comms::COMPANION_TXT_CLI_DATA);
    EXPECT_EQ(std::string(out.begin() + 16, out.end()), "version 1.2");

    sigurdos::mesh::StoredMessage stored{};
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&stored, 1), 1);
    EXPECT_EQ(stored.txt_type, sigurdos::comms::COMPANION_TXT_CLI_DATA);
    EXPECT_FALSE(stored.companion_sent);
    ASSERT_TRUE(bridge.handleFrame(sync, sizeof(sync)));
    ASSERT_EQ(sigurdos::mesh::messageStoreLoadAll(&stored, 1), 1);
    EXPECT_TRUE(stored.companion_sent);
}

// ── Channel slot semantics ──────────────────────────────────────

TEST_F(CompanionProtocolTest, GetChannelEmptySlotReturnsChannelInfo) {
    // Fixed-slot semantics: GET_CHANNEL for any valid index should return
    // CHANNEL_INFO with empty name/secret, not NOT_FOUND.
    host.channel_count = 40;

    uint8_t cmd[] = {sigurdos::comms::CMD_GET_CHANNEL, 5};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 1u);
    const auto& out = serial.writes[0];
    ASSERT_GE(out.size(), 50u);
    EXPECT_EQ(out[0], sigurdos::comms::RESP_CODE_CHANNEL_INFO);
    EXPECT_EQ(out[1], 5);  // channel_idx
    // name should be empty for non-zero index (first byte NUL)
    EXPECT_EQ(out[2], 0);
}

TEST_F(CompanionProtocolTest, SetChannelEmptyNameClearsSlot) {
    // Clearing a channel by setting an empty name should succeed, not return
    // ERR_NOT_FOUND or ERR_ILLEGAL_ARG.
    uint8_t frame[2 + 32 + 16]{};
    frame[0] = sigurdos::comms::CMD_SET_CHANNEL;
    frame[1] = 3;  // channel_idx
    // name: all zeros (empty), secret: all zeros
    ASSERT_TRUE(bridge.handleFrame(frame, sizeof(frame)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_OK);
}

} // namespace
