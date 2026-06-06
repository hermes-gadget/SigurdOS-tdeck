# SigurdOS T-Deck Roadmap

This roadmap is the current source of truth for bringing SigurdOS T-Deck to feature parity with core MeshCore field workflows, then beyond them. It is paired with [AUDIT.md](AUDIT.md), which records the 2026-06-04 end-to-end audit evidence and risk register.

## Current Baseline

- Source audit baseline: `020195e4581ec7b83147bab09af07e5c902b8ae5` on `dev`
- MeshCore submodule: `9a888541efaf57c38dfb886c1c1e4702f371baf1`
- Target board: LilyGo T-Deck, ESP32-S3, SX1262, ST7789, GT911, I2C keyboard, trackball
- Validation status: 2026-06-06 current-dev release validation passed
  `pio run -e SigurdOS_TDeck`, `pio run -e SigurdOS_TDeck_ble`, and
  `pio test -e native_test -v`; hardware interop, OTA, SD/map, sleep/wake, and
  soak gates remain before production release. See
  [RC2_HARDWARE_VALIDATION.md](RC2_HARDWARE_VALIDATION.md).
- Audited sources: `src/`, `test/`, `platformio.ini`, MeshCore companion-radio references, and feature docs.

SigurdOS already has a strong base: standalone chat, channel and DM messaging, BaseChatMesh integration, regions, contacts, GPS, offline maps, packet logs, telemetry builds, remote test support, OTA entry points, message persistence, an experimental companion BLE bridge, and a sizeable native test suite. The work below is about hardening that base, closing companion-app parity gaps, and making the firmware reliable enough for field use and repeated development.

## Current Implementation Audit

This audit records what is already present in the codebase so future roadmap work starts from the current implementation instead of re-planning shipped pieces.

| Area | Implemented now | Remaining roadmap work |
| --- | --- | --- |
| Mesh core | `SigurdMeshV2` extends `BaseChatMesh`; DMs, group channels, ACK tracking, advert discovery, trace, ping nearby, telemetry request/answer, client repeat, packet stats, and duty-cycle APIs exist. | Hardware interop matrix, release warning budget, and third-party warning isolation still need to catch up with the source state. |
| Regions | `src/mesh/regions.*` wraps `RegionMap`; channel names can seed regions; active scope persists in prefs; `sendFloodScoped()` stamps transport codes; Settings/region UI surfaces exist. | Physical scoped-flood interop, `$` private key persistence, collision tests, and app-driven flood-scope edge cases need validation. |
| Message persistence | Chat has legacy `/msgs` history and the newer `/companion_msgs` shared store with dedup, ACK flag, companion-sent flag, path length, and recent-message loading. | Unify the stores, add schema migration/power-loss tests, preserve text subtype metadata, and expand capacity/compaction policy. |
| Companion app bridge | `CompanionBridge` implements the stock frame dispatcher for device query, app start, contacts, DMs, channel text/data, channels, time get/set, stats, signing, identity import/export, flood scope, login, status, telemetry, trace, and async pushes. `SigurdOS_TDeck_ble` links the MeshCore ESP32 BLE NUS transport. | Treat BLE as experimental until official app hardware pairing, reconnect, sync, security, RAM, and repeater-management flows are validated. |
| Time | `CMD_GET_DEVICE_TIME` and `CMD_SET_DEVICE_TIME` are implemented through the companion host. GPS parsing includes NMEA checksum validation and currently sets the system RTC on first valid GPS date/time when GPS is active. | Add a clock policy and UI that identifies time source/age. GPS time sync should be user-polled or opportunistic when GPS is already active; it must not keep GPS powered or polling solely to maintain time. |
| GPS | GPS init, baud probing, interval-gated polling, fix data, satellite diagnostics, map/adverts, and settings toggles exist. Defaults still enable GPS and poll every loop. | Reduce default battery cost, add a "Sync time from GPS" action, add fix-acquisition timeout/status UX, and test sleep/wake behavior with GPS disabled/enabled. |
| Repeater/room workflows | Local UI supports repeater/room login, saved passwords, CLI command rows, fetch messages, status/telemetry requests, and command response display. Companion bridge sends login/status/telemetry and CLI-data requests. | Official MeshCore app repeater management needs a focused audit: CLI replies are currently re-stored/framed as plain messages instead of `TXT_TYPE_CLI_DATA`, allowed-repeat-frequency replies are empty, timeout/error mapping is incomplete, and keep-alive/session state needs app-level validation. |
| OTA and release ops | AP upload OTA, GitHub pull OTA, WiFi credential prefs, merged firmware script, and release docs exist. | Negative OTA tests, rollback/recovery docs, checksums, and release evidence still need to become routine. |

