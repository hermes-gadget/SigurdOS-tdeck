#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Native mock for ESP32 SPIFFS filesystem.
// Provides enough of the SPIFFS API surface for storage_init() testing.

#include <cstddef>
#include <cstdint>

// ── Minimal FS::File (used by SPIFFS.open) ───────────────
namespace fs {
class File {
public:
    File() {}
    operator bool() const { return _valid; }
    size_t write(const uint8_t* buf, size_t len) { (void)buf; return _valid ? len : 0; }
    size_t read(uint8_t* buf, size_t len) { (void)buf; return _valid ? len : 0; }
    size_t size() const { return 0; }
    void close() {}
    void mock_set_valid(bool v) { _valid = v; }
private:
    bool _valid = false;
};
} // namespace fs

// ── SPIFFS class ────────────────────────────────────────
class SPIFFSClass {
public:
    bool begin(bool formatIfMountFailed = false) {
        if (_mock_mount_succeeds) return true;
        if (formatIfMountFailed && _mock_format_succeeds) {
            _mock_formatted = true;
            return true;
        }
        return false;
    }

    bool format() {
        if (_mock_format_succeeds) {
            _mock_formatted = true;
            _mock_mount_succeeds = _mock_post_format_mount_succeeds;
            return true;
        }
        return false;
    }

    // Static singleton methods (matching ESP32 SPIFFS)
    static SPIFFSClass& getInstance() {
        static SPIFFSClass instance;
        return instance;
    }

    // Test control
    void mock_set_mount_result(bool succeeds) { _mock_mount_succeeds = succeeds; }
    void mock_set_format_result(bool succeeds) { _mock_format_succeeds = succeeds; }
    void mock_set_post_format_mount_result(bool succeeds) {
        _mock_post_format_mount_succeeds = succeeds;
    }
    bool mock_was_formatted() const { return _mock_formatted; }
    void mock_reset() {
        _mock_mount_succeeds = false;
        _mock_format_succeeds = true;
        _mock_post_format_mount_succeeds = true;
        _mock_formatted = false;
    }

private:
    bool _mock_mount_succeeds = false;
    bool _mock_format_succeeds = true;
    bool _mock_post_format_mount_succeeds = true;
    bool _mock_formatted = false;
};

extern SPIFFSClass SPIFFS;
