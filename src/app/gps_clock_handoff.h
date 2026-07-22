#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

#include "hal/gps.h"

namespace sigurdos::app {

inline bool gpsPollDue(uint32_t now, uint32_t last_poll, uint32_t interval_seconds) {
  if (interval_seconds == 0 || interval_seconds > UINT32_MAX / 1000U) return false;
  return static_cast<uint32_t>(now - last_poll) >= interval_seconds * 1000U;
}

template <typename LoopFn, typename PendingFn, typename EpochFn,
          typename SetClockFn, typename MarkFn>
bool serviceGpsClock(bool enabled, uint32_t now, uint32_t interval_seconds,
                     uint32_t& last_poll, LoopFn gps_loop,
                     PendingFn get_pending, EpochFn make_epoch,
                     SetClockFn set_clock, MarkFn mark_synced) {
  if (!enabled) return false;

  if (gpsPollDue(now, last_poll, interval_seconds)) {
    last_poll = now;
    gps_loop();
  }

  SigurdOSGpsUtcTime utc{};
  if (!get_pending(&utc)) return false;

  const uint32_t epoch = make_epoch(utc.year, utc.month, utc.day,
                                    utc.hour, utc.minute) + utc.second;
  if (!set_clock(epoch)) return false;

  mark_synced();
  return true;
}

}  // namespace sigurdos::app
