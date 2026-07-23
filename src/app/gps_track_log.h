#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sigurdos::app {

static constexpr std::size_t GPS_TRACK_MAX_POINTS = 2048;
static constexpr std::size_t GPS_TRACK_COMPACT_POINTS = 1024;
static constexpr std::size_t GPS_TRACK_MAP_POINTS = 256;
static constexpr uint32_t GPS_TRACK_DEFAULT_INTERVAL_S = 15;
static constexpr uint32_t GPS_TRACK_MIN_INTERVAL_S = 1;
static constexpr uint32_t GPS_TRACK_MAX_INTERVAL_S = 3600;

struct GpsTrackPoint {
    int32_t latitude_e7;
    int32_t longitude_e7;
    uint32_t timestamp;
};

struct GpsTrackStats {
    std::size_t waypoint_count;
    double distance_m;
    uint32_t duration_s;
    uint32_t started_at;
    uint32_t ended_at;
};

inline bool gpsTrackCoordinateValid(double latitude, double longitude)
{
    return std::isfinite(latitude) && std::isfinite(longitude) &&
        latitude >= -90.0 && latitude <= 90.0 &&
        longitude >= -180.0 && longitude <= 180.0;
}

inline GpsTrackPoint gpsTrackEncode(double latitude, double longitude,
                                    uint32_t timestamp)
{
    return {
        static_cast<int32_t>(std::lround(latitude * 10000000.0)),
        static_cast<int32_t>(std::lround(longitude * 10000000.0)),
        timestamp,
    };
}

inline double gpsTrackLatitude(const GpsTrackPoint& point)
{
    return static_cast<double>(point.latitude_e7) / 10000000.0;
}

inline double gpsTrackLongitude(const GpsTrackPoint& point)
{
    return static_cast<double>(point.longitude_e7) / 10000000.0;
}

inline double gpsTrackDistanceMeters(const GpsTrackPoint& first,
                                     const GpsTrackPoint& second)
{
    static constexpr double EARTH_RADIUS_M = 6371000.0;
    static constexpr double TRACK_DEGREES_TO_RADIANS =
        0.01745329251994329577;
    const double lat1 =
        gpsTrackLatitude(first) * TRACK_DEGREES_TO_RADIANS;
    const double lat2 =
        gpsTrackLatitude(second) * TRACK_DEGREES_TO_RADIANS;
    const double dlat = lat2 - lat1;
    const double dlon =
        (gpsTrackLongitude(second) - gpsTrackLongitude(first)) *
        TRACK_DEGREES_TO_RADIANS;
    const double sin_lat = std::sin(dlat / 2.0);
    const double sin_lon = std::sin(dlon / 2.0);
    double a = sin_lat * sin_lat +
        std::cos(lat1) * std::cos(lat2) * sin_lon * sin_lon;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    return EARTH_RADIUS_M * 2.0 *
        std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

inline uint32_t gpsTrackClampInterval(uint32_t interval_s)
{
    if (interval_s < GPS_TRACK_MIN_INTERVAL_S) return GPS_TRACK_MIN_INTERVAL_S;
    if (interval_s > GPS_TRACK_MAX_INTERVAL_S) return GPS_TRACK_MAX_INTERVAL_S;
    return interval_s;
}

inline uint32_t gpsTrackGpsDemandInterval(bool gps_enabled,
                                          uint32_t gps_interval_s,
                                          bool track_enabled,
                                          uint32_t track_interval_s)
{
    if (!track_enabled) return gps_enabled ? gps_interval_s : 0;
    const uint32_t track_interval = gpsTrackClampInterval(track_interval_s);
    if (!gps_enabled || gps_interval_s == 0 ||
        track_interval < gps_interval_s) {
        return track_interval;
    }
    return gps_interval_s;
}

bool gpsTrackInit();
bool gpsTrackAppend(double latitude, double longitude, uint32_t timestamp);
bool gpsTrackService(bool enabled, uint32_t interval_s, bool has_fix,
                     double latitude, double longitude, uint32_t timestamp,
                     uint32_t now_ms);
std::size_t gpsTrackLoadRecent(GpsTrackPoint* out, std::size_t max_points);
bool gpsTrackGetStats(GpsTrackStats* out);
bool gpsTrackTrim(std::size_t keep_newest = GPS_TRACK_COMPACT_POINTS);
bool gpsTrackClear();

#if !defined(ESP32_PLATFORM)
void gpsTrackSetPathForTest(const char* path);
void gpsTrackResetServiceForTest();
#endif

}  // namespace sigurdos::app
