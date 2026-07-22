// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#define SIGURDOS_COMPANION_USB 1
#define SIGURDOS_DEBUG 1
#include "diagnostics/companion_usb_console.h"
#include "diagnostics/log.h"
#include <helpers/ArduinoSerialInterface.h>

TEST(CompanionUsbConsoleTest, BinaryStreamIsolatedFromProjectLogs) {
    auto& data_serial = sigurdos::diagnostics::companionUsbDataSerial();
    data_serial.mock_reset();

    Serial.begin(115200);
    EXPECT_TRUE(data_serial.mock_was_begun());

    const uint8_t binary[] = {'>', 1, 0, 0xA5};
    for (uint8_t byte : binary) {
        EXPECT_EQ(sigurdos::diagnostics::companionUsbDataStream().write(byte), 1u);
    }

    SIG_LOGE("must not reach USB %d", 1);
    SIG_LOGW("must not reach USB");
    SIG_LOGD("must not reach USB");
    Serial.printf("raw project log must not reach USB\n");
    Serial.println("nor should println");

    ASSERT_EQ(data_serial.mock_tx_output().size(), sizeof(binary));
    EXPECT_EQ(data_serial.mock_tx_output(),
              std::string(reinterpret_cast<const char*>(binary), sizeof(binary)));
}

TEST(CompanionUsbConsoleTest, FramedTransportUsesRealDataStreamInBothDirections) {
    auto& data_serial = sigurdos::diagnostics::companionUsbDataSerial();
    Stream& data_stream = sigurdos::diagnostics::companionUsbDataStream();
    Stream& console_stream = Serial;
    EXPECT_NE(&data_stream, &console_stream);

    data_serial.mock_reset();
    ArduinoSerialInterface transport;
    transport.begin(data_stream);
    transport.enable();

    const uint8_t inbound[] = {'<', 2, 0, 0xA5, 0x5A};
    data_serial.mock_queue_rx_bytes(inbound, sizeof(inbound));
    uint8_t received[MAX_FRAME_SIZE]{};
    ASSERT_EQ(transport.checkRecvFrame(received), 2u);
    EXPECT_EQ(received[0], 0xA5);
    EXPECT_EQ(received[1], 0x5A);

    SIG_LOGW("discarded console line");
    const uint8_t outbound[] = {0x10, 0x20};
    ASSERT_EQ(transport.writeFrame(outbound, sizeof(outbound)), sizeof(outbound));
    const std::string expected{"\x3E\x02\x00\x10\x20", 5};
    EXPECT_EQ(data_serial.mock_tx_output(), expected);
}
