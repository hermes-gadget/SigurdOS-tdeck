# SigurdOS T-Deck End-To-End Audit

Date: 2026-06-04
Refreshed: 2026-06-06 source review against `020195e4581ec7b83147bab09af07e5c902b8ae5`

This audit covers the SigurdOS T-Deck MeshCore firmware at commit `97fb805fbb63fcbae19ed8e199e9f3659b8b331b`, with MeshCore submodule `9a888541efaf57c38dfb886c1c1e4702f371baf1`. The review focused on stability, performance, tests, debug tooling, and feature parity against core MeshCore field workflows.

No physical hardware test was performed during the original audit. Findings are based on source review, documentation review, native test execution, and release build execution. A 2026-06-06 source refresh removed findings that the current code clearly fixes, but did not rerun PlatformIO validation.

## Executive Summary

SigurdOS T-Deck is beyond a prototype. It already contains a functional embedded application with a broad UI, MeshCore/BaseChatMesh integration, channel and direct messaging, contacts, regions, GPS, offline maps, OTA entry points, structured telemetry builds, remote-test support, and a strong native test suite.

The main risk is not missing code volume. The main risk is release hardening. The original release build succeeded, but it used 86.4% of available internal RAM, emitted a large warning stream, and had limited automated coverage for hardware, radio interop, sleep/wake, OTA, storage failure, and long-running operation. Current source fixes some of the earlier policy and wake-mask issues, but build and hardware evidence still need refresh.

The largest feature-parity gap is still companion/client behavior. Connected field workflows emphasize phone/terminal-style operation, map/cache behavior, visible connection state, contact/message detail, and management UX. SigurdOS now has an experimental BLE companion bridge and native protocol coverage, but it still needs official app hardware validation, durable state sync, richer message/contact detail, stronger map behavior, and repeatable interop tests.

## Validation Evidence

Commands run during the audit:

| Check | Result |
| --- | --- |
| `git submodule update --init --recursive` | MeshCore submodule checked out at `9a888541efaf57c38dfb886c1c1e4702f371baf1` |
| `pio --version` | PlatformIO Core 6.1.18 |
| `pio test -e native_test -v` | 397 test cases collected; 396 succeeded; 1 skipped; duration 00:04:31.818 |
| `pio run -e SigurdOS_TDeck` | Success; duration 00:10:29.676 |
| Release build RAM | 283,036 of 327,680 bytes used, 86.4% |
| Release build flash | 1,935,401 of 6,553,600 bytes used, 29.5% |

The firmware build generated a merged image at `.pio/build/SigurdOS_TDeck/firmware-merged.bin`. After image generation, PlatformIO printed a Windows cp1252 `UnicodeEncodeError` in its output reader while still reporting environment success. Treat this as a contributor-tooling issue, not a firmware build failure.

## Architecture Map

| Area | Observed implementation |
| --- | --- |
| Boot orchestration | `src/main.cpp` initializes serial, board, battery, buzzer, SPIFFS, GPS gate, display, mesh, UI, SD, maps, WiFi auto-connect, telemetry, then runs display, OTA, WiFi, GPS, mesh, UI, and telemetry loops |
| Mesh wrapper | `src/mesh/mesh_wrapper.cpp` owns radio setup, identity/channel/contact persistence, BaseChatMesh construction, region APIs, advert timing, send/receive bridges, and shutdown saves |
| Mesh extension | `src/mesh/sigurd_mesh_v2.h` extends BaseChatMesh with discovery, telemetry responses, pending requests, ACK tracking, signal history, region-aware scoped send hooks, login/command/fetch helpers, and UI callbacks |
| UI | `src/ui/*` contains the LVGL home grid, chat, contacts, settings, map screen wiring, onboarding, navigation, and remote-display helper behavior |
| Persistence | NVS preferences in `src/hal/prefs.cpp`; SPIFFS contacts/channels/messages/custom vars; SD map tiles |
| Maps | `src/app/map_renderer.cpp` performs Web Mercator tile rendering, SD PNG decode, PSRAM tile cache, markers, panning, and zooming |
| Diagnostics | `src/diagnostics/*`, `src/test/test_controller.cpp`, display screenshot paths, packet logs, and telemetry collectors provide structured and serial debug surfaces |
| OTA/WiFi | `src/hal/wifi_ota.cpp` provides AP upload OTA and STA helpers; `src/hal/github_ota.cpp` provides GitHub release download/write state |

## Feature Parity Snapshot

