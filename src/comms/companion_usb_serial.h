// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
#pragma once

#include <Arduino.h>
#include <helpers/BaseSerialInterface.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sigurdos {
namespace comms {

// Length-prefixed companion transport for the USB CDC stream.  Unlike the
// MeshCore ArduinoSerialInterface, malformed lengths are rejected before any
// payload is buffered and incomplete frames expire so the next '<' marker can
// establish a fresh boundary.
class CompanionUsbSerialInterface : public BaseSerialInterface {
public:
    static constexpr uint32_t DEFAULT_INTER_BYTE_TIMEOUT_MS = 1000;

    explicit CompanionUsbSerialInterface(
        uint32_t inter_byte_timeout_ms = DEFAULT_INTER_BYTE_TIMEOUT_MS)
        : _inter_byte_timeout_ms(inter_byte_timeout_ms) {}

    void begin(Stream& serial) {
        _serial = &serial;
        resetParser();
    }

    void enable() override {
        _enabled = true;
        resetParser();
    }

    void disable() override {
        _enabled = false;
        resetParser();
    }

    bool isEnabled() const override { return _enabled; }
    bool isConnected() const override { return _enabled && _serial != nullptr; }
    bool isWriteBusy() const override { return false; }

    size_t writeFrame(const uint8_t src[], size_t len) override {
        if (!_enabled || !_serial || !src || len == 0 || len > MAX_FRAME_SIZE) {
            return 0;
        }

        const uint8_t header[3] = {
            static_cast<uint8_t>('>'),
            static_cast<uint8_t>(len & 0xFFu),
            static_cast<uint8_t>((len >> 8) & 0xFFu),
        };
        for (size_t i = 0; i < sizeof(header); ++i) {
            if (_serial->write(header[i]) != 1) return 0;
        }

        size_t written = 0;
        while (written < len && _serial->write(src[written]) == 1) {
            ++written;
        }
        return written;
    }

    size_t checkRecvFrame(uint8_t dest[]) override {
        if (!_enabled || !_serial || !dest) return 0;

        const uint32_t now = static_cast<uint32_t>(millis());
        if (_state != State::Idle &&
            static_cast<uint32_t>(now - _last_byte_ms) >=
                _inter_byte_timeout_ms) {
            resetParser();
        }

        while (_serial->available() > 0) {
            const int value = _serial->read();
            if (value < 0) break;
            const uint8_t byte = static_cast<uint8_t>(value);
            _last_byte_ms = static_cast<uint32_t>(millis());

            switch (_state) {
            case State::Idle:
                if (byte == static_cast<uint8_t>('<')) {
                    _state = State::LengthLow;
                }
                break;

            case State::LengthLow:
                _frame_len = byte;
                _state = State::LengthHigh;
                break;

            case State::LengthHigh:
                _frame_len |= static_cast<uint16_t>(byte) << 8;
                _received_len = 0;
                if (_frame_len == 0 || _frame_len > MAX_FRAME_SIZE) {
                    resetParser();
                } else {
                    _state = State::Payload;
                }
                break;

            case State::Payload:
                _rx_buf[_received_len++] = byte;
                if (_received_len == _frame_len) {
                    const size_t completed_len = _frame_len;
                    std::memcpy(dest, _rx_buf, completed_len);
                    resetParser();
                    return completed_len;
                }
                break;
            }
        }
        return 0;
    }

private:
    enum class State : uint8_t { Idle, LengthLow, LengthHigh, Payload };

    void resetParser() {
        _state = State::Idle;
        _frame_len = 0;
        _received_len = 0;
        _last_byte_ms = static_cast<uint32_t>(millis());
    }

    bool _enabled = false;
    State _state = State::Idle;
    uint16_t _frame_len = 0;
    uint16_t _received_len = 0;
    uint32_t _last_byte_ms = 0;
    uint32_t _inter_byte_timeout_ms;
    Stream* _serial = nullptr;
    uint8_t _rx_buf[MAX_FRAME_SIZE]{};
};

} // namespace comms
} // namespace sigurdos
