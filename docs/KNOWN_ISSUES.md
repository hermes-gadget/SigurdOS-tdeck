# Known Issues

This document tracks currently open known issues, bugs, and missing features in SigurdOS T-Deck firmware. All historically tracked issues that have been resolved are maintained in the Git history — check merged PRs and commit logs for the full record.

---

## Companion transport and interoperability limits

- **Transport availability:** BLE NUS is enabled in the normal firmware build.
  USB CDC is available in the mutually exclusive
  `SigurdOS_TDeck_companion_usb` build. Hardware-UART companion mode is absent
  because GPS occupies UART pins 43/44, and Wi-Fi TCP companion mode is absent
  because the protocol has no transport-level authentication policy suitable
  for a LAN listener.
- **Raw RX diagnostics:** `PUSH_CODE_LOG_RX_DATA` is intentionally not emitted.
  It would expose raw received RF diagnostics to every paired companion and is
  disabled as a privacy decision.
- **Best-effort pushes:** channel-data, raw, control, trace, status, telemetry,
  login, advert, and path-update pushes are not durable. Channel-data uses the
  bounded volatile page but cannot evict or overtake durable direct text.
- **Device-authored history:** the official protocol has no push code for a
  message composed on the T-Deck. Such messages transmit normally but do not
  appear as authored messages in the official phone app.
- **Protocol version:** the firmware continues to advertise protocol code 12.
  Code 13 will not be advertised until its path-discovery, scope, login, and
  contact behaviours have complete interoperability evidence against a current
  stock companion.
- **BLE validation build:** `SigurdOS_TDeck_ble_validation` currently fails to
  compile because its diagnostic output references the absent observer field
  `auth_timeout_count`. This does not affect the normal BLE-enabled release
  build, but blocks validation-environment memory and device evidence.

The MeshCore submodule remains pinned. It contains local anonymous-contact
fixes that are not a fast-forward match for current upstream; any future update
must reconcile contact allocation/persistence indices and revalidate room
connection keepalives.

---

## Launcher Compatibility

### Supported — bmorcelli/Launcher (v2.7.2+)

SigurdOS can now be installed as a Launcher app. See [`firmware/README.md`](../firmware/README.md) for the full installation guide and caveats.

**What's implemented (Phase 1/4):**
- ✅ Launcher install via SD, WebUI, or direct GitHub URL — use `SigurdOS-tdeck-launcher.bin`
- ✅ Runtime Launcher detection (probes for test-subtype app partition)
- ✅ Self-OTA gated with on-screen explanation when under Launcher
- ✅ Boot-time diagnostics when app-only install loses persistence
- ✅ Self-OTA disabled to prevent flash corruption of co-installed apps
- ✅ SPIFFS partition created for persistence (when using merged image)

**Phase 2a — Detection validated on hardware (2026-07-14 re-check):**
- ✅ Launcher detection tested via custom `test`-subtype partition
- ✅ `sigurdos_is_under_launcher()` returns `true` when Launcher partition exists
- ✅ Boot env diagnostic confirms `"bmorcelli/Launcher"` vs `"standalone"`
- ✅ Launcher installed on T-Deck <!-- TODO: verify — confirm whether end-to-end physical handoff test has run after current firmware and whether the handoff path is stable. -->

**Phase 3 / C6 — Keyboard warm-handoff hardening ✅ merged with #573 follow-up:**
- ✅ Retry loop: keyboard init now retries 3× with 100ms delay instead of single-NACK-abort
- ✅ Mode reset: sends `CMD_MODE_KEY` (0x04) before each probe to reset C3 to known state
- ✅ 2 new native tests covering transient-NACK recovery and exhaustion
- Run `pio test -e native_test -v` and `pio run -e SigurdOS_TDeck` against the current commit; changing test totals are intentionally not recorded as a permanent status claim.

**Remaining gaps (Phase 2b/5/6):**
- 🔜 Phase 2b: Actual Launcher boot handoff (T4/T9) — requires physical SD card or WebUI interaction on T-Deck
- ⏳ Phase 5: Full regression matrix (T1–T14) — standalone rows (T1–T3) pass, Launcher rows (T4–T13) need physical hardware
- ⚠️ LauncherHub catalog status is not yet re-verified against the latest tagged release assets and maintainer process. <!-- TODO: verify — issue #615 was closed as of 2026-06-26; confirm current listing status externally before removing this item. -->

---

## How to Help

Pick any item from the list above and open a PR against the `dev` branch. See [`CONTRIBUTING.md`](./CONTRIBUTING.md) for the full contribution workflow.
