#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include <cstdint>
#include <cstddef>

namespace sigurdos::mesh {

enum class TimeSource : uint8_t {
    Unknown = 0,
    Manual,
    Companion,
    GPS,
};

struct TimeSyncStatus {
    TimeSource source;
    uint32_t synced_at;
    uint32_t age_seconds;
};

class TimeSyncTracker {
public:
    void reset();
    void record(TimeSource source, uint32_t synced_at);
    TimeSyncStatus status(uint32_t now) const;

private:
    TimeSource source_ = TimeSource::Unknown;
    uint32_t synced_at_ = 0;
};

const char* timeSourceName(TimeSource source);
void formatTimeSyncStatus(const TimeSyncStatus& status,
                          char* out, size_t out_size);

// Convert a validated UTC calendar value into a Unix epoch. The checked form
// distinguishes the valid epoch value zero from malformed/out-of-range input.
bool utcToEpochChecked(int year, int month, int day,
                       int hour, int minute, int second,
                       uint32_t* epoch_out);
uint32_t utcToEpoch(int year, int month, int day,
                    int hour, int minute, int second = 0);

} // namespace sigurdos::mesh
