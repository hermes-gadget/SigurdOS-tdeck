# RC2 Hardware Validation

This document is the RC2 hardware-validation source of truth. It turns the
roadmap's hardware, interop, test, debug, and release gates into repeatable
checks that can be attached to PRs and release candidates.

## Port Safety

Current bench constraint: only `COM8` is approved for T-Deck validation in this
thread. `COM11` and `COM29` must not be opened or enumerated unless the operator
explicitly changes that rule in a direct instruction.

All scripts in this document require an explicit `--port`; they should not scan
serial devices.

## Build Profiles

| Profile | Environment | RF behavior | RC2 purpose |
| --- | --- | --- | --- |
| Release | `SigurdOS_TDeck` | Normal release policy | Installable firmware size, boot, UI, mesh, OTA, GPS, SD, map |
| Telemetry smoke | `SigurdOS_TDeck_telemetry` | No LoRa radio transmit in remote-test mode | COM8 serial control, UI navigation, structured telemetry, heap/PSRAM, peripheral queries |
| Remote test | `SigurdOS_TDeck_remote_test` | LoRa radio is not initialized; SPI bus is initialized for shared peripherals | Serial UI/input automation without RF |
| Remote test radio | `SigurdOS_TDeck_remote_test_radio` | Full LoRa mesh; can transmit | Explicit RF interop only after approved frequency, region, power, and peer-node plan |
| Companion BLE | `SigurdOS_TDeck_ble_validation` | Full mesh plus BLE validation counters | Official app pairing, reconnect, sync, and command traffic |
| GPS validation | `SigurdOS_TDeck_gps_validation` | GPS-only harness | GPS UART, fix acquisition, time/fix diagnostics, privacy-safe SPIFFS readback |

Do not use a transmitting profile just to prove serial automation. Use telemetry
or non-radio remote test first, then run RF profiles only for a named interop
case.

## Serial Smoke Harness

`scripts/validation/remote_test_smoke.py` drives the remote-test controller over
one explicitly named serial port and stores evidence in `.pio`.

Build and flash the telemetry smoke firmware to the approved T-Deck:

```powershell
pio run -e SigurdOS_TDeck_telemetry
pio run -e SigurdOS_TDeck_telemetry -t upload --upload-port COM8
```

Run the RC2 telemetry smoke:

```powershell
python scripts\validation\remote_test_smoke.py --port COM8 --profile telemetry --forbid-port COM11 --forbid-port COM29
```

Expected artifacts:

| Artifact | Meaning |
| --- | --- |
| `.pio/rc2_hardware_validation/<timestamp>/serial.log` | Raw serial transcript |
| `.pio/rc2_hardware_validation/<timestamp>/summary.json` | Pass/fail summary, command list, and expectation results |

The default telemetry profile verifies:

- Remote-test serial command path is responsive.
- Current screen and heap/PSRAM status can be queried.
- Structured telemetry emits `@heap`, `@lvgl`, `@mesh`, `@screen`, `@radio`,
  `@gps`, and `@nvs` records.
- Visible-widget dumping works for UI regression triage.

The non-telemetry `ui` profile is available for
`SigurdOS_TDeck_remote_test`:

```powershell
pio run -e SigurdOS_TDeck_remote_test
pio run -e SigurdOS_TDeck_remote_test -t upload --upload-port COM8
python scripts\validation\remote_test_smoke.py --port COM8 --profile ui --forbid-port COM11 --forbid-port COM29
```

## Current Hardware Evidence

### 2026-06-06 COM8 Telemetry Smoke

Branch: `codex/rc2-hardware-validation`

Commands:

```powershell
pio run -e SigurdOS_TDeck_telemetry
pio run -e SigurdOS_TDeck_telemetry -t upload --upload-port COM8
python scripts\validation\remote_test_smoke.py --port COM8 --profile telemetry --forbid-port COM11 --forbid-port COM29
```

Result:

