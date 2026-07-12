#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "comms/companion_bridge.h"

namespace {

class MockSerial final : public BaseSerialInterface {
public:
    bool enabled = false;
    bool connected = true;
    bool busy = false;
    int fail_writes = 0;
    std::vector<std::vector<uint8_t>> writes;

    void enable() override { enabled = true; }
    void disable() override { enabled = false; }
    bool isEnabled() const override { return enabled; }
    bool isConnected() const override { return connected; }
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
    bool cancelled_binary = false;
    std::vector<uint8_t> last_binary_data;
    std::vector<uint8_t> last_anon_data;
    uint8_t last_anon_key[32]{};
    bool sent_channel_data = false;
    bool sent_raw_data = false;
    uint8_t last_prefix[6]{};
    int last_channel_data_index = -1;
    uint8_t last_channel_data_path_len = 0;
    uint16_t last_channel_data_type = 0;
    std::vector<uint8_t> last_channel_data_path;
    std::vector<uint8_t> last_channel_data_payload;
    std::vector<uint8_t> last_raw_path;
    std::vector<uint8_t> last_raw_payload;
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

    uint8_t path_hash_mode = 0;

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
    }

    uint32_t currentTime() const override { return now; }
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
                      uint32_t& tx_delay_factor_x1000) const override {
        rx_delay_base_x1000 = tuning_rx_delay_base_x1000;
        tx_delay_factor_x1000 = tuning_tx_delay_factor_x1000;
    }
    bool setTuningParams(uint32_t rx_delay_base_x1000,
                         uint32_t tx_delay_factor_x1000) override {
        set_tuning_params_called = true;
        tuning_rx_delay_base_x1000 = rx_delay_base_x1000;
        tuning_tx_delay_factor_x1000 = tx_delay_factor_x1000;
        last_rx_base = rx_delay_base_x1000;
        last_airtime = tx_delay_factor_x1000;
        return rx_delay_base_x1000 <= 20000 && tx_delay_factor_x1000 <= 2000;
    }
    bool setBlePin(uint32_t) override { return true; }
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
    sigurdos::comms::CompanionContact last_added{};
    bool     has_connection = false;
    bool     rebooted = false, factory_reset_called = false;
    bool     scope_is_set = false;
    char     last_scope_name[32]{};
    bool     scope_unscoped = false, scope_cleared = false;
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
        last_added = c; return add_contact_ok;
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
    void setDefaultFloodScope(const char* name, const uint8_t*) override {
        if (!name || !name[0]) { scope_cleared = true; last_scope_name[0] = '\0'; }
        else std::strncpy(last_scope_name, name, sizeof(last_scope_name) - 1);
    }
    void setFloodScopeOverride(const uint8_t* key, bool unscoped) override {
        scope_unscoped = unscoped; scope_cleared = (!unscoped && !key);
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
        return {true, false, next_binary_tag++, 3000};
    }
    sigurdos::comms::CompanionSendResult sendAnonReq(
        const uint8_t* pub_key, const uint8_t* data, uint8_t data_len) override {
        sent_anon = true;
        ++anon_send_calls;
        std::memcpy(last_anon_key, pub_key, sizeof(last_anon_key));
        last_anon_data.assign(data, data + data_len);
        if (!last_send_ok) return {false, false, 0, 0};
        return {true, true, next_anon_tag++, 4500};
    }
    void cancelBinaryReqs() override { cancelled_binary = true; }
    sigurdos::comms::CompanionSendResult sendTracePath(uint32_t tag, uint32_t, uint8_t,
                                                       const uint8_t*, uint8_t path_len) override {
        last_trace_tag = tag; last_trace_path_len = path_len;
        return {last_send_ok, false, tag, 4000};
    }
    void selfTelemetry(uint8_t*, size_t* out_len) const override { if (out_len) *out_len = 0; }
    int getCustomVars(char*, size_t) const override { return 0; }  // empty by default
    bool setCustomVar(const char*, const char*) override { return false; }  // fail by default
    int signData(const uint8_t*, size_t len, uint8_t* sig_out) override {
        sign_len_seen = (int)len;
        std::memset(sig_out, 0xAB, 64);
        return 64;
    }
    sigurdos::comms::CompanionSendResult sendPathDiscovery(const uint8_t*) override {
        return {false, false, 0, 0};  // not found by default
    }
    uint8_t getAdvertPath(const uint8_t*, uint8_t*, uint8_t,
                          uint32_t*) const override { return 0; }
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
    EXPECT_TRUE(stored.companion_sent);
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
    msg.store_id = 42;
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

