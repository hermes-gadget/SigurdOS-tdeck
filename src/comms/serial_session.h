#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ben
//
// Session-generation capability for companion serial transports.
//
// MeshCore's BaseSerialInterface does not expose a connection epoch, so
// SigurdOS-owned serial interfaces report one through this small mixin.
// Transports and the companion bridge receive an explicit pointer to the
// capability at attach/begin time (no RTTI, no patching of MeshCore).

#include <cstdint>

#include <helpers/BaseSerialInterface.h>

namespace sigurdos {
namespace comms {

class SerialSessionSource {
public:
    virtual ~SerialSessionSource() = default;

    // Monotonic id of the currently authenticated session; 0 when no session
    // is live. Reconnectable transports increment on each successful
    // authentication; stable transports may use the connected epoch.
    virtual uint32_t connectionGeneration() const = 0;
};

// Safe accessor for call sites that hold a plain BaseSerialInterface* plus an
// optional session source. Everything without a source keeps the historical
// connected-epoch behaviour (1 while connected), so no call site changes
// meaning when the source is absent.
inline uint32_t serialSessionGenerationFor(const BaseSerialInterface* serial,
                                           const SerialSessionSource* source) {
    if (!serial || !serial->isConnected()) return 0;
    return source ? source->connectionGeneration() : 1u;
}

} // namespace comms
} // namespace sigurdos
