// transport_iface.h — Companion transport contract (Wadamesh-parity plan, Phase 1)
//
// Seeded on dev by Hermes on 2026-08-03 as the cross-agent API contract for the
// multi-transport companion workstream. Do NOT change this public API without
// flagging it in your commit message.
//
// Implementations (owner: companion-transports agent):
//   src/comms/transports.cpp        — registry, init/loop, enable/status persistence
//   src/comms/transport_tcp.*       — TCP server, port 5000, multi-client
//   src/comms/transport_ws.*        — WebSocket server, port 8765 (plain ws://)
// UI consumers (owner: network-ui agent) MUST only use this header — never the
// transport internals.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sigurdos {
namespace comms {

enum class TransportId : uint8_t {
    BLE = 0,  // existing NUS companion transport — behaviour must not change
    TCP = 1,  // plain TCP companion server, port 5000
    WS = 2,   // WebSocket companion server, port 8765 (ws:// only)
};

struct TransportStatus {
    bool enabled = false;     // user setting (persisted in NodePrefs)
    bool connected = false;   // at least one client / link up
    int clientCount = 0;      // active clients (BLE: 0 or 1)
    const char* detail = "";  // human-readable, e.g. "listening :5000" / "3 clients"
};

// --- control (called from Settings UI) ---
// Persist the user toggle via NodePrefs and start/stop the server accordingly.
// Returns false if the transport cannot start (e.g. no WiFi).
bool transport_set_enabled(TransportId id, bool on);
bool transport_enabled(TransportId id);

// --- status (called from Settings UI) ---
TransportStatus transport_status(TransportId id);

// --- lifecycle (declared here, wired into main.cpp by the integrator) ---
void transports_init();   // register transports, restore persisted enable state
void transports_loop();   // non-blocking; call from the main loop each iteration

// --- frame plumbing ---
// Inbound companion frames from ANY transport are delivered to this handler.
// The CompanionBridge registers itself as the handler so the existing command
// dispatcher is transport-agnostic.
using TransportFrameHandler = void (*)(const uint8_t* frame, size_t len,
                                       TransportId from);
void transports_set_frame_handler(TransportFrameHandler handler);

// Push a companion frame to every connected client of the given transport
// (push-to-all). History sync replay must only go to the requesting client —
// the transport layer exposes per-client send for that:
//   void transport_send_to(TransportId id, int clientIndex,
//                          const uint8_t* frame, size_t len);
void transports_broadcast(TransportId id, const uint8_t* frame, size_t len);
void transport_send_to(TransportId id, int clientIndex, const uint8_t* frame,
                       size_t len);

}  // namespace comms
}  // namespace sigurdos
