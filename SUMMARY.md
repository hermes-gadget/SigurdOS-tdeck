# Audit Concurrency Fixes

Branch: `fix/audit-concurrency`, based on `dev`.

## Fixes

- **#1468 — Diagnostic ring writer:** synchronized all mutable writer state with
  an ESP32 critical section or native mutex, and made diagnostic log records
  commit atomically.
- **#1469 — WiFi ownership:** worker tasks now enqueue coalesced release
  requests; the main loop alone performs coordinator and WiFi-driver releases.
- **#1476 — Map teardown:** lifecycle locking makes the final generation check
  and completion enqueue atomic with deinit generation invalidation and queue
  draining; stale buffers are freed by their owning side.
- **#1478 — GPS disable:** zero effective demand stops `Serial1`; later demand
  reinitializes the GPS UART while one-shot sync/high-rate demand remains active.
- **#1490 — OTA credentials:** AP passwords are cleared at session boundaries,
  wiped after AP shutdown, and exposed only for an active AP-mode OTA session.

## Files changed

- **#1468:** `src/diagnostics/diagnostic_io.cpp`,
  `src/diagnostics/diagnostic_io.h`, `test/test_log/main.cpp`
- **#1469:** `src/hal/wifi_coordinator.cpp`,
  `src/hal/wifi_coordinator.h`, `src/hal/wifi_ota.cpp`,
  `src/hal/github_ota.cpp`, `src/main.cpp`,
  `test/test_wifi_scan/test_wifi_scan.cpp`,
  `test/test_main_loop/test_main_loop.cpp`
- **#1476:** `src/app/map_renderer.cpp`, `src/app/map_renderer.h`,
  `test/test_map_renderer/test_map_renderer.cpp`
- **#1478:** `src/hal/gps.cpp`, `src/hal/gps.h`, `test/mocks/Arduino.h`,
  `test/test_gps/test_gps.cpp`
- **#1490:** `src/hal/wifi_ota.cpp`, `src/hal/wifi_ota.h`,
  `test/test_ota_auth/test_ota_auth.cpp`

## Verification

- `pio test -e native_test -v` — **1567 passed, 1 expected skip**
- `pio run -e SigurdOS_TDeck` — **success**

No device was flashed and no remote branch or pull request was created.