## Production-release Remainder

As of the 2026-06-06 current-dev validation refresh, the next production release
is gated by the following work:

1. Finish the release hardening pass: keep native/release/BLE builds green,
   maintain a no-new-local-warning budget, isolate third-party warnings, and
   close the remaining release/debug-policy items.
2. Complete hardware validation that is still missing from the RC2 matrix: RF
   interop, repeater/room workflows, OTA positive and negative cases, SD/map
   behavior, sleep/wake/power, and multi-hour soak.
3. Harden persistence and state sync: unify message stores, version schemas,
   add migration/power-loss tests, and preserve metadata needed for companion
   sync and repeater command replies.
4. Close companion parity: official MeshCore phone-app pairing, RX/TX,
   reconnect, sync, security policy, connection-state UI, and
   repeater-management flows.
5. Finish field UX and performance polish: contact/message detail, region
   interop, map cache behavior, alerts, telemetry history, virtualized long
   lists, and demand-driven GPS/power behavior.
6. Prepare release operations: signed or checksummed artifacts, firmware
   manifests, rollback/recovery docs, issue templates, and a repeatable release
   checklist with attached hardware evidence.

## North Star

Feature parity means a T-Deck can operate as a complete MeshCore field terminal without a phone. It should support daily chat, node discovery, repeater and room workflows, maps, telemetry, alerting, diagnostics, secure setup, update recovery, and predictable battery behavior.

Exceeding parity means SigurdOS should also provide stronger on-device observability, repeatable automated hardware tests, safer update and persistence behavior, and better offline-first workflows than phone or companion-terminal tools can provide.

## Workstreams

| Workstream | Outcome |
| --- | --- |
| Stability | Clean boot, predictable radio setup, safe sleep/wake, safe storage, no release-only debug leaks |
| Performance | Smooth UI under mesh load, bounded RAM, fast map/list rendering, controlled flash writes |
| Tests | Native, remote, hardware, interop, soak, OTA, and visual tests with reproducible artifacts |
| Debug | Structured telemetry, crash capture, packet traces, screenshots, and release-safe diagnostic policy |
| Feature parity | Core MeshCore workflows available directly on the T-Deck |
| Release ops | Firmware artifacts, rollback guidance, documentation, issue workflow, and field checklists |

## Phase 0 - Documentation And Source-Of-Truth Cleanup

Goal: make the repository easy to audit and safe for parallel contributors.

Priority tasks:

- Keep this roadmap as the planning source of truth and use [AUDIT.md](AUDIT.md) for findings.
- Open follow-up issues for stale or superseded feature docs instead of mixing fixes into feature PRs.
- Reconcile [MISSING_FEATURES.md](MISSING_FEATURES.md), [FEATURES_OVERVIEW.md](FEATURES_OVERVIEW.md), [TELEMETRY_ARCHITECTURE.md](TELEMETRY_ARCHITECTURE.md), and [TELEMETRY_EXPANSION_PLAN.md](TELEMETRY_EXPANSION_PLAN.md) with the current implementation in dedicated docs PRs.
- Add a short capability matrix to the README that points to feature docs, tests, hardware docs, and this roadmap.
- Tag issues by workstream: `stability`, `performance`, `test`, `debug`, `mesh-parity`, `ui`, `hardware`, `docs`.
- Keep protected process files, contribution rules, and known-issues updates in their own PRs.

Done when:

- New contributors can find current status without reading old implementation history.
- The docs distinguish implemented, experimental, and planned features.
- Every roadmap task links to an issue before code work starts.

## Phase 1 - Stabilization And Warning Cleanup

Goal: make the current release build boring, repeatable, and warning-accounted.

Priority tasks:

