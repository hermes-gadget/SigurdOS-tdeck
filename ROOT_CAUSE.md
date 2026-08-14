# W2 local-OTA crash root cause

- Issue: [#1557](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/1557)
- Evidence build: `dev` at `4532aec9502d4387f588a85842f21a978d7ea16a`
- Evidence: `/home/ben/.hermes/plans/evidence-w2-ota-crash/{coredump.bin,coredump_elf.bin,td_repro_raw.txt}`

## Conclusion

The preserved task-watchdog abort was caused by a full USB CDC transmit ring,
not by WiFi or OTA code. `loopTask` was synchronously draining diagnostic text
through `HWCDC::write()`. One CDC write could wait for twenty consecutive
100 ms ring-buffer timeouts, and the diagnostic drain could issue several such
writes in one application-loop pass. The cumulative wait exceeded the 10-second
runtime watchdog before Arduino's loop wrapper could feed it again.

The OTA row did contain a second, independent watchdog defect: at the evidence
commit its LVGL callback called `sigurdos::ota::start()` synchronously
(`4532aec9:src/ui/screens/screen_settings_system.cpp:1028`), while `start()` did
the WiFi reset, AP initialization, AP verification, and web-server construction
on `loopTask`. It created the `wifi-ota` task only after all of that work
completed (`4532aec9:src/hal/wifi_ota.cpp:674-691`). The preserved dump did not
reach that code, but leaving it synchronous would retain another path by which
the OTA row could starve the same watchdog.

The fix closes both paths.

## Evidence and crash mechanism

The decoded `loopTask` stack is:

```text
xRingbufferSend(..., xTicksToWait=100)
HWCDC::write(buffer="[stat] t=", size=9)
sigurdos::diagnostics::NonBlockingWriter::drain_locked(byte_budget=256)
sigurdos::diagnostics::drain_diagnostic_output()
loop()
loopTask
```

At the stopped `HWCDC::write()` frame, GDB reported
`consecutive_timeouts=18`, `max_consecutive_timeouts=20`, and
`tx_timeout_ms=100`. The staged CDC implementation retries the ring-buffer send
at `.pio/build/SigurdOS_TDeck_remote_test_radio/hwcdc-backport/cores/esp32/HWCDC.cpp:492-511`.
The application drain checks `availableForWrite()` and then performs a separate
write (`src/diagnostics/diagnostic_io.cpp:127-163`). That check is only a
snapshot: direct serial writers and ring-buffer item overhead can consume the
reported space before the write. A 256-byte drain can then make more than one
blocking driver call.

The runtime watchdog is ten seconds (`src/hal/boot_watchdog.cpp:94-106`). Its
progress record is updated only after the application loop reaches the end, and
Arduino feeds the registered task between complete `loop()` calls
(`src/hal/boot_watchdog.cpp:109-115`). The drain is immediately before that
end-of-loop progress point (`src/main.cpp:563-568`), so a blocked drain prevents
the feed.

Verbose display builds also write a line directly to `Serial` for every LVGL
flush (`src/hal/display.cpp:641-645`). Those writes share the same finite CDC
ring with the queued diagnostic writer and explain the sustained flush traffic
in `td_repro_raw.txt`.

### Why this coredump is not inside OTA startup

The coredump contains no WiFi or OTA worker task. More decisively, its OTA
globals are the never-started state:

```text
active=false
stop_requested=false
worker_exited=true
worker_owns_server=false
using_access_point=false
server=null
session_started_at=0
server_ip=""
last_error=""
companion_transport_parked=false
```

`td_repro_raw.txt` corroborates that state. The last acknowledged remote-test
command is the final `press tab`; flush output continues and the board resets,
but the subsequent `press enter` is never echoed. Therefore this preserved
failure happened before the OTA-row event handler invoked `ota::start()`.

### Why the controls passed, and why the failure looked OTA-specific

CDC saturation is load- and timing-dependent. It requires the host-facing TX
ring to be full and enough 100 ms retry windows to accumulate within one
application-loop pass. The OTA navigation script reached that condition at the
end of its command/render sequence. The Device PIN, WiFi-row, and idle controls
did not cross the same threshold during their capture windows. Their success
rules out those individual handlers as necessary causes, but it does not make
the OTA handler the cause of a dump whose OTA state is still untouched.

The OTA row nevertheless exposed a real design violation. At `4532aec9`,
`startAccessPoint()` performed two fixed 100 ms delays, a 250 ms settle delay,
and synchronous ESP WiFi driver operations
(`4532aec9:src/hal/wifi_ota.cpp:228-288`). The earlier worker change protected
multipart parsing and flash writes, but not startup. That is why this fix also
makes startup worker-owned.

## Fix

1. USB CDC diagnostics are configured with a zero transmit timeout immediately
   after `Serial.begin()` and before watchdog initialization or project logging
   (`src/main.cpp:229-239`, `src/diagnostics/diagnostic_io.cpp:15-20`). With a
   full TX ring, `HWCDC::write()` now returns without a timed wait. The queued
   writer advances its tail only by the byte count actually written and retains
   unwritten data for a later bounded drain
   (`src/diagnostics/diagnostic_io.cpp:162-171`). Best-effort direct debug output
   can be dropped under backpressure; it can no longer stop `loopTask`.

2. `ota::start()` now performs only bounded validation, coordinator lease
   reservation, credential copying, and task creation
   (`src/hal/wifi_ota.cpp:617-733`). `reserveForWorker()` records exclusive WiFi
   ownership without changing hardware mode (`src/hal/wifi_coordinator.cpp:64-69`).
   The worker performs AP/STA setup, web-server construction, request serving,
   and cleanup (`src/hal/wifi_ota.cpp:187-227`,
   `src/hal/wifi_ota.cpp:335-615`). `isActive()` is true during startup so power
   and conflicting system actions remain gated.

3. Local OTA exposes `Idle`, `Starting`, `Ready`, and `Failed` states. The LVGL
   callback creates a responsive "Starting OTA Update" dialog and polls that
   state on an object-owned timer (`src/ui/screens/screen_settings_system.cpp:97-142`,
   `src/ui/screens/screen_settings_system.cpp:1065-1122`). Worker code never
   accesses LVGL objects.

## Verification performed without hardware

The mission prohibited firmware builds, flashing, serial access, and the full
native suite. These allowed targeted tests pass:

- `pio test -e native_test -f test_main_loop -v` — 9/9
- `pio test -e native_test -f test_ota_runtime_policy -v` — 9/9
- `pio test -e native_test -f test_wifi_scan -v` — 17/17
- `pio test -e native_test -f test_log -v` — 7/7
- `pio test -e native_test -f test_ui_contract -v` — 9/9
- `git diff --check`

The tests pin the zero-timeout boot ordering, explicit startup states, complete
worker ownership of hardware startup, coordinator behavior, and diagnostic
queue concurrency.

## Required on-device verification

The orchestrator must validate the hardware-specific parts that native tests
cannot execute:

1. Build and flash the same remote-test/debug configuration used for the
   evidence. Repeat the exact navigation sequence and OTA Enter action at least
   ten times. Every `press enter` must be acknowledged, the dialog must move
   from **Starting OTA Update** to **OTA Update Active**, and no reset or task-WDT
   report may occur.
2. Repeat with the serial reader deliberately paused or throttled while verbose
   display/telemetry logs run for longer than the former 10-second watchdog
   window. UI, input, mesh, and watchdog progress must continue. Lost debug
   lines under deliberate backpressure are acceptable.
3. With no STA connection, confirm `SigurdOS-OTA` advertises, the displayed WPA2
   password works, the displayed IP serves the upload page, authenticated upload
   completes, and the device reboots only after worker cleanup/state checkpoint.
4. With STA connected, confirm the dialog displays the STA IP and no AP password,
   and the upload page remains reachable.
5. During AP startup/session, confirm TCP/WebSocket companion listeners park,
   ports 5000/8765 refuse connections, and persisted transport preferences do
   not change. After Close/expiry/stop, confirm the prior WiFi mode and companion
   listeners recover.
6. Re-run Device PIN, WiFi-row, and idle controls, then inspect reset reason,
   boot-watchdog record, and any new coredump. There must be no new task-WDT
   abort and no loop stack in `HWCDC::write()`.
