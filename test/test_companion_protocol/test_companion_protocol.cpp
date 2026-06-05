#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <vector>

#include "comms/companion_bridge.h"

namespace {

class MockSerial final : public BaseSerialInterface {
public:
    bool enabled = false;
    bool connected = true;
    std::vector<std::vector<uint8_t>> writes;

    void enable() override { enabled = true; }
    void disable() override { enabled = false; }
    bool isEnabled() const override { return enabled; }
    bool isConnected() const override { return connected; }
    bool isWriteBusy() const override { return false; }
    size_t writeFrame(const uint8_t src[], size_t len) override {
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
    bool sent_channel_data = false;
    uint8_t last_prefix[6]{};
    int last_channel_data_index = -1;
    uint8_t last_channel_data_path_len = 0;
    uint16_t last_channel_data_type = 0;
    std::vector<uint8_t> last_channel_data_path;
    std::vector<uint8_t> last_channel_data_payload;
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
    uint32_t now = 1234;

    uint32_t blePin() const override { return 123456; }
    uint8_t clientRepeat() const override { return 0; }
    uint8_t pathHashMode() const override { return 0; }
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

    int channelCount() const override { return 1; }
    bool getChannel(int index, sigurdos::comms::CompanionChannel& out) const override {
        if (index != 0) return false;
        std::memset(&out, 0, sizeof(out));
        std::strncpy(out.name, "#test", sizeof(out.name) - 1);
        return true;
    }
    bool setChannel(int, const sigurdos::comms::CompanionChannel&) override { return true; }

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
    bool sendAdvert(bool) override { return true; }
    bool setAdvertName(const char* name) override {
        set_advert_name_called = true;
        if (!name || !name[0]) return false;
        std::strncpy(advert_name, name, sizeof(advert_name) - 1);
        advert_name[sizeof(advert_name) - 1] = '\0';
        return true;
    }
    bool setAdvertLatLon(int32_t lat, int32_t lon) override {
        set_advert_latlon_called = true;
        advert_lat = lat;
        advert_lon = lon;
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
        return freq_khz >= 400000 && freq_khz <= 1000000 &&
               bw_hz >= 7800 && bw_hz <= 500000 &&
               sf >= 6 && sf <= 12 &&
               cr >= 5 && cr <= 8 &&
               client_repeat <= 1;
    }
    bool setRadioTxPower(int8_t tx_power_dbm) override {
        set_radio_tx_power_called = true;
        radio_tx_power_dbm = tx_power_dbm;
        return tx_power_dbm >= 2 && tx_power_dbm <= 22;
    }
    bool setBlePin(uint32_t) override { return true; }
    bool exportPrivateKey(uint8_t* out64) const override {
        std::memset(out64, 0x42, 64);
        return true;
    }
    bool importPrivateKey(const uint8_t*) override { return true; }
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
    EXPECT_EQ(out[2], 32);
    EXPECT_EQ(out[3], 8);
    uint32_t pin = 0;
    std::memcpy(&pin, &out[4], 4);
    EXPECT_EQ(pin, 123456u);
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
    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 1u);
    EXPECT_EQ(serial.writes[0][0], sigurdos::comms::RESP_CODE_NO_MORE_MESSAGES);
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

    uint8_t cmd[] = {sigurdos::comms::CMD_SYNC_NEXT_MESSAGE};
    ASSERT_TRUE(bridge.handleFrame(cmd, sizeof(cmd)));
    ASSERT_EQ(serial.writes.size(), 2u);
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

} // namespace
