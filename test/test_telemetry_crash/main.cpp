// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>

#include "diagnostics/telemetry_crash.h"

namespace {

using sigurdos::telemetry::crash::RTC_CRASH_BACKTRACE_CAPACITY;
using sigurdos::telemetry::crash::RTC_CRASH_ELF_SHA_CAPACITY;
using sigurdos::telemetry::crash::RTC_CRASH_MAGIC;
using sigurdos::telemetry::crash::RTC_CRASH_TASK_NAME_CAPACITY;
using sigurdos::telemetry::crash::RTC_CRASH_VERSION;
using sigurdos::telemetry::crash::RtcCrashRecord;
using sigurdos::telemetry::crash::bounded_backtrace_count;
using sigurdos::telemetry::crash::crash_record_crc32;
using sigurdos::telemetry::crash::crash_record_valid;

std::string read_project_file(const char* path)
{
    const char* prefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    for (const char* prefix : prefixes) {
        std::ifstream in(std::string(prefix) + path);
        if (in.good()) {
            return std::string(std::istreambuf_iterator<char>(in), {});
        }
    }
    return {};
}

RtcCrashRecord make_valid_record()
{
    RtcCrashRecord record{};
    record.magic = RTC_CRASH_MAGIC;
    record.version = RTC_CRASH_VERSION;
    record.length = sizeof(record);
    record.crash_pc = 0x42012345U;
    record.exception_task[RTC_CRASH_TASK_NAME_CAPACITY - 1] = '\0';
    record.app_elf_sha256[RTC_CRASH_ELF_SHA_CAPACITY - 1] = '\0';
    record.crc32 = crash_record_crc32(record);
    return record;
}

TEST(TelemetryCrashTest, BacktraceCapacityMatchesRecordStorage)
{
    RtcCrashRecord record{};

    EXPECT_EQ(sizeof(record.backtrace_pcs) / sizeof(record.backtrace_pcs[0]),
              RTC_CRASH_BACKTRACE_CAPACITY);
    EXPECT_EQ(sizeof(record.backtrace_pcs), 32u);
    record.backtrace_pcs[0] = 0x42012345U;
    EXPECT_EQ(record.backtrace_pcs[0], 0x42012345U);
    EXPECT_EQ(sizeof(record.exception_a), 64u);
    EXPECT_EQ(sizeof(record.exception_task), RTC_CRASH_TASK_NAME_CAPACITY);
    EXPECT_EQ(sizeof(record.app_elf_sha256), RTC_CRASH_ELF_SHA_CAPACITY);
}

TEST(TelemetryCrashTest, RecordValidationCoversVersionLengthBoundsAndCrc)
{
    const RtcCrashRecord valid = make_valid_record();
    EXPECT_TRUE(crash_record_valid(valid));

    RtcCrashRecord corrupt = valid;
    corrupt.crash_pc ^= 1U;
    EXPECT_FALSE(crash_record_valid(corrupt));

    corrupt = valid;
    corrupt.version++;
    corrupt.crc32 = crash_record_crc32(corrupt);
    EXPECT_FALSE(crash_record_valid(corrupt));

    corrupt = valid;
    corrupt.length--;
    corrupt.crc32 = crash_record_crc32(corrupt);
    EXPECT_FALSE(crash_record_valid(corrupt));

    corrupt = valid;
    corrupt.backtrace_count = RTC_CRASH_BACKTRACE_CAPACITY + 1;
    corrupt.crc32 = crash_record_crc32(corrupt);
    EXPECT_FALSE(crash_record_valid(corrupt));
}

TEST(TelemetryCrashTest, UsesCoreDumpContextAndEspResetEnums)
{
    const std::string source =
        read_project_file("src/diagnostics/telemetry_crash.cpp");
    ASSERT_FALSE(source.empty());

    EXPECT_NE(source.find("esp_core_dump_get_summary(&summary)"),
              std::string::npos);
    EXPECT_EQ(source.find("esp_register_shutdown_handler"), std::string::npos);
    EXPECT_EQ(source.find("capture_backtrace"), std::string::npos);
    EXPECT_NE(source.find("summary.ex_info.exc_a"), std::string::npos);
    EXPECT_NE(source.find(
                  "case ESP_RST_INT_WDT:   return \"interrupt watchdog\";"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "case ESP_RST_TASK_WDT:  return \"task watchdog\";"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "case ESP_RST_DEEPSLEEP: return \"deep sleep wake\";"),
              std::string::npos);
    EXPECT_NE(source.find(
                  "case ESP_RST_BROWNOUT:  return \"brownout\";"),
              std::string::npos);
}

TEST(TelemetryCrashTest, BoundedBacktraceCountPreservesValidCounts)
{
    EXPECT_EQ(bounded_backtrace_count(0), 0);
    EXPECT_EQ(bounded_backtrace_count(1), 1);
    EXPECT_EQ(bounded_backtrace_count(RTC_CRASH_BACKTRACE_CAPACITY),
              RTC_CRASH_BACKTRACE_CAPACITY);
}

TEST(TelemetryCrashTest, BoundedBacktraceCountClampsOverflowCounts)
{
    EXPECT_EQ(bounded_backtrace_count(RTC_CRASH_BACKTRACE_CAPACITY + 1),
              RTC_CRASH_BACKTRACE_CAPACITY);
    EXPECT_EQ(bounded_backtrace_count(UINT8_MAX), RTC_CRASH_BACKTRACE_CAPACITY);
}

}  // anonymous namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