| Check | Evidence |
| --- | --- |
| Build | PASS, 00:09:37.913 |
| Size | RAM 108,956/327,680 bytes; flash 1,844,361/6,553,600 bytes; merged image 1,910,368 bytes |
| Upload | PASS on manually specified `COM8`, 00:01:23.490 |
| Serial smoke | PASS, 8/8 checks |
| Artifacts | `.pio/rc2_hardware_validation/2026-06-06-035642/summary.json`, `.pio/rc2_hardware_validation/2026-06-06-035642/serial.log` |

Telemetry observations:

- `@heap`, `@lvgl`, `@mesh`, `@screen`, `@radio`, `@gps`, and `@nvs`
  records were returned by the firmware.
- GPS UART data path was active: `@gps|fix=0|qual=0|sv=0|baud=38400|chars=12105|sent=260|valid=258|csfail=2|sw=1`.
- This bench run validates GPS module data flow into firmware, but not outdoor
  fix acquisition. A sky-view run with `fix=1` and satellites must still be
  attached before GPS is marked complete for RC2.

## RC2 Validation Matrix

| Area | Minimum RC2 evidence | Current status |
| --- | --- | --- |
| Native tests | `pio test -e native_test -v` passes | Green in PR #464: 679 cases, 678 succeeded, 1 skipped |
| Release build | `pio run -e SigurdOS_TDeck` passes with size and warning summary recorded | Needed on current RC2 branch |
| Telemetry smoke | COM8 `SigurdOS_TDeck_telemetry` upload plus `remote_test_smoke.py --profile telemetry` pass | PASS on 2026-06-06 COM8 run |
| Non-radio remote UI | COM8 `SigurdOS_TDeck_remote_test` upload plus `remote_test_smoke.py --profile ui` pass | Needed after telemetry smoke |
| GPS | COM8 SPIFFS/NVS evidence with fixed records and privacy-safe coordinates | PR #464 records 84 fixed records and NVS `boot_count_value=36`; 2026-06-06 telemetry smoke proves GPS UART data path but not fix |
| Companion BLE | COM8 BLE validation boot/advertising plus official app pairing/auth/RX/TX | Boot/advertising previously proven; phone pairing still needed |
| RF interop | Named frequency/profile, peer node identity, TX/RX packet counters, packet log, and transcript | Not started for RC2; do not use `COM11` under current port constraint |
| Repeater/room | Login, status, telemetry, CLI data, fetch, timeout/error mapping with a real server/repeater | Needed |
| OTA | AP OTA and GitHub OTA positive and negative cases: no credentials, wrong credentials, TLS/404/interrupted download | Needed |
| SD/map | SD present/missing boot, tile load, missing tile state, map pan/zoom, cache behavior | Needed |
| Sleep/wake/power | Cold boot, configured boot, unconfigured no-transmit boot, sleep/wake, low-battery policy, GPS enabled/disabled | Needed |
| Soak | Multi-hour mesh/UI/telemetry/GPS/map loop with heartbeat artifacts | Needed |

## RF Interop Gate

Before any transmitting run:

- Confirm the exact regional frequency, bandwidth, spreading factor, coding
  rate, and TX power.
- Confirm whether the run uses `SigurdOS_TDeck_remote_test_radio`,
  `SigurdOS_TDeck_remote_test_radio_testfreq`, or a release/debug profile.
- Confirm the peer device and how RX/TX will be proven.
- Record whether first-boot/debug behavior may transmit an advert.
- Preserve packet logs and telemetry before restoring release firmware.

Under the current bench constraint, do not use the MeshCore radio on `COM11`.

## Pass/Fail Policy

Every RC2 hardware run should record:

- Branch and commit SHA.
- Firmware environment and build size.
- Port used.
- Whether the profile can transmit RF.
- Exact commands run.
- Raw artifacts under `.pio`.
- Human interpretation of pass/fail and residual risk.

Treat missing evidence as incomplete, not as a pass.
