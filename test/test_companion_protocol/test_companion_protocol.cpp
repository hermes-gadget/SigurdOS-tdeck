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
    uint8_t last_prefix[6]{};
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
    bool sendAdvert(bool) override { return true; }
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

} // namespace
