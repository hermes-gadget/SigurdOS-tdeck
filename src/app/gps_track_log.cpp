// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "gps_track_log.h"

#include "../hal/atomic_file.h"
#include "../hal/storage.h"

#include <cstdio>
#include <cstring>

#if defined(ESP32_PLATFORM)
#include <FS.h>
#include <SPIFFS.h>
#endif

namespace sigurdos::app {
namespace {

static constexpr uint32_t TRACK_MAGIC = 0x4B525447;  // "GTRK" little-endian
static constexpr uint16_t TRACK_VERSION = 1;

struct TrackHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t point_size;
};

static_assert(sizeof(TrackHeader) == 8, "track header layout changed");
static_assert(sizeof(GpsTrackPoint) == 12, "track point layout changed");

#if defined(ESP32_PLATFORM)
static constexpr const char* TRACK_PATH = "/gps_track.bin";
#else
static char g_track_path[192] = "/tmp/sigurdos_gps_track.bin";
#define TRACK_PATH g_track_path
#endif

bool g_service_started = false;
uint32_t g_last_record_ms = 0;

bool headerValid(const TrackHeader& header)
{
    return header.magic == TRACK_MAGIC && header.version == TRACK_VERSION &&
        header.point_size == sizeof(GpsTrackPoint);
}

bool pointValid(const GpsTrackPoint& point)
{
    return gpsTrackCoordinateValid(
        gpsTrackLatitude(point), gpsTrackLongitude(point));
}

#if defined(ESP32_PLATFORM)
using TrackFile = File;

TrackFile openRead()
{
    return SPIFFS.open(TRACK_PATH, "r");
}

TrackFile openAppend()
{
    return SPIFFS.open(TRACK_PATH, "a");
}

bool fileExists()
{
    return SPIFFS.exists(TRACK_PATH);
}

bool removeFile()
{
    return !SPIFFS.exists(TRACK_PATH) || SPIFFS.remove(TRACK_PATH);
}

std::size_t fileSize(TrackFile& file)
{
    return file.size();
}

bool seekFile(TrackFile& file, std::size_t offset)
{
    return file.seek(offset, SeekSet);
}

std::size_t readFile(TrackFile& file, void* data, std::size_t size)
{
    return file.read(static_cast<uint8_t*>(data), size);
}

std::size_t writeFile(TrackFile& file, const void* data, std::size_t size)
{
    return file.write(static_cast<const uint8_t*>(data), size);
}

void closeFile(TrackFile& file)
{
    file.close();
}

bool fileOpen(const TrackFile& file)
{
    return static_cast<bool>(file);
}
#else
using TrackFile = FILE*;

TrackFile openRead()
{
    return std::fopen(TRACK_PATH, "rb");
}

TrackFile openAppend()
{
    return std::fopen(TRACK_PATH, "ab");
}

bool fileExists()
{
    FILE* file = std::fopen(TRACK_PATH, "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
}

bool removeFile()
{
    return std::remove(TRACK_PATH) == 0 || !fileExists();
}

std::size_t fileSize(TrackFile& file)
{
    const long current = std::ftell(file);
    if (current < 0 || std::fseek(file, 0, SEEK_END) != 0) return 0;
    const long end = std::ftell(file);
    std::fseek(file, current, SEEK_SET);
    return end < 0 ? 0 : static_cast<std::size_t>(end);
}

bool seekFile(TrackFile& file, std::size_t offset)
{
    return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0;
}

std::size_t readFile(TrackFile& file, void* data, std::size_t size)
{
    return std::fread(data, 1, size, file);
}

std::size_t writeFile(TrackFile& file, const void* data, std::size_t size)
{
    return std::fwrite(data, 1, size, file);
}

void closeFile(TrackFile& file)
{
    if (file) std::fclose(file);
    file = nullptr;
}

bool fileOpen(const TrackFile& file)
{
    return file != nullptr;
}
#endif

bool inspectFile(std::size_t* point_count, bool* has_partial_tail = nullptr)
{
    if (point_count) *point_count = 0;
    if (has_partial_tail) *has_partial_tail = false;
    TrackFile file = openRead();
    if (!fileOpen(file)) return false;
    TrackHeader header{};
    const bool valid_header =
        readFile(file, &header, sizeof(header)) == sizeof(header) &&
        headerValid(header);
    const std::size_t size = fileSize(file);
    closeFile(file);
    if (!valid_header || size < sizeof(TrackHeader)) return false;
    const std::size_t payload_size = size - sizeof(TrackHeader);
    if (point_count) *point_count = payload_size / sizeof(GpsTrackPoint);
    if (has_partial_tail) {
        *has_partial_tail = payload_size % sizeof(GpsTrackPoint) != 0;
    }
    return true;
}

bool validateTrackFile(sigurdos::storage::AtomicFileReader& reader, void*)
{
    TrackHeader header{};
    if (reader.read(&header, sizeof(header)) != sizeof(header) ||
        !headerValid(header)) {
        return false;
    }
    const std::size_t size = reader.size();
    if (size < sizeof(header) ||
        (size - sizeof(header)) % sizeof(GpsTrackPoint) != 0) {
        return false;
    }
    const std::size_t count =
        (size - sizeof(header)) / sizeof(GpsTrackPoint);
    if (count > GPS_TRACK_MAX_POINTS) return false;
    GpsTrackPoint point{};
    for (std::size_t i = 0; i < count; ++i) {
        if (reader.read(&point, sizeof(point)) != sizeof(point) ||
            !pointValid(point)) {
            return false;
        }
    }
    return true;
}

struct RewriteContext {
    std::size_t skip;
    std::size_t count;
};

bool writeTrackSnapshot(sigurdos::storage::AtomicFileWriter& writer, void* raw)
{
    const auto* context = static_cast<const RewriteContext*>(raw);
    const TrackHeader header{TRACK_MAGIC, TRACK_VERSION, sizeof(GpsTrackPoint)};
    if (writer.write(&header, sizeof(header)) != sizeof(header)) return false;
    if (!context || context->count == 0) return true;

    TrackFile source = openRead();
    if (!fileOpen(source) ||
        !seekFile(source, sizeof(TrackHeader) +
            context->skip * sizeof(GpsTrackPoint))) {
        closeFile(source);
        return false;
    }
    GpsTrackPoint points[16];
    std::size_t remaining = context->count;
    bool ok = true;
    while (remaining > 0) {
        const std::size_t batch = remaining < 16 ? remaining : 16;
        const std::size_t bytes = batch * sizeof(GpsTrackPoint);
        if (readFile(source, points, bytes) != bytes ||
            writer.write(points, bytes) != bytes) {
            ok = false;
            break;
        }
        remaining -= batch;
    }
    closeFile(source);
    return ok;
}

bool rewriteNewest(std::size_t keep_newest)
{
    std::size_t count = 0;
    if (!inspectFile(&count)) return false;
    if (keep_newest > count) keep_newest = count;
    RewriteContext context{count - keep_newest, keep_newest};
    return sigurdos::storage::atomicFileReplace(
        TRACK_PATH, writeTrackSnapshot, &context, validateTrackFile, nullptr);
}

bool createEmpty()
{
    RewriteContext context{0, 0};
    return sigurdos::storage::atomicFileReplace(
        TRACK_PATH, writeTrackSnapshot, &context, validateTrackFile, nullptr);
}

}  // namespace

bool gpsTrackInit()
{
#if defined(ESP32_PLATFORM)
    if (!sigurdos::storage_available()) return false;
#endif
    if (!sigurdos::storage::atomicFileRecover(
            TRACK_PATH, validateTrackFile, nullptr)) {
        return false;
    }
    if (!fileExists()) return createEmpty();
    std::size_t count = 0;
    bool partial = false;
    if (!inspectFile(&count, &partial)) return false;
    if (partial || count > GPS_TRACK_MAX_POINTS) {
        const std::size_t keep =
            count > GPS_TRACK_MAX_POINTS ? GPS_TRACK_COMPACT_POINTS : count;
        return rewriteNewest(keep);
    }
    return true;
}

bool gpsTrackAppend(double latitude, double longitude, uint32_t timestamp)
{
    if (!gpsTrackCoordinateValid(latitude, longitude) || !gpsTrackInit()) {
        return false;
    }
    std::size_t count = 0;
    if (!inspectFile(&count)) return false;
    if (count >= GPS_TRACK_MAX_POINTS &&
        !rewriteNewest(GPS_TRACK_COMPACT_POINTS)) {
        return false;
    }
    const GpsTrackPoint point = gpsTrackEncode(latitude, longitude, timestamp);
    TrackFile file = openAppend();
    if (!fileOpen(file)) return false;
    const bool ok = writeFile(file, &point, sizeof(point)) == sizeof(point);
    closeFile(file);
    return ok;
}

bool gpsTrackService(bool enabled, uint32_t interval_s, bool has_fix,
                     double latitude, double longitude, uint32_t timestamp,
                     uint32_t now_ms)
{
    if (!enabled) {
        g_service_started = false;
        return false;
    }
    if (!has_fix || !gpsTrackCoordinateValid(latitude, longitude)) return false;
    const uint32_t interval_ms = gpsTrackClampInterval(interval_s) * 1000UL;
    if (g_service_started &&
        static_cast<uint32_t>(now_ms - g_last_record_ms) < interval_ms) {
        return false;
    }
    g_service_started = true;
    g_last_record_ms = now_ms;
    const uint32_t recorded_at = timestamp != 0
        ? timestamp
        : (0x80000000UL | ((now_ms / 1000UL) & 0x7FFFFFFFUL));
    return gpsTrackAppend(latitude, longitude, recorded_at);
}

std::size_t gpsTrackLoadRecent(GpsTrackPoint* out, std::size_t max_points)
{
    if (!out || max_points == 0 || !gpsTrackInit()) return 0;
    std::size_t count = 0;
    if (!inspectFile(&count)) return 0;
    const std::size_t selected = count < max_points ? count : max_points;
    TrackFile file = openRead();
    if (!fileOpen(file) ||
        !seekFile(file, sizeof(TrackHeader) +
            (count - selected) * sizeof(GpsTrackPoint))) {
        closeFile(file);
        return 0;
    }
    const std::size_t bytes = selected * sizeof(GpsTrackPoint);
    const std::size_t read = readFile(file, out, bytes);
    closeFile(file);
    return read / sizeof(GpsTrackPoint);
}

bool gpsTrackGetStats(GpsTrackStats* out)
{
    if (!out) return false;
    *out = {};
    if (!gpsTrackInit()) return false;
    std::size_t count = 0;
    if (!inspectFile(&count)) return false;
    TrackFile file = openRead();
    if (!fileOpen(file) || !seekFile(file, sizeof(TrackHeader))) {
        closeFile(file);
        return false;
    }

    GpsTrackPoint previous{};
    GpsTrackPoint point{};
    bool have_previous = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (readFile(file, &point, sizeof(point)) != sizeof(point) ||
            !pointValid(point)) {
            closeFile(file);
            return false;
        }
        if (!have_previous) {
            out->started_at = point.timestamp;
        } else {
            out->distance_m += gpsTrackDistanceMeters(previous, point);
            const bool same_time_domain =
                ((previous.timestamp ^ point.timestamp) & 0x80000000UL) == 0;
            if (same_time_domain && point.timestamp >= previous.timestamp) {
                out->duration_s += point.timestamp - previous.timestamp;
            }
        }
        previous = point;
        have_previous = true;
    }
    closeFile(file);
    out->waypoint_count = count;
    if (have_previous) {
        out->ended_at = previous.timestamp;
    }
    return true;
}

bool gpsTrackTrim(std::size_t keep_newest)
{
    if (!gpsTrackInit()) return false;
    if (keep_newest > GPS_TRACK_MAX_POINTS) keep_newest = GPS_TRACK_MAX_POINTS;
    return rewriteNewest(keep_newest);
}

bool gpsTrackClear()
{
#if defined(ESP32_PLATFORM)
    if (!sigurdos::storage_available()) return false;
#endif
    if (!removeFile()) return false;
    g_service_started = false;
    return createEmpty();
}

#if !defined(ESP32_PLATFORM)
void gpsTrackSetPathForTest(const char* path)
{
    if (!path || !path[0]) return;
    std::snprintf(g_track_path, sizeof(g_track_path), "%s", path);
    g_track_path[sizeof(g_track_path) - 1] = '\0';
}

void gpsTrackResetServiceForTest()
{
    g_service_started = false;
    g_last_record_ms = 0;
}
#endif

}  // namespace sigurdos::app
