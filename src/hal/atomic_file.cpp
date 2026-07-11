// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include "atomic_file.h"

#include <cstdio>
#include <cstring>

#if defined(ESP32_PLATFORM)
#include <FS.h>
#include <SPIFFS.h>
#endif

namespace sigurdos::storage {

namespace {

#if !defined(ESP32_PLATFORM)
AtomicFileNativeFault g_native_fault = AtomicFileNativeFault::None;

bool faultIs(AtomicFileNativeFault fault)
{
    return g_native_fault == fault;
}
#endif

bool fileExists(const char* path)
{
#if defined(ESP32_PLATFORM)
    return SPIFFS.exists(path);
#else
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
#endif
}

bool removeFile(const char* path)
{
#if defined(ESP32_PLATFORM)
    return !SPIFFS.exists(path) || SPIFFS.remove(path);
#else
    return std::remove(path) == 0 || !fileExists(path);
#endif
}

bool validatePath(const char* path, AtomicFileValidateFn validate_fn, void* ctx)
{
    if (!validate_fn) return false;
#if !defined(ESP32_PLATFORM)
    if (faultIs(AtomicFileNativeFault::Validate)) return false;
#endif

#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(path, "r");
    if (!file) return false;
    AtomicFileReader reader(&file);
    const bool valid = validate_fn(reader, ctx);
    file.close();
#else
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    AtomicFileReader reader(file);
    const bool valid = validate_fn(reader, ctx);
    std::fclose(file);
#endif
    return valid;
}

bool renameFile(const char* from, const char* to)
{
#if defined(ESP32_PLATFORM)
    return SPIFFS.rename(from, to);
#else
    if (faultIs(AtomicFileNativeFault::Rename)) return false;
    // SPIFFS (IDF 4.4) returns SPIFFS_ERR_CONFLICTING_NAME when the
    // destination exists — no POSIX overwrite. Emulate that here so native
    // tests exercise the same remove-then-rename path as hardware (#837).
    if (fileExists(to)) return false;
    return std::rename(from, to) == 0;
#endif
}

}  // namespace

AtomicFileWriter::AtomicFileWriter(void* handle)
    : handle_(handle), good_(handle != nullptr), bytes_written_(0)
{
}

size_t AtomicFileWriter::write(const void* data, size_t length)
{
    if (!good_ || !data || length == 0) return 0;
#if !defined(ESP32_PLATFORM)
    if (faultIs(AtomicFileNativeFault::Write)) {
        good_ = false;
        return 0;
    }
#endif

#if defined(ESP32_PLATFORM)
    const size_t written = static_cast<File*>(handle_)->write(
        static_cast<const uint8_t*>(data), length);
#else
    const size_t written = std::fwrite(data, 1, length, static_cast<FILE*>(handle_));
#endif
    bytes_written_ += written;
    if (written != length) good_ = false;
    return written;
}

bool AtomicFileWriter::good() const
{
    return good_;
}

size_t AtomicFileWriter::bytesWritten() const
{
    return bytes_written_;
}

AtomicFileReader::AtomicFileReader(void* handle) : handle_(handle)
{
}

size_t AtomicFileReader::read(void* data, size_t length)
{
    if (!handle_ || !data || length == 0) return 0;
#if defined(ESP32_PLATFORM)
    return static_cast<File*>(handle_)->read(static_cast<uint8_t*>(data), length);
#else
    return std::fread(data, 1, length, static_cast<FILE*>(handle_));
#endif
}

bool AtomicFileReader::seek(size_t offset)
{
    if (!handle_) return false;
#if defined(ESP32_PLATFORM)
    return static_cast<File*>(handle_)->seek(offset, SeekSet);
#else
    return std::fseek(static_cast<FILE*>(handle_), (long)offset, SEEK_SET) == 0;
#endif
}

size_t AtomicFileReader::size()
{
    if (!handle_) return 0;
#if defined(ESP32_PLATFORM)
    return static_cast<File*>(handle_)->size();
#else
    FILE* file = static_cast<FILE*>(handle_);
    const long current = std::ftell(file);
    if (current < 0 || std::fseek(file, 0, SEEK_END) != 0) return 0;
    const long end = std::ftell(file);
    std::fseek(file, current, SEEK_SET);
    return end < 0 ? 0 : (size_t)end;
#endif
}

bool atomicFileTempPath(const char* path, char* out, size_t out_size)
{
    if (!path || !path[0] || !out || out_size == 0) return false;
    const int written = std::snprintf(out, out_size, "%s.tmp", path);
    return written > 0 && (size_t)written < out_size;
}

bool atomicFileReplace(const char* path,
                       AtomicFileWriteFn write_fn, void* write_ctx,
                       AtomicFileValidateFn validate_fn, void* validate_ctx)
{
    if (!path || !write_fn || !validate_fn) return false;
    char temp_path[192];
    if (!atomicFileTempPath(path, temp_path, sizeof(temp_path))) return false;
    if (!removeFile(temp_path)) return false;

#if !defined(ESP32_PLATFORM)
    if (faultIs(AtomicFileNativeFault::TempOpen)) return false;
#endif

#if defined(ESP32_PLATFORM)
    File file = SPIFFS.open(temp_path, "w");
    if (!file) return false;
    AtomicFileWriter writer(&file);
    bool ok = write_fn(writer, write_ctx) && writer.good();
    file.close();
#else
    FILE* file = std::fopen(temp_path, "wb");
    if (!file) return false;
    AtomicFileWriter writer(file);
    bool ok = write_fn(writer, write_ctx) && writer.good();
    if (faultIs(AtomicFileNativeFault::Close)) ok = false;
    if (std::fclose(file) != 0) ok = false;
#endif

    if (!ok || !validatePath(temp_path, validate_fn, validate_ctx)) {
        removeFile(temp_path);
        return false;
    }
    // SPIFFS rename fails when the destination exists, so the live file must
    // be removed first — but only now that the temp has validated. A crash in
    // this window leaves the validated temp for atomicFileRecover to promote.
    if (!removeFile(path)) return false;
    return renameFile(temp_path, path);
}

bool atomicFileRecover(const char* path,
                       AtomicFileValidateFn validate_fn, void* validate_ctx)
{
    if (!path || !validate_fn) return false;
    char temp_path[192];
    if (!atomicFileTempPath(path, temp_path, sizeof(temp_path))) return false;
    if (!fileExists(temp_path)) return true;

    if (!validatePath(temp_path, validate_fn, validate_ctx)) {
        removeFile(temp_path);
        return fileExists(path) && validatePath(path, validate_fn, validate_ctx);
    }
    // Same as atomicFileReplace: clear the destination for SPIFFS before
    // promoting the validated temp. The temp survives a failure here, so a
    // later recovery pass can retry the commit.
    if (!removeFile(path)) return false;
    return renameFile(temp_path, path);
}

#if !defined(ESP32_PLATFORM)
void atomicFileSetNativeFault(AtomicFileNativeFault fault)
{
    g_native_fault = fault;
}
#endif

}  // namespace sigurdos::storage
