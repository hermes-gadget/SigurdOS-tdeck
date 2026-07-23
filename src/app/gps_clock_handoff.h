#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

#include "hal/gps.h"

namespace sigurdos::app {

template <typename PendingFn, typename EpochFn, typename SetClockFn,
          typename MarkFn>
bool serviceGpsClock(bool requested, PendingFn get_pending, EpochFn make_epoch,
                     SetClockFn set_clock, MarkFn mark_synced) {
  if (!requested) return false;

  SigurdOSGpsUtcTime utc{};
  if (!get_pending(&utc)) return false;

  const uint32_t epoch = make_epoch(utc.year, utc.month, utc.day,
                                    utc.hour, utc.minute) + utc.second;
  if (!set_clock(epoch)) return false;

  mark_synced();
  return true;
}

}  // namespace sigurdos::app
