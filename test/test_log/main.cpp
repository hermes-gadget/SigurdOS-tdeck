// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
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

#include <string>
#include <thread>

#include "Arduino.h"

// Defining the master switch numerically as zero must behave exactly like an
// undefined release-build switch.
#define SIGURDOS_DEBUG 0
#include "diagnostics/log.h"
#include "diagnostics/diagnostic_io.h"

TEST(LogMacrosTest, ErrorAndWarningAddLevelPrefixAndNewline) {
    Serial.mock_reset();

    SIG_LOGE("failure %d", 7);
    SIG_LOGW("warning");

    EXPECT_EQ("[E] failure 7\n[W] warning\n", Serial.mock_tx_output());
}

TEST(LogMacrosTest, DebugLogsCompileOutUnlessDebugBuild) {
    Serial.mock_reset();

    SIG_LOGD("debug %s", "message");

#if SIGURDOS_DEBUG_ACTIVE
    EXPECT_EQ("[D] debug message\n", Serial.mock_tx_output());
#else
    EXPECT_TRUE(Serial.mock_tx_output().empty());
#endif
}

TEST(LogMacrosTest, NumericZeroDisablesDebugLogging) {
    EXPECT_EQ(SIGURDOS_DEBUG_ACTIVE, 0);
}

TEST(DiagnosticWriterTest, ConcurrentProducersAndDrainerPreserveRecords) {
    auto& diagnostic_writer = sigurdos::diagnostics::writer();
    diagnostic_writer.reset();
    Serial.mock_reset();

    constexpr int producer_count = 4;
    constexpr int records_per_producer = 32;
    std::thread producers[producer_count];
    for (int producer = 0; producer < producer_count; ++producer) {
        producers[producer] = std::thread([producer, &diagnostic_writer]() {
            for (int record = 0; record < records_per_producer; ++record) {
                const std::string line = "producer-" + std::to_string(producer) + "-" +
                                         std::to_string(record);
                diagnostic_writer.println(line.c_str());
            }
        });
    }

    std::thread drainer([&diagnostic_writer]() {
        for (int pass = 0; pass < 256; ++pass) diagnostic_writer.drain();
    });

    for (auto& producer : producers) producer.join();
    drainer.join();
    diagnostic_writer.flush();

    const std::string output = Serial.mock_tx_output();
    std::size_t line_count = 0;
    std::size_t line_start = 0;
    while (line_start < output.size()) {
        const std::size_t line_end = output.find('\n', line_start);
        ASSERT_NE(line_end, std::string::npos);
        const std::string line = output.substr(line_start, line_end - line_start);
        EXPECT_EQ(line.rfind("producer-", 0), 0U);
        ++line_count;
        line_start = line_end + 1;
    }
    EXPECT_EQ(line_count, static_cast<std::size_t>(producer_count * records_per_producer));
    EXPECT_EQ(diagnostic_writer.dropped_records(), 0U);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
