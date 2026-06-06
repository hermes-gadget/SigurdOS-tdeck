// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.


#include <gtest/gtest.h>

#include "Arduino.h"

// Exercise the production telemetry implementation in this focused native test
// without changing the global native build source filter.
#include "diagnostics/telemetry_protocol.cpp"

namespace {

using namespace sigurdos::telemetry;

class TelemetryProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        arduino_mock::reset();
        Serial.mock_clear_rx();
        Serial.mock_clear_tx();
    }

    void expect_output(const char* expected) {
        EXPECT_EQ(Serial.mock_tx_output(), expected);
    }

    void clear_output() {
        Serial.mock_clear_tx();
    }
};

TEST_F(TelemetryProtocolTest, EmitsSignedAndUnsignedSingleFieldRecords) {
    emit_record1(tag::HEAP, key::H, -42);
    expect_output("@heap|h=-42\n");

    clear_output();
    emit_record1_u(tag::RADIO, key::RADIO_AIRTXTOTAL, 4294967295UL);
    expect_output("@radio|air_tx=4294967295\n");
}

TEST_F(TelemetryProtocolTest, EmitsStringRecordsAndFallbackForMissingValues) {
    emit_record1_s(tag::PKT, key::TEXT, "hello");
    expect_output("@pkt|text=hello\n");

    clear_output();
    emit_record1_s(tag::PKT, key::SRC, "");
    expect_output("@pkt|src=*\n");

    clear_output();
    emit_record1_s(tag::PKT, key::SRC, nullptr);
    expect_output("@pkt|src=*\n");
}

TEST_F(TelemetryProtocolTest, EmitsCommandResponseRecords) {
    emit_ok("hb", 1234);
    expect_output("@ok|cmd=hb|cost_us=1234\n");

    clear_output();
    emit_err("telemetry", "unknown");
    expect_output("@err|cmd=telemetry|desc=unknown\n");

    clear_output();
    emit_end_resp("contacts", 5);
    expect_output("@end|cmd=contacts|n=5\n");
}

TEST_F(TelemetryProtocolTest, EmitsManualMultiFieldRecord) {
    emit_tag(tag::MESH);
    emit_sep();
    emit_kv(key::RSSI, -88);
    emit_sep();
    emit_kv(key::SNR, 9);
    emit_end();

    expect_output("@mesh|rssi=-88|snr=9\n");
}

TEST_F(TelemetryProtocolTest, EmitsBuildIdentityRecordFields) {
    emit_tag(tag::BUILD);
    emit_sep();
    emit_kv_s(key::FW, "beta-0.1.39");
    emit_sep();
    emit_kv_s(key::GIT, "abc123def456");
    emit_sep();
    emit_kv_u(key::DIRTY, 1);
    emit_sep();
    emit_kv_s(key::MESHCORE, "9a888541efaf");
    emit_sep();
    emit_kv_s(key::ENV, "SigurdOS_TDeck_telemetry");
    emit_sep();
    emit_kv_s(key::PART, "default_16MB.csv");
    emit_sep();
    emit_kv_s(key::BOARD, "t-deck");
    emit_sep();
    emit_kv_s(key::MCU, "esp32s3");
    emit_end();

    expect_output("@build|fw=beta-0.1.39|git=abc123def456|dirty=1|mcore=9a888541efaf|env=SigurdOS_TDeck_telemetry|part=default_16MB.csv|board=t-deck|mcu=esp32s3\n");
}

TEST_F(TelemetryProtocolTest, EmitsFloatRecordsWithPrecision) {
    emit_tag(tag::TEMP);
    emit_sep();
    emit_kv_f(key::TEMP_VAL, 3.0f, 2);
    emit_sep();
    emit_kv_f(key::GPS_ALT, 12.5f, 1);
    emit_end();

    expect_output("@temp|val=3.00|alt=12.5\n");
}

TEST_F(TelemetryProtocolTest, PreservesNegativeSignForFractionalFloats) {
    emit_tag(tag::GPS);
    emit_sep();
    emit_kv_f(key::GPS_LAT, -0.5f, 1);
    emit_sep();
    emit_kv_f(key::GPS_LON, -12.25f, 2);
    emit_end();

    expect_output("@gps|lat=-0.5|lon=-12.25\n");
}

}  // namespace
