// Host-native coverage for the Phase 3 screen-sleep state machine. The
// production source is included here because native_test intentionally builds
// a selective source set and platformio.ini is shared with all environments.
#include <gtest/gtest.h>

#include "../../src/power/screen_sleep.h"
#include "../../src/power/screen_sleep.cpp"

#include <array>
#include <cstdint>
#include <vector>

namespace {

using sigurdos::power::Hooks;
using sigurdos::power::NO_WAKE_DEADLINE_MS;
using sigurdos::power::PredicateState;
using sigurdos::power::WakeEvent;
using sigurdos::power::WakeReason;

struct FakeRuntime {
    bool screen_off = true;
    bool no_client = true;
    bool wifi_off = true;
    bool ble_off = true;
    bool on_battery = true;
    bool mesh_idle = true;
    uint32_t next_wake_in_ms = NO_WAKE_DEADLINE_MS;
    uint32_t epoch = 1234;
    WakeReason polled_reason = WakeReason::None;
    int display_wake_count = 0;
    int enter_count = 0;
    int leave_count = 0;
    std::vector<uint32_t> delays;
    std::vector<WakeEvent> wakes;
};

FakeRuntime* g_runtime = nullptr;

bool fake_screen_off() { return g_runtime->screen_off; }
bool fake_no_client() { return g_runtime->no_client; }
bool fake_wifi_off() { return g_runtime->wifi_off; }
bool fake_ble_off() { return g_runtime->ble_off; }
bool fake_on_battery() { return g_runtime->on_battery; }
bool fake_mesh_idle() { return g_runtime->mesh_idle; }
uint32_t fake_next_wake(uint32_t) { return g_runtime->next_wake_in_ms; }
uint32_t fake_epoch() { return g_runtime->epoch; }
WakeReason fake_poll_wake_reason() {
    const WakeReason result = g_runtime->polled_reason;
    g_runtime->polled_reason = WakeReason::None;
    return result;
}
void fake_wake_display() { ++g_runtime->display_wake_count; }
void fake_delay(uint32_t delay_ms) { g_runtime->delays.push_back(delay_ms); }
void fake_transition(uint32_t, bool entering) {
    if (entering) ++g_runtime->enter_count;
    else ++g_runtime->leave_count;
}
void fake_wake_callback(const WakeEvent& event) {
    g_runtime->wakes.push_back(event);
}

void begin_fake(FakeRuntime& runtime)
{
    g_runtime = &runtime;
    const Hooks hooks{
        fake_screen_off,
        fake_no_client,
        fake_wifi_off,
        fake_ble_off,
        fake_on_battery,
        fake_mesh_idle,
        fake_next_wake,
        fake_epoch,
        fake_poll_wake_reason,
        fake_wake_display,
        fake_delay,
    };
    sigurdos::power::begin(hooks);
    sigurdos::power::onTransition(fake_transition);
    sigurdos::power::onWake(fake_wake_callback);
}

TEST(ScreenSleepPolicyTest, PredicateMatrixRequiresEveryPowerGate)
{
    PredicateState all{};
    all.screen_off = true;
    all.no_companion_clients = true;
    all.wifi_off = true;
    all.ble_off = true;
    all.on_battery = true;
    all.mesh_idle = true;
    EXPECT_TRUE(sigurdos::power::idle_predicates_allow(all));

    PredicateState candidate = all;
    candidate.screen_off = false;
    EXPECT_FALSE(sigurdos::power::idle_predicates_allow(candidate));
    candidate = all;
    candidate.no_companion_clients = false;
    EXPECT_FALSE(sigurdos::power::idle_predicates_allow(candidate));
    candidate = all;
    candidate.wifi_off = false;
    EXPECT_FALSE(sigurdos::power::idle_predicates_allow(candidate));
    candidate = all;
    candidate.ble_off = false;
    EXPECT_FALSE(sigurdos::power::idle_predicates_allow(candidate));
    candidate = all;
    candidate.on_battery = false;
    EXPECT_FALSE(sigurdos::power::idle_predicates_allow(candidate));
    candidate = all;
    candidate.mesh_idle = false;
    EXPECT_FALSE(sigurdos::power::idle_predicates_allow(candidate));
}

TEST(ScreenSleepPolicyTest, ThrottleDelayIsBoundedByQuantumAndDeadline)
{
    EXPECT_EQ(sigurdos::power::throttle_delay_ms(NO_WAKE_DEADLINE_MS), 50u);
    EXPECT_EQ(sigurdos::power::throttle_delay_ms(100u), 50u);
    EXPECT_EQ(sigurdos::power::throttle_delay_ms(10u), 10u);
    EXPECT_EQ(sigurdos::power::throttle_delay_ms(0u), 0u);
    EXPECT_EQ(sigurdos::power::throttle_delay_ms(4u, 0u), 0u);
    EXPECT_EQ(sigurdos::power::throttle_delay_ms(4u, 3u), 3u);
}

TEST(ScreenSleepPolicyTest, EntersAndLeavesOnPacketTouchAndButtonWake)
{
    const std::array<WakeReason, 3> reasons = {
        WakeReason::Packet, WakeReason::Touch, WakeReason::Button};

    for (const WakeReason reason : reasons) {
        FakeRuntime runtime;
        begin_fake(runtime);

        // The first pass primes the edge detector and enters the throttle.
        sigurdos::power::loopEnd(100u);
        ASSERT_TRUE(sigurdos::power::throttling());
        ASSERT_EQ(runtime.enter_count, 1);
        ASSERT_EQ(runtime.delays.size(), 1u);
        EXPECT_EQ(runtime.delays.back(), 50u);

        runtime.polled_reason = reason;
        sigurdos::power::loopEnd(150u);

        EXPECT_FALSE(sigurdos::power::throttling());
        EXPECT_EQ(runtime.leave_count, 1);
        EXPECT_EQ(runtime.display_wake_count, 1);
        ASSERT_EQ(runtime.wakes.size(), 1u);
        EXPECT_EQ(runtime.wakes[0].reason, reason);
        EXPECT_EQ(runtime.wakes[0].now_ms, 150u);
        EXPECT_EQ(runtime.wakes[0].epoch, 1234u);
        EXPECT_EQ(sigurdos::power::lastWakeReason(), reason);
    }
}

TEST(ScreenSleepPolicyTest, TimerDeadlineIsARecordedWake)
{
    FakeRuntime runtime;
    begin_fake(runtime);

    sigurdos::power::loopEnd(10u);
    ASSERT_TRUE(sigurdos::power::throttling());
    runtime.next_wake_in_ms = 0;
    sigurdos::power::loopEnd(20u);

    ASSERT_EQ(runtime.wakes.size(), 1u);
    EXPECT_EQ(runtime.wakes[0].reason, WakeReason::Timer);
    EXPECT_EQ(sigurdos::power::wake_reason_name(WakeReason::Timer), "timer");
    EXPECT_TRUE(runtime.delays.size() == 1u);
}

TEST(ScreenSleepPolicyTest, PredicateFailureStopsThrottleWithoutFalseWake)
{
    FakeRuntime runtime;
    begin_fake(runtime);
    sigurdos::power::loopEnd(0u);
    ASSERT_TRUE(sigurdos::power::throttling());

    runtime.wifi_off = false;
    sigurdos::power::loopEnd(50u);

    EXPECT_FALSE(sigurdos::power::throttling());
    EXPECT_EQ(runtime.leave_count, 1);
    EXPECT_TRUE(runtime.wakes.empty());
    EXPECT_EQ(sigurdos::power::wakeCount(), 0u);
}

TEST(ScreenSleepPolicyTest, WakeLogRetainsNewestEightEvents)
{
    FakeRuntime runtime;
    begin_fake(runtime);

    for (uint32_t i = 0; i < 10; ++i) {
        sigurdos::power::loopEnd(i * 10u);
        ASSERT_TRUE(sigurdos::power::throttling());
        runtime.polled_reason = (i % 2u == 0u)
            ? WakeReason::Touch : WakeReason::Packet;
        sigurdos::power::loopEnd(i * 10u + 1u);
        ASSERT_FALSE(sigurdos::power::throttling());
    }

    ASSERT_EQ(sigurdos::power::wakeLogSize(), 8u);
    WakeEvent first{};
    WakeEvent last{};
    ASSERT_TRUE(sigurdos::power::wakeLogAt(0, &first));
    ASSERT_TRUE(sigurdos::power::wakeLogAt(7, &last));
    EXPECT_EQ(first.now_ms, 21u);
    EXPECT_EQ(last.now_ms, 91u);
    EXPECT_FALSE(sigurdos::power::wakeLogAt(8, &last));
    EXPECT_EQ(sigurdos::power::wakeCount(), 10u);
}

TEST(ScreenSleepPolicyTest, DisabledPolicyLeavesThrottleImmediately)
{
    FakeRuntime runtime;
    begin_fake(runtime);
    sigurdos::power::loopEnd(0u);
    ASSERT_TRUE(sigurdos::power::throttling());

    sigurdos::power::setEnabled(false);
    EXPECT_FALSE(sigurdos::power::enabled());
    EXPECT_FALSE(sigurdos::power::throttling());
    EXPECT_EQ(runtime.leave_count, 1);
}

} // namespace
