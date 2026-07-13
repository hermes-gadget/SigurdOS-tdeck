// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "comms/companion_usb_serial.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace {

class ByteStream : public Stream {
public:
    int available() override { return static_cast<int>(rx.size()); }

    int read() override {
        if (rx.empty()) return -1;
        const uint8_t value = rx.front();
        rx.pop_front();
        return value;
    }

    size_t write(uint8_t value) override {
        if (tx.size() >= write_limit) return 0;
        tx.push_back(value);
        return 1;
    }

    void queue(std::initializer_list<uint8_t> bytes) {
        rx.insert(rx.end(), bytes.begin(), bytes.end());
    }

    void queue(const std::vector<uint8_t>& bytes) {
        rx.insert(rx.end(), bytes.begin(), bytes.end());
    }

    std::deque<uint8_t> rx;
    std::vector<uint8_t> tx;
    size_t write_limit = static_cast<size_t>(-1);
};

class CompanionUsbSerialTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::current_millis = 0;
        transport.begin(stream);
        transport.enable();
    }

    ByteStream stream;
    sigurdos::comms::CompanionUsbSerialInterface transport;
    uint8_t output[MAX_FRAME_SIZE]{};
};

TEST_F(CompanionUsbSerialTest, RetainsPartialFrameUntilPayloadCompletes) {
    stream.queue({'<', 3, 0, 'a'});
    EXPECT_EQ(transport.checkRecvFrame(output), 0u);

    stream.queue({'b', 'c'});
    ASSERT_EQ(transport.checkRecvFrame(output), 3u);
    EXPECT_EQ(std::vector<uint8_t>(output, output + 3),
              (std::vector<uint8_t>{'a', 'b', 'c'}));
}

TEST_F(CompanionUsbSerialTest, AcceptsMaximumLengthFrame) {
    std::vector<uint8_t> frame{'<', static_cast<uint8_t>(MAX_FRAME_SIZE), 0};
    for (size_t i = 0; i < MAX_FRAME_SIZE; ++i) {
        frame.push_back(static_cast<uint8_t>(i));
    }
    stream.queue(frame);

    ASSERT_EQ(transport.checkRecvFrame(output), static_cast<size_t>(MAX_FRAME_SIZE));
    for (size_t i = 0; i < MAX_FRAME_SIZE; ++i) {
        EXPECT_EQ(output[i], static_cast<uint8_t>(i));
    }
}

TEST_F(CompanionUsbSerialTest, RejectsOversizedLengthAndFindsNextFrame) {
    const uint16_t oversized = MAX_FRAME_SIZE + 1;
    stream.queue({'<', static_cast<uint8_t>(oversized & 0xFFu),
                  static_cast<uint8_t>(oversized >> 8),
                  0xAA, 0xBB, '<', 2, 0, 'o', 'k'});

    ASSERT_EQ(transport.checkRecvFrame(output), 2u);
    EXPECT_EQ(output[0], 'o');
    EXPECT_EQ(output[1], 'k');
}

TEST_F(CompanionUsbSerialTest, StalledFrameExpiresAndNextMarkerResynchronizes) {
    stream.queue({'<', 4, 0, 'x'});
    EXPECT_EQ(transport.checkRecvFrame(output), 0u);

    arduino_mock::current_millis =
        sigurdos::comms::CompanionUsbSerialInterface::DEFAULT_INTER_BYTE_TIMEOUT_MS;
    stream.queue({'<', 2, 0, 'o', 'k'});

    ASSERT_EQ(transport.checkRecvFrame(output), 2u);
    EXPECT_EQ(output[0], 'o');
    EXPECT_EQ(output[1], 'k');
}

TEST_F(CompanionUsbSerialTest, PayloadMayContainSyncMarker) {
    stream.queue({'<', 3, 0, 'a', '<', 'b'});
    ASSERT_EQ(transport.checkRecvFrame(output), 3u);
    EXPECT_EQ(output[1], '<');
}

TEST_F(CompanionUsbSerialTest, ZeroLengthAndNoiseDoNotBlockLaterFrame) {
    stream.queue({0xAA, '<', 0, 0, 0xBB, '<', 1, 0, 'z'});
    ASSERT_EQ(transport.checkRecvFrame(output), 1u);
    EXPECT_EQ(output[0], 'z');
}

TEST_F(CompanionUsbSerialTest, TimeoutArithmeticSurvivesMillisWrap) {
    sigurdos::comms::CompanionUsbSerialInterface short_timeout(64);
    short_timeout.begin(stream);
    short_timeout.enable();
    arduino_mock::current_millis = 0xFFFFFFF0u;
    stream.queue({'<', 2, 0, 'a'});
    EXPECT_EQ(short_timeout.checkRecvFrame(output), 0u);

    arduino_mock::current_millis = 0x00000020u;  // 48 ms later
    stream.queue({'b'});
    ASSERT_EQ(short_timeout.checkRecvFrame(output), 2u);
    EXPECT_EQ(output[1], 'b');
}

TEST_F(CompanionUsbSerialTest, WritesFramedPayloadAndRejectsInvalidLengths) {
    const uint8_t payload[] = {0x10, 0x20, 0x30};
    ASSERT_EQ(transport.writeFrame(payload, sizeof(payload)), sizeof(payload));
    EXPECT_EQ(stream.tx,
              (std::vector<uint8_t>{'>', 3, 0, 0x10, 0x20, 0x30}));

    EXPECT_EQ(transport.writeFrame(nullptr, 1), 0u);
    EXPECT_EQ(transport.writeFrame(payload, 0), 0u);
    EXPECT_EQ(transport.writeFrame(payload, MAX_FRAME_SIZE + 1), 0u);
}

TEST_F(CompanionUsbSerialTest, ReportsPartialPayloadWriteForBridgeRetry) {
    stream.write_limit = 4;  // complete header plus one payload byte
    const uint8_t payload[] = {1, 2, 3};
    EXPECT_EQ(transport.writeFrame(payload, sizeof(payload)), 1u);
}

TEST_F(CompanionUsbSerialTest, DisabledTransportDoesNotConsumeOrWrite) {
    transport.disable();
    stream.queue({'<', 1, 0, 'x'});
    EXPECT_EQ(transport.checkRecvFrame(output), 0u);
    EXPECT_EQ(stream.rx.size(), 4u);
    const uint8_t payload = 1;
    EXPECT_EQ(transport.writeFrame(&payload, 1), 0u);
}

} // namespace