| Capability | SigurdOS current state | Parity target expectation | Gap |
| --- | --- | --- | --- |
| Standalone channel/DM chat | Present with persisted messages, timestamps, search, ACK indicator, emoji support, UTF-8 truncation helpers, and a newer shared companion message store | Standalone messaging with clear delivery and history workflows | Mostly present; legacy and companion stores need unification, subtype metadata persistence, and richer detail views |
| Contacts | Discovery, LRU cap, details, removal, path reset, telemetry request, QR display pieces | Sort/filter, favorites, badges, public key/manual add, rich metadata | Needs manual/favorite/filter UX and persistent richer metadata |
| Regions | Region APIs, scoped send hooks, active-scope prefs, UI surfaces, and native tests are present | Region/channel isolation and scoped sends must interop | Needs hardware proof, private-key persistence, and app-driven flood-scope validation |
| Maps | Offline SD tiles, contact markers, panning/zooming, cache | Online tile fetching, negative tile cache, and map source selection | Needs online/cache parity and map performance tests |
| Repeater/room management | Local login, command, fetch, status, telemetry, saved-password, and companion command paths exist | Field-terminal workflows expose management actions, status, and errors | Official app management still needs timeout fixes, CLI-data framing, allowed-frequency semantics, explicit errors, permissions, and interop validation |
| Telemetry | Structured telemetry builds, collectors, packet logs, commands, battery/GPS response | Visible diagnostics and telemetry history | Needs history UI, release-output validation, and doc reconciliation |
| Companion client | Experimental BLE bridge, companion frame dispatcher, offline queue, pushes, time get/set, and native protocol tests exist | Connected clients center on app/terminal workflows | Needs official app hardware validation, WiFi bridge work, reconnect/sync hardening, and repeater-management timeout fixes |
| OTA/update | AP OTA and GitHub OTA entry points exist | Safe update, progress, recovery, and release artifacts | Needs negative tests, rollback docs, manifests |
| Power | Low-battery shutdown, backlight control, sleep paths, GPS settings, and wake-mask helpers | Field terminals need predictable runtime and wake behavior | Needs measured hardware profiles, battery-safe GPS defaults, and wake-source hardware proof |

## Stability Findings

### S1 - Release build has high internal RAM pressure

The release build uses 283,036 bytes of 327,680 bytes of RAM, leaving roughly 44 KB of internal RAM headroom. That is risky for BLE bridge work, HTTP/TLS OTA, LVGL allocations, map decode paths, telemetry rings, and future companion sync.

Recommendation: make RAM a release gate. Track internal RAM, PSRAM, minimum heap, stack high-water marks, and worst-case map/chat/OTA scenarios. Reduce release RAM below 80% before adding large companion features.

### S2 - OTA and WiFi paths can starve the main loop

The main loop is cooperative. GitHub OTA performs HTTP connection and stream/write work inside the loop state machine, and AP/STA WiFi paths contain blocking delays and restart delays. These are acceptable for controlled update flows, but they need explicit UI and watchdog expectations.

Recommendation: add OTA state-machine tests, negative tests, watchdog/heartbeat telemetry during OTA, and user-visible "mesh paused/update active" semantics where appropriate.

### S3 - Persistence has improved but still needs unification

Messages now have a shared bounded binary store at `/companion_msgs` with deduplication, ACK flags, companion-sent flags, path length, and recent-message loading. The legacy chat path still persists `/msgs` through periodic whole-file rewrites, so the system has dual sources of truth and not all subtype, route, or command-reply metadata survives across UI, companion, and reboot paths.

Recommendation: unify `/msgs` and `/companion_msgs` behind one journaled or append-friendly store, version schemas, add migration tests, and persist enough metadata for companion sync, command replies, and message details.

### S4 - Documentation has source-of-truth drift

The code now implements several capabilities that older roadmap and telemetry-plan documents still present as planned or partial. This creates review risk for parallel contributors.

Recommendation: keep roadmap and audit findings separate, then run dedicated follow-up docs PRs for protected or historical docs that need reconciliation.

## Performance Findings

### P1 - UI modules are large and hard to reason about

`src/ui/screens.cpp`, `src/ui/chat_screen.cpp`, `src/mesh/mesh_wrapper.cpp`, and `src/mesh/sigurd_mesh_v2.h` carry a large amount of behavior. This increases regression risk because small UI or mesh changes can touch broad files.

Recommendation: split large screens and mesh helpers after warning cleanup. Keep `mesh_wrapper.h` stable while extracting screen-specific modules and persistence services.

### P2 - Map rendering is powerful but allocation-heavy

The map renderer decodes PNG tiles, converts to RGB565, and caches tiles in PSRAM. It handles SD/offline workflows, but online fetching, negative cache, cache budget settings, and stress tests are missing.

Recommendation: add map performance tests for cache hit/miss, SD missing, high zoom, contact markers, and low PSRAM. Add online tile fetch only after cache budgets and UI responsiveness are measured.

### P3 - Dual message stores can create latency and state drift

