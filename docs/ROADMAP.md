# SigurdOS T-Deck Roadmap

**Fresh start — 2026-08-03.** Supersedes the previous ROADMAP.md, MISSING_FEATURES.md,
COMPANION_PARITY_ACTION_PLAN.md, audit.md, REVIEW.md and the Launcher/EFUSE audit docs
(all purged as historical). This is the single source of truth for where the project
is and where it is going.

## Design Goals

1. **"Discord UI on a LoRa radio"** — a polished, handheld mesh messenger on the
   LilyGo T-Deck (ESP32-S3, SX1262, ST7789, GT911, I2C keyboard, trackball).
2. **Full MeshCore protocol compatibility** — interoperates with any MeshCore node
   and acts as a companion radio for the official app over USB, BLE, TCP (5000) and
   WebSocket (8765).
3. **One board, done right.** T-Deck only. T-Deck Plus and other boards are
   explicitly out of scope (Ben, 2026-08-03).
4. **A reliable field device** — battery-aware (idle power regime + lock screen),
   offline-first (SD-backed deep history, offline maps), zero data loss.
5. **Polished UX** — i18n, adaptive text fit, notifications, lock/PIN, theme system.
6. **Security posture** — PIN lock, documented trust boundaries; deliberately no
   eFuse provisioning path (tracks #1210).
7. **Engineering discipline** — 1622 native tests, CI gates, hardware verification
   protocol, release evidence.
8. **Explicitly declined scope** — WebMirror, on-device MQTT bridge, infrastructure
   roles (dedicated repeaters / room servers / sensors), Launcher listing (O1,
   externally blocked), board breadth.

## Current State (verified 2026-08-03)

All items below are merged on `dev`; native suite 1622 green (1621 pass + 1
ESP32-only skip); production + debug + remote-test builds compile; the feature set
was hardware-verified on the T-Deck (screenshots vision-verified, boot/soak logs).

| Area | State | Notes |
| --- | --- | --- |
| Mesh core | ✅ | `SigurdMeshV2` extends `BaseChatMesh`; DMs, channels, ACK, advert discovery, trace, ping, telemetry, regions |
| Multi-transport companion | ✅ merged + UI-verified | TCP:5000 + WS:8765 servers, push-to-all + per-client sync dedup, BLE companion; contract in `src/comms/transport_iface.h` |
| SD deep history | ✅ merged + HW-verified | `/sdcard/msgs` (5000 msgs) with SPIFFS fallback; storage row shows live usage |
| Lock screen + power regime | ✅ merged + HW-verified | Idle power-save, swipe/key unlock, auto-off |
| i18n | ✅ merged + HW-verified | EN/DE/FR/ES, language picker, persisted (NVS); German verified on-device |
| Adaptive text fit | ✅ merged + HW-verified | Font-ladder auto-fit UI-wide, 8px SemiBold floor, no clipping |
| Offline map + markers | ✅ (pre-wave) | Tile cache in PSRAM; regions support |
| Terminal, telemetry, remote-test, notifications, dual OTA, PIN | ✅ (pre-wave) | Protect list, kept intact |
| Native tests | ✅ 1622 | Extended by every wave |
| Hardware verification | ✅ | 2026-08-03 campaign: home, transports, lock/unlock, i18n switch, WiFi scan, storage indicator |

## Known Gaps (audit findings, 2026-08-03)

1. **NVS settings reset on every full merged-image reflash** — issue **#1492**.
   Language and other NVS prefs are lost on USB reflash while SPIFFS state survives.
   Root cause unconfirmed; OTA (app-only) persistence unverified on hardware.
2. **Only 4 locales.** Dutch (NL) is wanted (Ben verified overflow with it); Wadamesh
   ships 12. Add NL + IT + PT as a minimum.
3. **Text-fit residuals** — FR `Configurer la radio`, ES `CONFIGURACIÓN` /
   `Configura la radio` still overflow the SETUP tile even at the 8px floor.
   Fix via shorter translations or ellipsis/wrap policy.
4. **TCP/WS transports never exercised end-to-end with real WiFi creds.**
   UI verified (OFF/waiting states, WiFi scan); actual client connections + sync
   dedup unproven on hardware.
5. **Companion interop matrix incomplete** — official-app protocol over USB/BLE
   validated earlier; TCP/WS leg pending (ties to #4).
6. **GPS** — T-Deck has no GPS module; position comes from the phone/companion.
   UX for "no position" not finalized.
7. **Launcher O2** (return-to-Launcher) — hardware/API gated, evidence pending.
   Everything else launcher-related is complete.
8. **Battery life** — power regime exists; no long-duration battery measurement.
9. **Release gates** — RELEASE_EVIDENCE warning budget + hardware interop matrix +
   soak evidence not yet satisfied for a production release.

## Forward Plan

### Phase A — Reliability (P1)
- [ ] Root-cause + fix #1492 (NVS persistence across reflash/OTA); add a
      reflash-persistence test to the hardware campaign
- [ ] OTA round-trip on hardware (dual OTA slots, branch switches)
- [ ] Battery-life measurement under the idle power regime (target: >2 weeks idle)
- [ ] Soak evidence (12h+, flat heap/PSRAM) recorded in RELEASE_EVIDENCE

### Phase B — i18n completion (P1)
- [ ] Add Dutch (NL) — first, Ben's language
- [ ] Add Italian (IT) + Portuguese (PT) toward Wadamesh's 12-locale parity
- [ ] Resolve SETUP-tile residuals (shorten translations or ellipsis policy)
- [ ] Verify every locale renders clip-free on hardware (grid sweep)

### Phase C — Transport proof (P1)
- [ ] Hardware E2E with real WiFi: TCP client + WS client connect, push-to-all,
      per-client sync dedup verified against the official companion protocol
- [ ] Companion interop matrix completed across USB/BLE/TCP/WS

### Phase D — Field polish (P2)
- [ ] GPS position UX (phone-provided position display)
- [ ] Offline map + SD tile storage verification on hardware
- [ ] Notifications depth (banner actions, unread badges per channel)
- [ ] Power/lock tuning from battery measurements

### Phase E — Release (P2)
- [ ] RELEASE_EVIDENCE gates: warning budget, interop matrix, soak
- [ ] test-builds branch flow for reporter builds
- [ ] Web flasher manifest refresh + release PR

### Deferred / declined (no work planned)
- WebMirror (cancelled 2026-08-03), on-device MQTT (cancelled 2026-08-03),
  board breadth / T-Deck Plus, Launcher listing (externally blocked),
  infrastructure roles.