TEST_F(CompanionProtocolTest, DisconnectDuringDrainKeepsFrameForReconnect) {
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Alice", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Alice", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "reconnect", sizeof(msg.text) - 1);
    msg.timestamp = 78;
    msg.store_id = 43;
    ASSERT_TRUE(bridge.enqueueMessage(msg));
    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};

    serial.connected = false;
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    serial.connected = true;
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 2U); // tickle + retried frame
    EXPECT_EQ(serial.writes[1][0], sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV_V3);
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

    serial.writes.clear();
    std::vector<uint8_t> set(1 + 31 + 16, 0);
    set[0] = cc::CMD_SET_DEFAULT_FLOOD_SCOPE;
    std::strcpy((char*)&set[1], "#crew");
    ASSERT_TRUE(bridge.handleFrame(set.data(), set.size()));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
    EXPECT_STREQ(host.last_scope_name, "#crew");

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
    ASSERT_TRUE(bridge.handleFrame(setkey, sizeof(setkey)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_OK);
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

TEST_F(CompanionProtocolTest, SignDataBeforeStartFails) {
    uint8_t data[3] = { cc::CMD_SIGN_DATA, 'x', 'y' };
    ASSERT_TRUE(bridge.handleFrame(data, sizeof(data)));
    EXPECT_EQ(serial.writes[0][0], cc::RESP_CODE_ERR);
    EXPECT_EQ(serial.writes[0][1], cc::ERR_CODE_BAD_STATE);
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

TEST_F(CompanionProtocolTest, PushLoginStatusTelemetryTrace) {
    uint8_t prefix[6] = { 1, 2, 3, 4, 5, 6 };
    EXPECT_TRUE(bridge.pushLoginResult(prefix, true, 3, false));
    EXPECT_EQ(serial.writes[0][0], cc::PUSH_CODE_LOGIN_SUCCESS);
    EXPECT_EQ(serial.writes[0][1], 3);

    serial.writes.clear();
    EXPECT_TRUE(bridge.pushLoginResult(prefix, false, 0, false));
    EXPECT_EQ(serial.writes[0][0], cc::PUSH_CODE_LOGIN_FAIL);

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
    EXPECT_TRUE(verify[0].companion_sent);
    EXPECT_FALSE(verify[1].companion_sent);
}

// ── Frame metadata ─────────────────────────────────────────────

TEST_F(CompanionProtocolTest, SignedMessageFrameIncludesExtraBytes) {
    // Regression: signed messages should include the 4-byte sender prefix extra
    // between txt_type and timestamp in the DM V3 frame.
    sigurdos::mesh::StoredMessage msg{};
    std::strncpy(msg.conversation, "DM: Carol", sizeof(msg.conversation) - 1);
    std::strncpy(msg.sender, "Carol", sizeof(msg.sender) - 1);
    std::strncpy(msg.text, "secret", sizeof(msg.text) - 1);
    msg.timestamp = 0x01020304u;
    msg.txt_type = 2;  // COMPANION_TXT_SIGNED_PLAIN
    msg.extra_len = 4;
    msg.extra[0] = 0xAA; msg.extra[1] = 0xBB; msg.extra[2] = 0xCC; msg.extra[3] = 0xDD;
    for (int i = 0; i < 6; i++) msg.sender_prefix[i] = (uint8_t)(0xE0 + i);

    ASSERT_TRUE(bridge.enqueueMessage(msg));
    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_GE(serial.writes.size(), 2u);
    const auto& out = serial.writes[1];
    ASSERT_GE(out.size(), 20u);
    EXPECT_EQ(out[0], sigurdos::comms::RESP_CODE_CONTACT_MSG_RECV_V3);
    // [1] snr, [2] res1, [3] res2, [4..9] sender_prefix, [10] path_len,
    // [11] txt_type (should be 2), [12..15] extra 4 bytes, [16..19] timestamp
    EXPECT_EQ(out[11], 2u);         // COMPANION_TXT_SIGNED_PLAIN
    EXPECT_EQ(out[12], 0xAA);       // extra[0]
    EXPECT_EQ(out[13], 0xBB);       // extra[1]
    EXPECT_EQ(out[14], 0xCC);       // extra[2]
    EXPECT_EQ(out[15], 0xDD);       // extra[3]
    uint32_t ts = 0;
    std::memcpy(&ts, &out[16], 4);
    EXPECT_EQ(ts, 0x01020304u);
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
