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
#include "diagnostics/telemetry_policy.h"

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

    clear_output();
    emit_tag(tag::SD);
    emit_sep();
    emit_kv_u64(key::SD_FREE, 8589934593ULL);
    emit_end();
    expect_output("@sd|free=8589934593\n");
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

TEST_F(TelemetryProtocolTest, EscapesFramingAndControlBytesInStrings) {
    emit_record1_s(tag::PKT, key::TEXT, "a|b=c%*\r\n");
    expect_output("@pkt|text=a%7Cb%3Dc%25%2A%0D%0A\n");

    clear_output();
    emit_record1_s(tag::PKT, key::TEXT, "caf\xC3\xA9");
    expect_output("@pkt|text=caf\xC3\xA9\n");
}

TEST_F(TelemetryProtocolTest, EmitsCommandResponseRecords) {
    emit_ok("hb", 1234);
    expect_output("@ok|cmd=hb|cost_us=1234\n");

    clear_output();
    emit_err("telemetry", "unknown");
    expect_output("@err|cmd=telemetry|desc=unknown\n");

    clear_output();
    emit_end_resp("contacts", 5, 4321);
    expect_output("@end|cmd=contacts|n=5|cost_us=4321\n");

    clear_output();
    emit_err("bad|query", "unknown_query");
    expect_output("@err|cmd=bad%7Cquery|desc=unknown_query\n");
}

TEST_F(TelemetryProtocolTest, EmitsManualMultiFieldRecord) {
    emit_tag(tag::MESH);
    emit_sep();
    emit_kv(key::RSSI, -88);
    emit_sep();
    emit_kv(key::SNR, -7);
    emit_sep();
    emit_kv(key::NOISE, -121);
    emit_end();

    expect_output("@mesh|rssi=-88|snr=-7|noise=-121\n");
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
    emit_sep();
    emit_kv_s(key::BUILD_SOURCE, "github_actions");
    emit_sep();
    emit_kv_s(key::RUN_ID, "1234");
    emit_sep();
    emit_kv_s(key::RUN_ATTEMPT, "2");
    emit_sep();
    emit_kv_s(key::REF, "feature|diagnostics");
    emit_sep();
    emit_kv_s(key::RUN_URL, "https://example.test/run=1234");
    emit_end();

    expect_output("@build|fw=beta-0.1.39|git=abc123def456|dirty=1|mcore=9a888541efaf|env=SigurdOS_TDeck_telemetry|part=default_16MB.csv|board=t-deck|mcu=esp32s3|source=github_actions|run_id=1234|attempt=2|ref=feature%7Cdiagnostics|run_url=https://example.test/run%3D1234\n");
}

TEST_F(TelemetryProtocolTest, EveryDeclaredScreenHasATelemetryName) {
    for (int value = 0;
         value < static_cast<int>(sigurdos::ui::Screen::COUNT); ++value) {
        EXPECT_STRNE(screen_name(static_cast<sigurdos::ui::Screen>(value)), "?");
    }
    EXPECT_STREQ(screen_name(sigurdos::ui::Screen::Bluetooth), "Bluetooth");
}

TEST_F(TelemetryProtocolTest, WidgetTraversalCountsDeepTreesAndReportsBudget) {
    struct Node {
        Node* children[65] = {};
        uint32_t count = 0;
    };
    Node root;
    Node child;
    Node grandchild;
    Node great_grandchild;
    root.children[0] = &child;
    root.count = 1;
    child.children[0] = &grandchild;
    child.count = 1;
    grandchild.children[0] = &great_grandchild;
    grandchild.count = 1;
    auto child_count = [](Node* node) { return node->count; };
    auto child_at = [](Node* node, uint32_t index) { return node->children[index]; };

    const WidgetTreeCount deep = count_widget_tree(&root, child_count, child_at);
    EXPECT_EQ(deep.count, 4);
    EXPECT_FALSE(deep.truncated);

    Node wide_root;
    Node leaves[65];
    wide_root.count = 65;
    for (uint32_t i = 0; i < 65; ++i) wide_root.children[i] = &leaves[i];
    const WidgetTreeCount wide =
        count_widget_tree(&wide_root, child_count, child_at);
    EXPECT_TRUE(wide.truncated);
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
