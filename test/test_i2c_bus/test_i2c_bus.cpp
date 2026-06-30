// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <gtest/gtest.h>

#include "Arduino.h"
#include "hal/i2c_bus.h"
#include "hal/tdeck_pins.h"

namespace {

class I2cBusTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    arduino_mock::reset();
    Wire = TwoWire();
    sigurdos::i2c::reset_for_test();
  }
};

TEST_F(I2cBusTest, ProbeWhitelistContainsOnlyTDeckDevices)
{
  using namespace sigurdos::i2c;
  EXPECT_TRUE(is_target_address(KEYBOARD_ADDR));
  EXPECT_TRUE(is_target_address(TOUCH_ADDR_PRIMARY));
  EXPECT_TRUE(is_target_address(TOUCH_ADDR_ALTERNATE));

  for (uint16_t addr = 0; addr <= 0x7F; ++addr) {
    const bool expected = addr == KEYBOARD_ADDR ||
                          addr == TOUCH_ADDR_PRIMARY ||
                          addr == TOUCH_ADDR_ALTERNATE;
    EXPECT_EQ(is_target_address((uint8_t)addr), expected) << addr;
  }
}

TEST_F(I2cBusTest, UnknownAddressIsRejectedWithoutBusTraffic)
{
  EXPECT_FALSE(sigurdos::i2c::probe_target(0x42));
  EXPECT_EQ(Wire.mock_end_count(), 0u);
}

TEST_F(I2cBusTest, TargetProbeUsesOnlyRequestedAddress)
{
  EXPECT_TRUE(sigurdos::i2c::probe_target(sigurdos::i2c::KEYBOARD_ADDR));
  ASSERT_EQ(Wire.mock_address_count(), 1u);
  EXPECT_EQ(Wire.mock_address_at(0), sigurdos::i2c::KEYBOARD_ADDR);
}

TEST_F(I2cBusTest, RuntimeConfigurationBoundsClockAndTimeout)
{
  sigurdos::i2c::configure_runtime();
  EXPECT_EQ(Wire.mock_clock(), sigurdos::i2c::BUS_CLOCK_HZ);
  EXPECT_EQ(Wire.mock_timeout_ms(), sigurdos::i2c::TRANSACTION_TIMEOUT_MS);
}

TEST_F(I2cBusTest, RecoveryDoesNothingWhenBusIsAlreadyFree)
{
  EXPECT_EQ(sigurdos::i2c::recover_before_begin(),
            sigurdos::i2c::RecoveryResult::AlreadyFree);
  EXPECT_EQ(arduino_mock::pin_mode_calls[PIN_TOUCH_SCL], 1);
}

TEST_F(I2cBusTest, RecoveryReleasesSdaWithBoundedClocking)
{
  arduino_mock::forced_read_value[PIN_TOUCH_SDA] = LOW;
  arduino_mock::forced_read_count[PIN_TOUCH_SDA] = 4;

  EXPECT_EQ(sigurdos::i2c::recover_before_begin(),
            sigurdos::i2c::RecoveryResult::Recovered);
  EXPECT_LE(arduino_mock::pin_mode_calls[PIN_TOUCH_SCL],
            2 * sigurdos::i2c::RECOVERY_CLOCKS + 2);
}

TEST_F(I2cBusTest, RecoveryStopsAfterNineClocksWhenSdaStaysLow)
{
  arduino_mock::forced_read_value[PIN_TOUCH_SDA] = LOW;
  arduino_mock::forced_read_count[PIN_TOUCH_SDA] = 32;

  EXPECT_EQ(sigurdos::i2c::recover_before_begin(),
            sigurdos::i2c::RecoveryResult::Stuck);
  EXPECT_EQ(arduino_mock::pin_mode_calls[PIN_TOUCH_SCL],
            2 * sigurdos::i2c::RECOVERY_CLOCKS + 2);
}

TEST_F(I2cBusTest, BeginRecoversBeforeStartingWire)
{
  sigurdos::i2c::begin();

  EXPECT_TRUE(Wire.mock_was_begun());
  EXPECT_EQ(Wire.mock_begin_count(), 1);
  EXPECT_EQ(Wire.mock_begin_sda(), PIN_TOUCH_SDA);
  EXPECT_EQ(Wire.mock_begin_scl(), PIN_TOUCH_SCL);
  EXPECT_EQ(Wire.mock_clock(), sigurdos::i2c::BUS_CLOCK_HZ);
  EXPECT_EQ(Wire.mock_timeout_ms(), sigurdos::i2c::TRANSACTION_TIMEOUT_MS);
}

TEST_F(I2cBusTest, BeginIsProcessWideIdempotent)
{
  sigurdos::i2c::begin();
  const int scl_modes_after_first_begin = arduino_mock::pin_mode_calls[PIN_TOUCH_SCL];

  sigurdos::i2c::begin();

  EXPECT_EQ(Wire.mock_begin_count(), 1);
  EXPECT_EQ(arduino_mock::pin_mode_calls[PIN_TOUCH_SCL], scl_modes_after_first_begin);
  EXPECT_EQ(Wire.mock_clock(), sigurdos::i2c::BUS_CLOCK_HZ);
  EXPECT_EQ(Wire.mock_timeout_ms(), sigurdos::i2c::TRANSACTION_TIMEOUT_MS);
}

} // namespace
