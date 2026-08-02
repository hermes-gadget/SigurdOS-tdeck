# Security and authorization fixes

Branch: `fix/audit-security-auth`

## Fixes

### #1466 — Factory reset failure reporting

- Verifies the BLE reset interlock, each owned NVS namespace operation, and
  SPIFFS formatting before rebooting.
- Returns failure and leaves the device running when any destructive step fails.
- Files: `src/hal/factory_reset_policy.h`, `src/mesh/mesh_wrapper.cpp`,
  `test/test_prefs/test_prefs.cpp`.
- Tests: injected NVS failure prevents formatting; injected SPIFFS failure is
  reported after NVS processing.

### #1470 — Local OTA reboot ownership

- Replaces the WebServer worker's direct restart with a main-loop pending-reboot
  handoff.
- The loop waits for worker cleanup, checkpoints mesh state, closes SPIFFS, and
  performs the reboot; manual Settings reboot is blocked during the handoff.
- Files: `src/hal/ota_runtime_policy.h`, `src/hal/wifi_ota.h`,
  `src/hal/wifi_ota.cpp`, `src/ui/screens/screen_settings_system.cpp`,
  `test/test_ota_runtime_policy/test_ota_runtime_policy.cpp`.
- Tests: reboot finalization requires a pending upload and an idle worker.

### #1474 — PIN cancellation from Home

- Cancelling PIN entry explicitly restores the source screen, including Home,
  instead of calling `go_back()` against an empty history stack.
- Delayed success callbacks from a cancelled PIN screen are ignored.
- Files: `src/ui/navigation.cpp`, `src/ui/screens_common.cpp`,
  `test/test_navigation_pin_gate/test_navigation_pin_gate.cpp`.
- Tests: Home cancellation restores Home and does not resume the protected route.

### #1475 — PIN-gated detail routes

- Adds Contact Detail and Repeater Detail to protected routes and gates refreshes
  of an already-open detail screen.
- Preserves detail names and repeater login mode through PIN unlock, and routes
  internal/test-controller detail entry points through the navigation gate.
- Files: `src/ui/navigation.cpp`, `src/ui/screens/screen_contacts.cpp`,
  `src/ui/screens/screen_repeaters.cpp`, `src/test/test_controller.cpp`,
  `test/test_navigation_pin_gate/test_navigation_pin_gate.cpp`.
- Tests: protected parameterized entry and refresh tests for both detail routes.

### #1488 — Repeater logout timer lifetime

- Makes the logout timer root- and generation-owned, cancels it on replacement
  or deletion, and checks the active route before calling `go_back()`.
- Files: `src/ui/generation_owner.h`, `src/ui/screens/screen_contacts.cpp`,
  `src/ui/screens/screen_repeaters.cpp`, `test/test_ui_generation/main.cpp`.
- Tests: deferred actions are rejected for stale timer, route, or generation
  state.

### #1489 — Room message fetch rejection

- Honors `sendRoomMsgFetchRequest()` failure, posts an actionable UI error, and
  keeps the dialog open for correction.
- Confirmation and Chat navigation occur only after the request is accepted.
- Files: `src/ui/room_fetch_policy.h`, `src/ui/screens/screen_contacts.cpp`,
  `test/test_room_fetch_policy/main.cpp`.
- Tests: accepted and rejected request UI decisions.

## Verification

- `pio test -e native_test -v`: **1,569 succeeded, 1 skipped** (1,570 cases).
- `pio run -e SigurdOS_TDeck`: **SUCCESS**.
