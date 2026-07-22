#include <gtest/gtest.h>

#include "hal/tdeck_pins.h"
#include "hal/tdeck_sleep_orchestrator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using sigurdos::TDeckSleepStatus;

struct FakeSleepOperations {
    std::vector<std::string> calls;
    uint64_t timer_us = 0;
    int timer_error = 0;
    int hold_error = 0;

    int enableTimerWake(uint64_t wake) {
        calls.push_back("timer");
        timer_us = wake;
        return timer_error;
    }
    void quiescePeripheralRail() { calls.push_back("quiesce"); }
    void configureOptionalRadioWake() { calls.push_back("radio-wake-check"); }
    void removePeripheralPower() { calls.push_back("rail-low"); }
    void settlePeripheralRail() { calls.push_back("settle"); }
    int holdPeripheralRail() {
        calls.push_back("rail-hold");
        return hold_error;
    }
    void enableDeepSleepHold() { calls.push_back("deep-hold"); }
    void enterDeepSleep() { calls.push_back("deep-sleep"); }
};

TEST(TDeckSleepOrchestratorTest, ExecutesProductionSequenceInOrder) {
    FakeSleepOperations operations;

    EXPECT_EQ(sigurdos::tdeck_orchestrate_sleep(operations, false, 30),
              TDeckSleepStatus::DeepSleepReturned);
    EXPECT_EQ(operations.timer_us, 30000000ULL);
    EXPECT_EQ(operations.calls,
              (std::vector<std::string>{
                  "timer", "quiesce", "radio-wake-check", "rail-low",
                  "settle", "rail-hold", "deep-hold", "deep-sleep"}));
}

TEST(TDeckSleepOrchestratorTest, ZeroSecondsUsesRecoveryTimer) {
    FakeSleepOperations operations;

    sigurdos::tdeck_orchestrate_sleep(operations, false, 0);

    EXPECT_EQ(operations.timer_us, 900000000ULL);
}

TEST(TDeckSleepOrchestratorTest, InhibitionHasNoHardwareSideEffects) {
    FakeSleepOperations operations;

    EXPECT_EQ(sigurdos::tdeck_orchestrate_sleep(operations, true, 60),
              TDeckSleepStatus::Inhibited);
    EXPECT_TRUE(operations.calls.empty());
}

TEST(TDeckSleepOrchestratorTest, TimerFailureLeavesPeripheralsPowered) {
    FakeSleepOperations operations;
    operations.timer_error = -1;

    EXPECT_EQ(sigurdos::tdeck_orchestrate_sleep(operations, false, 60),
              TDeckSleepStatus::WakeConfigurationFailed);
    EXPECT_EQ(operations.calls, (std::vector<std::string>{"timer"}));
}

TEST(TDeckSleepOrchestratorTest, RailHoldFailureNeverEntersDeepSleep) {
    FakeSleepOperations operations;
    operations.hold_error = 0x102;

    EXPECT_EQ(sigurdos::tdeck_orchestrate_sleep(operations, false, 60),
              TDeckSleepStatus::PeripheralRailHoldFailed);
    EXPECT_EQ(operations.calls,
              (std::vector<std::string>{
                  "timer", "quiesce", "radio-wake-check", "rail-low",
                  "settle", "rail-hold"}));
}

TEST(TDeckSleepOrchestratorTest, TDeckDio1CannotBeAnRtcWakeSource) {
    constexpr int ESP32_S3_LAST_RTC_GPIO = 21;

    EXPECT_EQ(PIN_LORA_DIO1, 45);
    EXPECT_GT(PIN_LORA_DIO1, ESP32_S3_LAST_RTC_GPIO);
    EXPECT_NE(SIGURDOS_LORA_DIO1_WAKE_MASK, 0ULL);
}

#ifdef SIGURDOS_TDECK
TEST(TDeckSleepOrchestratorTest, DedicatedTargetCompilesAsTDeck) {
    SUCCEED();
}
#endif

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