- Fix the ESP32 sleep wake mask warning caused by shifting a 32-bit value for high GPIO numbers. This affects LoRa DIO and deep-sleep wake reliability.
- Remove or gate release serial commands such as screenshot, send, and nav behind an explicit debug or remote-test policy.
- Clean project warnings in `src/ui/theme.h`, `src/hal/github_ota.cpp`, `src/hal/prefs.cpp`, `src/ui/screens.cpp`, `src/ui/navigation.cpp`, and `src/ui/home_screen.cpp`.
- 2026-06-06 release-validation work removed the current local warnings in
  `src/hal/github_ota.cpp`, the release BLE validation stub, and the companion
  DM conversation-label path. The remaining warning work should keep using the
  release build as the source of truth.
- Make LVGL config inclusion explicit so the build no longer prints repeated `lv_conf.h` possible-failure messages.
- Resolve `MAX_TEXT_LEN` redefinition between build flags and `BaseChatMesh`.
- Separate upstream MeshCore warnings from local warnings in CI logs so local regressions are visible.
- Investigate the PlatformIO Windows cp1252 output exception printed during merged firmware generation, even though the build exits successfully.
- Add a build-warning budget. Start with "no new local warnings", then move to "no local warnings".
- Confirm release radio flags are intentional, especially `RADIOLIB_GODMODE`, USB CDC debug warnings, and static/excluded module selections.

Done when:

- Release firmware builds with no untriaged local warnings.
- All remaining third-party warnings are documented or isolated.
- Sleep/wake paths are tested on hardware.
- Release builds do not expose debug-only serial commands by accident.

## Phase 2 - Test And CI Expansion

Goal: turn current native coverage into a complete firmware validation system.

Priority tasks:

- Keep the native suite green and add coverage for warning-prone logic: regions, ACK matching, persistence migration, OTA state transitions, map tile cache, telemetry command parsing, and storage failures.
- Add smoke tests for `SigurdOS_TDeck_telemetry`, `SigurdOS_TDeck_remote_test`, and `SigurdOS_TDeck_remote_test_radio` build environments.
- Add a hardware boot matrix: cold boot, configured boot, unconfigured no-transmit boot, SD present/missing, GPS enabled/disabled, low battery, sleep/wake, first-boot onboarding.
- Add interop tests with at least one reference MeshCore node, one room server/repeater path, and one terminal-client or official-client scenario.
- Add UI screenshot or widget-tree regression tests for home, chat, contacts, settings, map, telemetry, OTA, and onboarding.
- Add OTA negative tests: no credentials, wrong credentials, TLS failure, 404, interrupted download, write failure, rollback/reboot.
- Add flash-wear and persistence tests for chat history, contacts, channels, regions, WiFi profiles, and key import/export.
- Add soak tests that run mesh loop, UI loop, telemetry, GPS, and map activity for hours with heartbeat artifacts.

Done when:

- CI validates docs-only, native, release build, and telemetry/remote-test build profiles.
- Hardware test results are attached to release PRs.
- Interop failures can be reproduced from documented commands and fixtures.

## Phase 3 - Persistence And State Sync Foundation

Goal: make message, contact, channel, region, and clock state durable enough for companion sync and field use.

Priority tasks:

- Unify the legacy `/msgs` chat history and the newer `/companion_msgs` shared store into one append-friendly store that supports compaction and power-loss recovery.
- Persist message metadata beyond sender/text/timestamp/self: ACK state, delivery attempts, route/path hints, RSSI/SNR, channel/DM identity, unread state, stable message IDs, companion-sent state, path length, and text subtype (`TXT_TYPE_PLAIN`, `TXT_TYPE_CLI_DATA`, signed text).
- Version every persistent schema and add migration tests for older SPIFFS/NVS data.
- Harden contact persistence so path metadata, shared secrets, permissions, favorite/pinned state, manual contacts, app-imported contacts, and repeater session hints survive reboot.
- Add import/export flows for identity, contacts, channels, regions, WiFi profiles, and clock/source diagnostics.
- Track storage budgets for SPIFFS, SD, NVS, and PSRAM-backed caches.
- Add deduplication for repeated packets and repeated companion-sync events.
- Add a unified clock policy covering manual time, MeshCore RTC, companion-app `CMD_SET_DEVICE_TIME`, and GPS time.
- Add a user action for "Sync time from GPS" and allow automatic GPS time sync only when GPS is already active for another user-visible workflow, such as map/location/adverts. Do not keep GPS powered or polling solely to maintain the clock.
- Surface the active time source, last-sync age, and confidence in Settings and diagnostics.

