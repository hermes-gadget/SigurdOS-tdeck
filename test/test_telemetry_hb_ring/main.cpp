// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>
#include <cstdint>
#include "Arduino.h"

#define SIGURDOS_TELEMETRY 1

bool psramFound()
{
    return false;
}

#include "diagnostics/telemetry_hb_ring.cpp"

namespace sigurdos {
namespace telemetry {

static uint32_t s_emit_records = 0;

namespace tag {
const char HB_RING[] = "hb-ring";
}

namespace key {
const char N[] = "n";
const char I[] = "i";
const char T[] = "t";
const char H[] = "h";
const char B[] = "b";
}

void emit_tag(const char*) {}
void emit_kv(const char*, int32_t) {}
void emit_kv_u(const char*, uint32_t) {}
void emit_kv_s(const char*, const char*) {}
void emit_kv_f(const char*, float, int) {}
void emit_sep() {}
void emit_end() { s_emit_records++; }

void hb_ring_test_reset_emit_count()
{
    s_emit_records = 0;
}

uint32_t hb_ring_test_emit_count()
{
    return s_emit_records;
}

}  // namespace telemetry
}  // namespace sigurdos

namespace {

using sigurdos::telemetry::HB_RING_SIZE;
using sigurdos::telemetry::HbRingEntry;
using sigurdos::telemetry::hb_ring_clear;
using sigurdos::telemetry::hb_ring_count;
using sigurdos::telemetry::hb_ring_get;
using sigurdos::telemetry::hb_ring_init;
using sigurdos::telemetry::hb_ring_push;
using sigurdos::telemetry::hb_ring_query;
using sigurdos::telemetry::hb_ring_test_emit_count;
using sigurdos::telemetry::hb_ring_test_reset_emit_count;

HbRingEntry make_entry(uint32_t value)
{
    HbRingEntry e{};
    e.t_s = value;
    e.h = 1000 + value;
    e.hm = 900 + value;
    e.p = 800 + value;
    e.b = (uint8_t)(value % 100);
    e.rssi = (int16_t)(-200 + (int32_t)value);
    e.wt = (uint16_t)(value % 50);
    return e;
}

class HbRingTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        arduino_mock::reset();
        ASSERT_TRUE(hb_ring_init());
        hb_ring_clear();
        hb_ring_test_reset_emit_count();
    }
};

TEST_F(HbRingTest, StartsEmptyAfterInit)
{
    HbRingEntry out{};
    EXPECT_EQ(hb_ring_count(), 0u);
    EXPECT_FALSE(hb_ring_get(0, &out));
}

TEST_F(HbRingTest, RepeatedInitReportsExistingAllocation)
{
    EXPECT_TRUE(hb_ring_init());
}

TEST_F(HbRingTest, ReadsUnwrappedEntriesByLogicalIndex)
{
    hb_ring_push(make_entry(10));
    hb_ring_push(make_entry(11));
    hb_ring_push(make_entry(12));

    HbRingEntry out{};
    ASSERT_TRUE(hb_ring_get(0, &out));
    EXPECT_EQ(out.t_s, 10u);
    ASSERT_TRUE(hb_ring_get(2, &out));
    EXPECT_EQ(out.t_s, 12u);
}

TEST_F(HbRingTest, RetainsFullAndDiffHeartbeatFlags)
{
    HbRingEntry full = make_entry(1);
    HbRingEntry diff = make_entry(2);
    diff.flags = 1;
    hb_ring_push(full);
    hb_ring_push(diff);

    HbRingEntry out{};
    ASSERT_TRUE(hb_ring_get(0, &out));
    EXPECT_EQ(out.flags, 0u);
    ASSERT_TRUE(hb_ring_get(1, &out));
    EXPECT_EQ(out.flags, 1u);
}

TEST_F(HbRingTest, RejectsNullOutputPointer)
{
    hb_ring_push(make_entry(1));
    EXPECT_FALSE(hb_ring_get(0, nullptr));
}

TEST_F(HbRingTest, WrappedRingRetainsOldestWindow)
{
    constexpr uint32_t extra = 5;
    const uint32_t total = HB_RING_SIZE + extra;
    for (uint32_t i = 0; i < total; i++) {
        hb_ring_push(make_entry(i));
    }

    HbRingEntry out{};
    EXPECT_EQ(hb_ring_count(), total);
    EXPECT_FALSE(hb_ring_get(extra - 1, &out));

    ASSERT_TRUE(hb_ring_get(extra, &out));
    EXPECT_EQ(out.t_s, extra);

    ASSERT_TRUE(hb_ring_get(total - 1, &out));
    EXPECT_EQ(out.t_s, total - 1);
}

TEST_F(HbRingTest, QueryLargerThanCapacityEmitsRetainedWindow)
{
    constexpr uint32_t extra = 7;
    for (uint32_t i = 0; i < HB_RING_SIZE + extra; i++) {
        hb_ring_push(make_entry(i));
    }

    const uint32_t emitted = hb_ring_query(HB_RING_SIZE + 100);
    EXPECT_EQ(emitted, HB_RING_SIZE + 1);
    EXPECT_EQ(hb_ring_test_emit_count(), HB_RING_SIZE + 1);
}

}  // anonymous namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
