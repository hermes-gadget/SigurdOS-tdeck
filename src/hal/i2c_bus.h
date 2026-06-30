#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstdint>

namespace sigurdos::i2c {

static constexpr uint8_t KEYBOARD_ADDR = 0x55;
static constexpr uint8_t TOUCH_ADDR_PRIMARY = 0x5D;
static constexpr uint8_t TOUCH_ADDR_ALTERNATE = 0x14;
static constexpr uint32_t BUS_CLOCK_HZ = 400000;
static constexpr uint16_t TRANSACTION_TIMEOUT_MS = 20;
static constexpr uint8_t RECOVERY_CLOCKS = 9;

enum class RecoveryResult : uint8_t {
  AlreadyFree,
  Recovered,
  Stuck,
};

// Only the three known T-Deck peripherals are valid probe targets. Runtime
// traffic may use their addresses normally; this guard prevents full-bus scans.
bool is_target_address(uint8_t addr);
bool probe_target(uint8_t addr);

// Clock a slave out of a partial byte before Wire owns the pins. This function
// must not be called while the Wire controller is active.
RecoveryResult recover_before_begin();

// Owns shared-bus startup and applies the runtime clock/timeout contract.
void begin();
void configure_runtime();

// Reset process-wide startup state for native test isolation.
void reset_for_test();

} // namespace sigurdos::i2c
