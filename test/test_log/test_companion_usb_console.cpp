// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#define SIGURDOS_COMPANION_USB 1
#define SIGURDOS_DEBUG 1
#include "diagnostics/companion_usb_console.h"
#include "diagnostics/log.h"

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
