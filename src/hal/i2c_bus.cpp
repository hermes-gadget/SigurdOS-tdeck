// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "i2c_bus.h"

#include "tdeck_pins.h"
#include "../diagnostics/log.h"
#include <Arduino.h>
#include <Wire.h>

namespace sigurdos::i2c {

static bool bus_started = false;

static void emit_stop_condition()
{
  // SDA low while SCL is released, followed by SDA release.
  digitalWrite(PIN_TOUCH_SDA, LOW);
  pinMode(PIN_TOUCH_SDA, OUTPUT);
  delayMicroseconds(5);
  pinMode(PIN_TOUCH_SCL, INPUT_PULLUP);
  delayMicroseconds(5);
  pinMode(PIN_TOUCH_SDA, INPUT_PULLUP);
  delayMicroseconds(5);
}

bool is_target_address(uint8_t addr)
{
  return addr == KEYBOARD_ADDR ||
         addr == TOUCH_ADDR_PRIMARY ||
         addr == TOUCH_ADDR_ALTERNATE;
}

bool probe_target(uint8_t addr)
{
  if (!is_target_address(addr)) return false;
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

RecoveryResult recover_before_begin()
{
  // Open-drain idle: release both lines and let their pull-ups raise them.
  pinMode(PIN_TOUCH_SDA, INPUT_PULLUP);
  pinMode(PIN_TOUCH_SCL, INPUT_PULLUP);
  delayMicroseconds(5);
  const bool sda_high = digitalRead(PIN_TOUCH_SDA) != LOW;
  const bool scl_high = digitalRead(PIN_TOUCH_SCL) != LOW;
  if (sda_high && scl_high) return RecoveryResult::AlreadyFree;
  // A slave holding SCL low prevents safe clocking. Leave both lines released
  // and let controller initialization report whether the bus can be owned.
  if (!scl_high) return RecoveryResult::Stuck;

  for (uint8_t pulse = 0; pulse < RECOVERY_CLOCKS; ++pulse) {
    // Drive only LOW. INPUT_PULLUP releases the line instead of actively
    // driving it HIGH, preserving open-drain bus behavior.
    digitalWrite(PIN_TOUCH_SCL, LOW);
    pinMode(PIN_TOUCH_SCL, OUTPUT);
    delayMicroseconds(5);
    pinMode(PIN_TOUCH_SCL, INPUT_PULLUP);
    delayMicroseconds(5);

    if (digitalRead(PIN_TOUCH_SDA) != LOW) {
      // Wire takes ownership only after the STOP condition completes.
      emit_stop_condition();
      return RecoveryResult::Recovered;
    }
  }

  pinMode(PIN_TOUCH_SDA, INPUT_PULLUP);
  pinMode(PIN_TOUCH_SCL, INPUT_PULLUP);
  if (digitalRead(PIN_TOUCH_SDA) == LOW) return RecoveryResult::Stuck;
  emit_stop_condition();
  return RecoveryResult::Recovered;
}

void configure_runtime()
{
  Wire.setClock(BUS_CLOCK_HZ);
  Wire.setTimeOut(TRANSACTION_TIMEOUT_MS);
}

bool begin()
{
  if (bus_started) {
    configure_runtime();
    return true;
  }

  const RecoveryResult recovery = recover_before_begin();
  if (!Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL)) {
    SIG_LOGW("i2c controller initialization failed; retry remains enabled");
    return false;
  }
  configure_runtime();
  bus_started = true;

#if defined(SIGURDOS_DEBUG)
  if (recovery == RecoveryResult::Recovered) {
    SIG_LOGD("i2c recovered shared bus before startup");
  } else if (recovery == RecoveryResult::Stuck) {
    SIG_LOGD("i2c WARNING: SDA remained low after recovery clocks");
  }
#else
  (void)recovery;
#endif
  return true;
}

void reset_for_test()
{
  bus_started = false;
}

} // namespace sigurdos::i2c
