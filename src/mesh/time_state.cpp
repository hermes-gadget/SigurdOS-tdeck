// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben

#include "time_state.h"

#include <cstdio>

namespace sigurdos::mesh {

void TimeSyncTracker::reset()
{
    source_ = TimeSource::Unknown;
    synced_at_ = 0;
}

void TimeSyncTracker::record(TimeSource source, uint32_t synced_at)
{
    source_ = source;
    synced_at_ = synced_at;
}

TimeSyncStatus TimeSyncTracker::status(uint32_t now) const
{
    const uint32_t age = source_ == TimeSource::Unknown || now < synced_at_
        ? 0
        : now - synced_at_;
    return {source_, synced_at_, age};
}

const char* timeSourceName(TimeSource source)
{
    switch (source) {
    case TimeSource::Manual: return "Manual";
    case TimeSource::Companion: return "Companion";
    case TimeSource::GPS: return "GPS";
    case TimeSource::Unknown:
    default:
        return "Unknown";
    }
}

void formatTimeSyncStatus(const TimeSyncStatus& status,
                          char* out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (status.source == TimeSource::Unknown) {
        std::snprintf(out, out_size, "Unknown");
        return;
    }

    const char* source = timeSourceName(status.source);
    if (status.age_seconds < 60) {
        std::snprintf(out, out_size, "%s, %lus ago", source,
                      static_cast<unsigned long>(status.age_seconds));
    } else if (status.age_seconds < 3600) {
        std::snprintf(out, out_size, "%s, %lum ago", source,
                      static_cast<unsigned long>(status.age_seconds / 60));
    } else if (status.age_seconds < 86400) {
        std::snprintf(out, out_size, "%s, %luh ago", source,
                      static_cast<unsigned long>(status.age_seconds / 3600));
    } else {
        std::snprintf(out, out_size, "%s, %lud ago", source,
                      static_cast<unsigned long>(status.age_seconds / 86400));
    }
}

uint32_t utcToEpoch(int year, int month, int day,
                    int hour, int minute, int second)
{
    // Howard Hinnant's civil-calendar algorithm. It does not depend on the
    // process timezone and remains valid throughout the 32-bit Unix range.
    int y = year;
    unsigned m = static_cast<unsigned>(month);
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m - 3u) + 2u) / 5u
        + static_cast<unsigned>(day - 1);
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    const int days = era * 146097 + static_cast<int>(doe) - 719468;
    return static_cast<uint32_t>(days) * 86400u
        + static_cast<uint32_t>(hour) * 3600u
        + static_cast<uint32_t>(minute) * 60u
        + static_cast<uint32_t>(second);
}

} // namespace sigurdos::mesh