The newer shared message store adds stable IDs, deduplication, ACK state, companion-sent state, path length, and bounded recent loading. The legacy chat save path still performs periodic whole-file writes to `/msgs`, which can become expensive as history grows and can drift from the companion-visible `/companion_msgs` store.

Recommendation: collapse chat and companion persistence into one log/journal design with compaction, stable IDs, subtype metadata, deduplication, and migration coverage.

### P4 - Build time and dependency volume are high

The target build took more than ten minutes on this machine and compiled broad dependency sets. That is workable for release builds, but painful for frequent CI and contributor iteration.

Recommendation: keep fast native tests, add targeted build profiles, and make warning categorization concise so contributors can find local regressions quickly.

## Test Findings

### Strengths

- Native tests cover many key areas: battery, build assumptions, channel validation, chat truncation, companion protocol frames, emoji, GPS, home screen, keyboard, layout, map, message store behavior, mesh messaging, mesh wrapper, navigation, pins, preferences, regions, SD card, serial debug gating, terminal, theme, touch, and trackball.
- The remote test controller is unusually valuable for an embedded UI firmware project. It can drive navigation, typing, screenshot capture, widget listing, radio setup, channels, messages, repeaters/rooms, login, fetch, and diagnostics.
- Telemetry and debug builds are already represented in `platformio.ini`.

### Gaps

- No hardware test evidence was produced for this audit.
- No CI evidence was reviewed for physical radio interop, sleep/wake, OTA, GPS, SD map performance, or long-running mesh/UI soak.
- ACK matching and delivery status need deeper tests for multi-contact, repeated ACK, timeout, reboot, and persisted state.
- OTA needs both positive and negative tests.
- Map rendering needs low-resource and missing-resource tests.
- Companion-client behavior now has native protocol coverage, but still lacks official app hardware, reconnect, sync, and repeater-management artifacts.

Recommendation: keep the current native suite as the baseline, then add hardware/remote-test artifacts and interop fixtures before declaring feature parity.

## Debug And Telemetry Findings

### Strengths

- The project has structured telemetry files, heartbeat rings, crash-related helpers, collectors, and remote-test command surfaces.
- Packet logs and UI-accessible diagnostics exist.
- Serial screenshot capture and widget-tree commands are present and useful for automated testing.
- Debug flags are granular across display, mesh, UI, map, and diagnostics.

### Risks

- Release serial-output and telemetry budgets still need fresh build validation now that serial command injection is opt-in.
- Telemetry architecture docs describe planned work that now partly exists in code, making it hard to know what is authoritative.
- The build warning stream is noisy enough that important warnings are easy to miss.
- The PlatformIO output encoding exception on Windows can distract from real build issues.

Recommendation: keep debug surfaces defined by build type: release, debug, telemetry, remote test, and hardware CI. Each should have explicit serial output, command availability, and memory budget expectations.

## Warning Categories Observed

Local or local-adjacent warnings to prioritize:

- `src/ui/theme.h`: inline variables require C++17 while target build is not configured for C++17
- `src/hal/github_ota.cpp`: progress string may truncate
- `src/hal/prefs.cpp`: small key-buffer truncation warnings for WiFi profile keys
- `src/ui/screens.cpp`: multi-character constant/comparison issue and possible status string truncation
- `src/mesh/mesh_wrapper.cpp` and `src/mesh/sigurd_mesh_v2.h`: `memset` on non-trivial `ContactInfo`
- `src/ui/navigation.cpp`, `src/ui/home_screen.cpp`: unused variables

Third-party or submodule warnings to isolate:

- Repeated LVGL `lv_conf.h` possible-failure messages
- MeshCore reorder, unused-variable, and non-trivial memset warnings
- RadioLib `RADIOLIB_GODMODE` and USB CDC debug warnings
- QR library unknown pragmas and unsigned comparison warnings
- Melopero RV3028 ambiguous `requestFrom` overload warning

## Recommended Priority Order

1. Refresh release build and native-test validation, then fix remaining warning hygiene before adding major features.
2. Add hardware sleep/wake and OTA negative tests.
3. Reduce RAM pressure and record runtime heap/PSRAM telemetry in release-like scenarios.
4. Unify chat and companion message persistence into a schema-versioned, power-loss-aware store.
5. Validate companion BLE/WiFi bridge parity and state sync on hardware with the official app.
6. Expand parity UX: contact filters, message details, map cache, room/repeater management, alerts.
7. Add release artifacts, recovery docs, and field validation checklists.

## Audit Conclusion

SigurdOS T-Deck has a credible foundation and a broad feature surface, but it should be treated as a beta firmware until hardware, warning, persistence, companion-sync, and release-operation gaps are closed. The highest-leverage next step is not a single new feature. It is to stabilize the build, make tests span actual hardware behavior, and turn the existing primitives into reliable parity workflows.