Done when:

- A power cut during message, preference, or clock-source writes does not corrupt the active profile.
- A companion client can sync without losing local-only history or misclassifying command replies.
- The official app can set device time and the T-Deck records that the app was the source.
- GPS time can be acquired on demand without creating a background battery drain.
- State migrations are tested before schema changes ship.

## Phase 4 - Companion Transport Parity

Goal: close the largest connected-client feature gap: phone, app, and external-terminal workflows.

Priority tasks:

- Treat the existing `CompanionBridge` and `SigurdOS_TDeck_ble` build as experimental until they pass official MeshCore app hardware validation.
- Complete official app pairing, reconnect, message sync, channel sync, contact sync, app-start, and app-shutdown tests over BLE.
- Land the ESP32 MeshCore BLE cached-bond reconnect fix in the actual
  `lib/meshcore` transport source used by this firmware, then rerun the host
  USB BLE command/reconnect smoke from a clean checkout.
- Add a WiFi bridge or WebUI mode for local companion access where BLE is insufficient.
- Define pairing, device PIN, key exposure, private-key export/import, and local-network security policy.
- Support two-way sync for contacts, channels, DMs, channel messages, node config, telemetry history, time source, and map state.
- Keep `CMD_GET_DEVICE_TIME` and `CMD_SET_DEVICE_TIME` compatible with stock companion firmware so the official app can supply time whenever connected.
- Add connection-state UI: BLE, WiFi, companion connected, sync pending, sync failed, last app time sync, and last GPS time sync.
- Add offline queueing so outbound companion messages survive disconnects, while preserving the T-Deck local message store.
- Audit and fix official MeshCore app repeater management timeouts. Cover `CMD_SEND_LOGIN`, `PUSH_CODE_LOGIN_*`, `CMD_SEND_STATUS_REQ`, `PUSH_CODE_STATUS_RESPONSE`, `CMD_GET_ALLOWED_REPEAT_FREQ`, `RESP_ALLOWED_REPEAT_FREQ`, CLI-data sends through `CMD_SEND_TXT_MSG`, and repeater keep-alive/session state.
- Preserve `TXT_TYPE_CLI_DATA` from `onCommandDataRecv()` through persistence and `RESP_CODE_CONTACT_MSG_RECV_V3`; do not reframe repeater command replies as plain chat messages for the app.
- Return accurate allowed repeat-frequency ranges, or document and test the empty response semantics if SigurdOS intentionally does not gate client repeat by frequency.
- Add a protocol compatibility test plan against official MeshCore clients, terminal-client behavior, and golden frames captured from stock companion firmware.

Done when:

- A user can pair a companion app or terminal, sync current state, send/receive messages, set device time, and recover from reconnects.
- Security-sensitive material is never exposed without a deliberate user action.
- Companion sync survives reboots and transport changes.
- Official app repeater-management options complete or fail with explicit errors instead of timing out.

## Phase 5 - Mesh Feature Parity And Polish

Goal: match and improve the core field workflows users expect from a standalone MeshCore terminal.

Priority tasks:

- Verify region support over real hardware and interop nodes, including scoped sends, active region switching, `$` private region keys, and collision behavior.
- Add terminal-style contact sorting, filtering, favorites, manual contact add, and richer contact badges.
- Expand message detail views with path, hop count, RSSI/SNR, ACK status, repeaters, route hints, and packet metadata.
- Add path hash mode and route-reset workflows where compatible with MeshCore behavior.
- Finish local and app-driven room server/repeater management: login status, permissions, CLI-data command responses, fetch message history, status/telemetry pushes, allowed repeat-frequency reporting, explicit errors, retries/timeouts, and keep-alive behavior.
- Add alerts and popups for new DMs, mentions, battery, GPS fix/loss, companion connect/disconnect, OTA, and storage failures.
- Add telemetry history views for battery/GPS/sensor data and remote node snapshots.
- Add QR/import/export flows for contacts, channels, regions, and identity backup.

