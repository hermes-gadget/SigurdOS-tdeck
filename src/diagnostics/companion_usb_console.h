#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

// The companion USB protocol owns the ESP32-S3 USB CDC stream.  This header is
// force-included for project sources in that build so ordinary Serial logging
// cannot insert text into the binary frame stream.

#if defined(__cplusplus) && defined(SIGURDOS_COMPANION_USB) && \
    SIGURDOS_COMPANION_USB

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

namespace sigurdos {
namespace diagnostics {

// Capture the real global before Serial is redirected below.
inline decltype(Serial)& companionUsbDataSerial() { return Serial; }
inline Stream& companionUsbDataStream() { return companionUsbDataSerial(); }

class CompanionUsbDiscardConsole : public Stream {
public:
    template <typename... Args>
    void begin(Args... args) {
        // setup() still initializes USB CDC; only its subsequent console
        // writes are discarded.
        companionUsbDataSerial().begin(args...);
    }

    void end() {}
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    int availableForWrite() { return 0; }

    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t*, size_t len) override { return len; }

    template <typename T>
    size_t print(const T&) { return 0; }

    template <typename T>
    size_t println(const T&) { return 0; }

    size_t println() { return 0; }

    template <typename... Args>
    size_t printf(const char*, Args...) { return 0; }

    void setDebugOutput(bool) {}
    explicit operator bool() const { return true; }
};

inline CompanionUsbDiscardConsole& companionUsbDiscardConsole() {
    static CompanionUsbDiscardConsole console;
    return console;
}

} // namespace diagnostics
} // namespace sigurdos

#define Serial (::sigurdos::diagnostics::companionUsbDiscardConsole())

#endif
