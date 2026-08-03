// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben

#pragma once

#include <cstddef>
#include <cstdint>

#include <helpers/BaseSerialInterface.h>

namespace sigurdos {
namespace comms {

// Companion streams use the same little-endian length prefix as the serial
// transports.  Keeping this parser independent of WiFi makes its boundary and
// oversize-frame behavior testable on the native target as well as on-device.
class CompanionFrameParser {
public:
    static constexpr size_t MAX_PAYLOAD = MAX_FRAME_SIZE;

    CompanionFrameParser() { reset(); }

    void reset()
    {
        _state = State::SeekMarker;
        _expected = 0;
        _received = 0;
        _ready = false;
    }

    bool push(uint8_t byte)
    {
        if (_ready) return false;

        switch (_state) {
        case State::SeekMarker:
            if (byte == '<') _state = State::LengthLow;
            return false;
        case State::LengthLow:
            _expected = byte;
            _state = State::LengthHigh;
            return false;
        case State::LengthHigh:
            _expected |= static_cast<uint16_t>(byte) << 8;
            if (_expected == 0 || _expected > MAX_PAYLOAD) {
                reset();
                if (byte == '<') _state = State::LengthLow;
                return false;
            }
            _received = 0;
            _state = State::Payload;
            return false;
        case State::Payload:
            _payload[_received++] = byte;
            if (_received == _expected) {
                _ready = true;
                _state = State::SeekMarker;
                return true;
            }
            return false;
        }
        return false;
    }

    bool available() const { return _ready; }

    bool take(uint8_t* out, size_t capacity, size_t* length_out)
    {
        if (!_ready || !out || capacity < _expected) return false;
        for (size_t i = 0; i < _expected; ++i) out[i] = _payload[i];
        if (length_out) *length_out = _expected;
        _expected = 0;
        _received = 0;
        _ready = false;
        return true;
    }

private:
    enum class State : uint8_t {
        SeekMarker,
        LengthLow,
        LengthHigh,
        Payload,
    };

    State _state = State::SeekMarker;
    uint16_t _expected = 0;
    uint16_t _received = 0;
    bool _ready = false;
    uint8_t _payload[MAX_PAYLOAD]{};
};

inline size_t encodeCompanionTransportFrame(uint8_t* out, size_t capacity,
                                             const uint8_t* payload, size_t len)
{
    if (!out || !payload || len == 0 || len > MAX_FRAME_SIZE ||
        capacity < len + 3) return 0;
    out[0] = '>';
    out[1] = static_cast<uint8_t>(len & 0xFF);
    out[2] = static_cast<uint8_t>((len >> 8) & 0xFF);
    for (size_t i = 0; i < len; ++i) out[i + 3] = payload[i];
    return len + 3;
}

} // namespace comms
} // namespace sigurdos
