# SigurdOS T-Deck Roadmap

**Fresh start — 2026-08-03.** Supersedes the previous ROADMAP.md, MISSING_FEATURES.md,
COMPANION_PARITY_ACTION_PLAN.md, audit.md, REVIEW.md and the Launcher/EFUSE audit docs
(all purged as historical). This is the single source of truth for where the project
is, where it is going, and **exactly how to ship it**. Companion:
[`PROJECT_HISTORY.md`](PROJECT_HISTORY.md) (the full-arc history and end-goal analysis).

---

## 1. Design Goals

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

---

## 2. Current State (verified 2026-08-03)

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

---

## 3. Known Gaps (audit findings, 2026-08-03)

1. ~~**NVS settings reset on every full merged-image reflash**~~ — **RESOLVED
   2026-08-04 (issue #1492 closed)**. Root cause: `firmware-merged.bin` carries
   0xFF padding over the NVS + nvs_keys span (0x9000–0xE000), so `write-flash
   0x0` physically erases NVS every time. Fix: procedure — merged image is
   fresh-install-only; upgrades use component flash (bootloader 0x0, ptable
   0x8000, app 0x10000) or OTA. Proven on hardware: German setting survived
   reboot + component reflash; wiped by merged-image flash (control).
2. **Only 4 locales.** Dutch (NL) is wanted; Wadamesh ships 12. Add NL + IT + PT.
3. **Text-fit residuals** — FR `Configurer la radio`, ES `CONFIGURACIÓN` /
   `Configura la radio` still overflow the SETUP tile at the 8px floor.
4. **TCP/WS transports never exercised end-to-end with real WiFi creds.**
5. **Companion interop matrix incomplete** — USB/BLE validated; TCP/WS leg pending.
6. **GPS UX** — no module on T-Deck; position comes from the phone/companion.
7. **Launcher O2** (return-to-Launcher) — hardware/API gated, evidence pending.
8. **Battery life unmeasured** — power regime exists; no long-duration numbers.
9. **Release gates open** — RELEASE_EVIDENCE warning budget + interop matrix +
   soak evidence not yet satisfied for a stable release.

---

## 4. How to Ship — Phase A: Reliability (P1, do first)

> **Why first:** device identity equals mesh presence. Data loss is the one bug
> that breaks the product promise. Everything below has a concrete exit criterion.

### A1 — Fix NVS persistence across reflash/OTA (issue #1492)
- [x] Root-caused on-device (2026-08-04): merged image erases NVS (0xFF padding
      over 0x9000–0xE000). Reproduced both directions on the T-Deck: German
      survived reboot + component reflash; merged-image flash wiped it.
- [x] Fixed in tooling/docs: `firmware/README.md` + `docs/HARDWARE_TESTING.md`
      now mandate component flash / OTA for upgrades; merged image = fresh
      installs only.
- [x] Regression step added to the hardware campaign (below): settings must
      survive 3 component reflashes + one OTA round-trip.
- **Exit: ✅ #1492 closed 2026-08-04; NVS proven persistent across reflash/OTA.**

### A2 — OTA round-trip on hardware
- [x] Dual-slot switching verified on hardware (2026-08-04): `esp_ota_set_boot_partition`
      (the exact API field OTA uses) → reboot → booted app1; switch back → booted
      app0. `[ota-diag] boot=… running=…` confirms each boot. New test-controller
      command `ota-set <0|1>` makes this repeatable. (Otadata is sector-aligned:
      entries at +0/+4096, seq odd→app0 / even→app1, state NEW→VALID.)
- [x] Rollback path (bad image → fallback slot) + WiFi OTA end-to-end — **verified on
      hardware 2026-08-05** (fix #1495): corrupt image → clean HTTP 500 rejection;
      real 3 MB upload → HTTP 200 → reboot into new image; NVS survived the OTA
      flash (PIN + settings intact); second OTA session after the round-trip
      broadcast and accepted clients again.
- **Exit: ✅ both slots verified; WiFi-OTA + rollback verified end-to-end (#1495 closed).**

### A3 — Battery-life measurement
- [x] Logging started 2026-08-04 (soak-watch cron, 15-min samples of `batt=`
      from `[stat]`, log at `/home/ben/sigurdos-soak/soak.log`).
- [ ] Target: **>2 weeks idle** on a full charge (measurement runs ~2 weeks).
- **Exit:** measurement recorded in RELEASE_EVIDENCE when the curve completes.

### A4 — Soak evidence
- [x] 12h+ soak windows on the instrumented remote-test build (identical
      power/mesh/UI code; `[stat]` heartbeat + batt every 5s at idle). Windows on
      2026-08-04 (00:15 start interrupted by B2/B3 device work; later windows +
      partial evidence appended in 16ef7599). **Owner signed off 2026-08-04:
      operational soak was long and clean; this is not a completed release-grade
      evidence record.**
- [x] Soak report appended to RELEASE_EVIDENCE (partial, 16ef7599; superseded,
      not a release PASS).
- **Exit: ✅ operational soak evidence satisfied (owner sign-off); release-grade
  A4 evidence remains open until a completed record is attached.**

---

## 5. How to Ship — Phase B: i18n Completion (P1)

### B1 — Dutch (NL), first
- [x] `Language::Dutch` added (enum value 4); full 52-string table in
      `src/i18n/i18n.cpp`; picker auto-extends (`language_count` loop); i18n +
      text-fit tests extended; docs/I18N.md updated.
- [x] **Latent bug found by on-device testing:** `prefs.cpp` clamped
      `language >= I18N_LANGUAGE_COUNT` to 0 with the count **hardcoded to 4** —
      Dutch (4) was silently reverted to English on save AND on boot. Fixed by
      deriving the clamp from `i18n::Language::Count`; regression test
      `EveryLanguageRoundTripsThroughTheNvsLoadPath` added (fails on old code).
- [x] On-device: Dutch selected via the picker; home grid sweep capture —
      **all 12 tiles fit, zero clipping** (incl. INSTELLINGEN + AANKONDIGEN at
      the 8px floor); **Dutch persists across reboot** (NVS round-trip proven).
- **Exit: ✅ B1 complete 2026-08-04 (commit pending final suite).**

### B2 — Italian (IT) + Portuguese (PT)
- [x] `Language::Italian` (5) + `Language::Portuguese` (6), complete 52-string
      tables each; picker/names auto-extend; i18n drop-in tests + text-fit
      coverage (IMPOSTAZIONI/REPETIDORES worst-cases at the 8px floor).
- [x] On-device: both locales selected via the keyboard path (tab/tb/enter —
      the lower picker rows sit under the modal cancel zone, taps can't reach
      them; documented in HARDWARE_TESTING.md). Home grid captures: **12/12
      tiles zero clipping** for IT and PT; **both persist across reboot**.
- **Exit: ✅ B2 complete 2026-08-04 (commit d10749ce).**

### B3 — SETUP-tile residuals
- [x] FR `Configurer la radio` / ES `CONFIGURACIÓN` / `Configura la radio` exceeded
      the 8px floor; shortened translations: FR `Régler radio`, ES `CONFIGURAR` +
      `Configura radio`, IT `Configura radio`, PT `Ajustar rádio`. Text-fit test
      extended: HomeSetupWarning measured at the 76px floor for all 7 languages.
- [x] On-device (remote-test build, 2026-08-04): FR + ES + IT + PT home grid
      captures via the language picker — **12/12 tiles zero clipping each**;
      ES `CONFIGURAR` and PT `CONFIGURAR` complete and centered (vision + 3×
      pixel zoom verified); FR `CONFIGURATION` fits at the floor with margins.
- **Exit: ✅ B3 complete 2026-08-04 — zero clipped labels across
      EN/DE/FR/ES/NL/IT/PT on hardware (NL from B1, IT/PT from B2, FR/ES here).**

---

## 6. How to Ship — Phase C: Transport Proof (P1)

### C1 — TCP:5000 end-to-end
- [x] WiFi creds received + entered on-device 2026-08-04 (dialog, 8-char password verified on screen).
- [x] Attempted association: fails at −87…−94 dBm — `WL_DISCONNECTED` ("No response from
      access point") / "Connection lost". AP is healthy (Pi joins GrahamIOT at −50 dBm on the
      same desk) → **device placement issue: T-Deck must be closer to the AP** to run C1–C4.
- [ ] From the VM: `python3` socket client → `192.168.1.102:5000` (or the device's
      STA IP) → complete a protocol handshake (login frame, status request).
- [ ] Send a DM from the mesh/companion side and verify the client receives it
      (push-to-all).

### C2 — WS:8765 end-to-end
- [ ] `websocat ws://<device-ip>:8765` (or browser) → verify the companion WS
      protocol exchange; confirm the mirror-free companion framing is intact.

### C3 — Multi-client sync dedup
- [ ] Two clients connected simultaneously → send one message → both receive it
      exactly once (per-client sync dedup verified on the wire).

### C4 — Complete the interop matrix
- [ ] USB/BLE (already validated) + TCP/WS legs: fill the matrix in
      `docs/COMPANION_SUPPORT.md` and record in RELEASE_EVIDENCE.
- **Exit:** all four transports proven against the official companion protocol.

---

## 7. How to Ship — Phase D: Field Polish (P2)

- [ ] **GPS UX** — position comes from the phone/companion; define and implement
      the "no position" state and last-known-position display in chat/map.
- [ ] **Offline map verification** — SD tile storage + markers verified on hardware;
      negative-cache/online tiles remain a v0.6 item.
- [ ] **Notifications depth** — banner actions, unread badges per channel.
- [ ] **Power/lock tuning** — from A3 measurements (timeouts, backlight curve).

---

## 8. How to Ship — Phase E: Release (P2) — the ship instructions

> The goal: **the first stable (non-RC) tag** — `0.1.0` — with evidence. RC tags
> are currently cut every ~2 weeks; a stable needs the gates below satisfied.

### E1 — RELEASE_EVIDENCE gates (docs/RELEASE_EVIDENCE.md)
- [ ] Warning budget: no new warnings over budget in any env build.
- [ ] Interop matrix complete (C4).
- [ ] Soak evidence (A4) + battery measurement (A3) attached.
- [ ] Golden frames: home/settings/chat captures from the release build, vision-verified.

### E2 — Test gates
- [ ] `pio test -e native_test` → **1622+ green** (or new count, 0 failures).
- [ ] `pio test -e native_sanitize` → ASan/UBSan clean.
- [ ] Builds: `pio run -e SigurdOS_TDeck`, `-e SigurdOS_TDeck_debug`,
      `-e SigurdOS_TDeck_remote_test_radio` all clean.

### E3 — Reporter build flow
- [ ] Push the production binary to the `test-builds` orphan branch; share the raw
      URL + MD5 (existing protocol).

### E4 — Web flasher + artifacts
- [ ] Refresh the firmware manifest (`firmware/README.md` / web flasher manifest);
      release publishes `SigurdOS-tdeck-launcher.bin` (byte-identical to
      `firmware-merged.bin`) plus the debug and test builds.

### E5 — Cut the release
- [ ] Tag `0.1.0` on `dev` at the verified commit; release notes with the feature
      list + evidence links; CI `build-release.yml` builds and attaches artifacts.
- [ ] Flash the production build to the T-Deck; boot-verify (serial log to
      "Radio ready", no asserts); final soak start.

### E6 — Post-release
- [ ] Bump `SIGURDOS_VERSION` on `dev`; update FEATURES_OVERVIEW/PROJECT_HISTORY
      with the stable milestone.
- **Exit:** `0.1.0` stable release live with evidence, device running it.

---

## 9. v0.6 Ship-Ready Backlog (from the 2026-08-02 brainstorm)

> Ship after the stable. Each item is scoped with tests + hardware verification
> per the HARDWARE_TESTING protocol. **Top-3 first: backup, canned messages, SOS.**

| # | Item | Concrete scope | Effort | Tests |
| --- | --- | --- | --- | --- |
| 1 | **SD backup bundle** | One-tap export of identity + contacts + channels + prefs to a dated file on SD; restore from the same menu | M | test_backup_bundle (round-trip), HW: backup→factory reset→restore |
| 2 | **Canned messages** | 3–5 editable presets ("73", "QTH?", "Testing 1-2-3") inserted in 2 taps in chat | S | test_canned_messages, HW: insert in DM |
| 3 | **SOS beacon** | Hold combo → SOS + GPS position, repeats every N min until cancelled, buzzer pattern | M | test_sos (rate/timer logic), HW: trigger + cancel |
| 4 | GPX track export | Recorded GPS tracks → `.gpx` on SD | S | test_gpx_writer, HW: export + parse |
| 5 | Airtime/duty-cycle meter | "Airtime today: 0.6% / 1%" in Signal/settings | S | test_duty_cycle (TX accounting) |
| 6 | Mesh time sync | Take time from any heard node when GPS/app unavailable | S | test_time_sync (ordering), HW: two nodes |
| 7 | Reply/quote + forward | Quote on reply; forward received messages to another chat | S | test_chat_actions |
| 8 | In-chat location sharing | "Send my position" as a map-renderable message | S–M | test_location_message |
| 9 | Keyword & contact alerts | Alert on chosen contact/keyword, even on other screens | M | test_alerts, HW: banner on cross-screen hit |
| 10 | Waypoints + nav | Save position as named waypoint; bearing/distance to it or a contact | M | test_waypoints |
| 11 | SD OTA sideload | Copy `firmware.bin` to SD, flash from Settings | M | HW: sideload full update |
| 12 | Pull-forward: draft persistence | Don't lose half-typed messages on navigation | S | test_draft_store |
| 13 | Pull-forward: power profiles | "Field" (low TX, GPS off) vs "Desk" one-tap | M | test_power_profiles, HW: battery |
| 14 | Pull-forward: online map tiles + neg-cache | Maps usable beyond pre-cached SD tiles | M | test_tile_negcache |
| 15 | Pull-forward: guided login errors | Explicit repeater/room login errors instead of timeouts | S | test_login_errors |

---

## 10. Definition of Done — the project's end goal

The stable release is shipped **and**:
1. **No data loss** — #1492 fixed, backup bundle shipped, reflash/OTA persistence proven
2. **All transports proven** — USB/BLE/TCP/WS interop matrix complete
3. **Languages complete** — NL/IT/PT in, zero clipped text, lockstep translation rule
4. **Field-tested** — battery >2 weeks idle, soak clean, GPS UX final
5. **Safety story** — SOS beacon + location sharing shipped
6. **Docs current** — ROADMAP/PROJECT_HISTORY/FEATURES_OVERVIEW/KNOWN_ISSUES updated

---

## 11. Standing Process Rules

- Issue-first: no code without a GitHub issue (CONTRIBUTING.md).
- Tests before merge: full native suite green on every branch; sanitizer env on
  release candidates.
- Hardware verification protocol (docs/HARDWARE_TESTING.md) for every feature;
  screenshots vision-verified, boot/soak logs attached.
- Read ROADMAP before proposing work — declined scope must not be re-proposed.
- Keep the living docs current in the same PR that changes the code.
