#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#include <cstddef>
#include <cstdint>

namespace sigurdos::storage {

class AtomicFileWriter {
public:
    explicit AtomicFileWriter(void* handle);
    size_t write(const void* data, size_t length);
    bool good() const;
    size_t bytesWritten() const;

private:
    void* handle_;
    bool good_;
    size_t bytes_written_;
};

class AtomicFileReader {
public:
    explicit AtomicFileReader(void* handle);
    size_t read(void* data, size_t length);
    bool seek(size_t offset);
    size_t size();

private:
    void* handle_;
};

using AtomicFileWriteFn = bool (*)(AtomicFileWriter& writer, void* ctx);
using AtomicFileValidateFn = bool (*)(AtomicFileReader& reader, void* ctx);

// Stream a replacement to <path>.tmp, close it, validate the complete file,
// then atomically rename it over path. A valid temp is retained if only the
// final rename fails so boot recovery can finish the commit.
bool atomicFileReplace(const char* path,
                       AtomicFileWriteFn write_fn, void* write_ctx,
                       AtomicFileValidateFn validate_fn, void* validate_ctx);

// Recover an interrupted replacement. Invalid temps are removed without
// touching the live file; valid temps are atomically promoted.
bool atomicFileRecover(const char* path,
                       AtomicFileValidateFn validate_fn, void* validate_ctx);

bool atomicFileTempPath(const char* path, char* out, size_t out_size);

#if !defined(ESP32_PLATFORM)
enum class AtomicFileNativeFault {
    None,
    TempOpen,
    Write,
    Close,
    Validate,
    Rename,
};

void atomicFileSetNativeFault(AtomicFileNativeFault fault);
#endif

}  // namespace sigurdos::storage