Done when:

- The T-Deck can perform the main field workflows without a separate device.
- Feature docs include screenshots or command examples for each workflow.
- Interop results are recorded for each MeshCore packet class the UI exposes.

## Phase 6 - UI, Performance, And Power

Goal: make the device feel responsive and conserve battery during real field use.

Priority tasks:

- Decompose large UI files, especially `src/ui/screens.cpp` and `src/ui/chat_screen.cpp`, into focused screen modules with shared helpers.
- Virtualize long lists for messages, contacts, packet logs, telemetry history, and map markers.
- Reduce release RAM pressure below 80% before adding large BLE or map features.
- Profile LVGL buffer use, PSRAM fallback behavior, map tile decode allocations, and worst-case chat rendering.
- Add online tile fetch and negative tile cache for map parity, while preserving offline SD-first behavior.
- Add map tile prefetch, cache budget settings, and graceful no-SD/no-network states.
- Improve autolock, sleep, wake sources, and backlight behavior around radio receive, trackball, keyboard, touch, GPS, and companion BLE.
- Make GPS power and polling demand-driven: choose battery-safe defaults, avoid every-loop polling by default, and ensure GPS time sync cannot keep the receiver active on its own.
- Add battery and duty-cycle status surfaces that are visible without opening deep diagnostics.
- Add keyboard editor polish: shortcuts, selection, clipboard or draft persistence, and faster correction flows.

Done when:

- UI remains responsive while receiving mesh packets, rendering maps, and saving state.
- Battery behavior is predictable and documented.
- Large data views do not cause frame spikes or heap fragmentation.

## Phase 7 - Release And Field Operations

Goal: make releases installable, recoverable, and supportable.

Priority tasks:

- Publish merged firmware artifacts with checksums and board/region notes.
- Add a release checklist covering native tests, release build, telemetry build, hardware smoke, radio interop, OTA, SD/map, GPS, and sleep/wake.
- Add recovery docs for factory reset, identity backup/restore, failed OTA, bad radio config, and unconfigured no-transmit boot.
- Add version reporting that includes firmware version, git SHA, MeshCore submodule SHA, build environment, and partition layout.
- Add firmware-manifest support for GitHub OTA and future Web Flasher flows.
- Add issue templates for bugs, hardware test reports, interop failures, and feature parity requests.

Done when:

- A non-developer can install, update, recover, and report useful diagnostics.
- Maintainers can compare field reports against exact firmware artifacts.
- Release PRs carry consistent validation evidence.

## Definition Of Parity

| Capability | Required parity behavior | Exceeds parity when |
| --- | --- | --- |
| Standalone chat | Channel and DM messaging, timestamps, ACK status, unread state | Search, details, delivery history, resilient drafts |
| Contacts | Discovery, manual add, details, route/path reset | Favorites, filters, badges, QR/import/export |
| Maps | Own position, contacts, pan/zoom, offline tiles | Online tile fetch, negative cache, prefetch, route overlays |
| Repeater/room workflows | Login, command, fetch, status, errors, and official-app management responses | Guided workflows, permission display, timeout recovery hints, and command transcript history |
| Companion access | BLE or WiFi client sync, messaging, and app-supplied time | Two-way offline queue, transport switching, sync diagnostics, and source-aware clock reconciliation |
| Diagnostics | Packet log, telemetry, screenshots, heap/status | Crash ring, heartbeat history, reproducible hardware artifacts |
| Power | Backlight, sleep, low-battery shutdown, battery-safe GPS polling | Wake-source tuning, demand-driven GPS, power profiles, measured runtime |
| Releases | Build artifacts and install docs | Rollback/recovery flows and field-report automation |

## PR Validation Checklist

Every code PR should state which of these were run:

- `pio test -e native_test -v`
- `pio run -e SigurdOS_TDeck`
- Relevant telemetry or remote-test build
- Hardware boot test, if applicable
- Physical radio test or documented reason it was not run
- Companion/client interop test, if applicable
- Screenshot/widget-tree evidence for UI work
- Persistence migration test for storage changes

Docs-only PRs should still run `git diff --check` and should avoid changing protected process docs unless the PR is specifically scoped for that.
