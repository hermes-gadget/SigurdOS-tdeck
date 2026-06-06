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

The telemetry profile includes `query build`. A passing release-candidate run
must capture the `@build` record with firmware version, git SHA, dirty flag,
MeshCore SHA, PlatformIO environment, partition table, board, and MCU.

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

### 2026-06-06 COM8 Build Identity Telemetry Smoke

Branch and commit: `codex/rc2-build-identity` at `9543e88a0996`.

Commands:

```powershell
pio test -e native_test -f test_build_info -f test_telemetry_protocol -v
pio run -e SigurdOS_TDeck_telemetry -t upload --upload-port COM8
python scripts\validation\remote_test_smoke.py --port COM8 --profile telemetry --forbid-port COM11 --forbid-port COM29 --startup-timeout 8 --command-timeout 10
```

Result:

| Check | Evidence |
| --- | --- |
| Native focused tests | PASS, 8/8 cases across `test_build_info` and `test_telemetry_protocol` |
| Upload/build | PASS on manually specified `COM8`, 00:15:50.059; all esptool flash hashes verified |
| Size | RAM 110,228/327,680 bytes; flash 1,870,317/6,553,600 bytes; merged image 1,936,320 bytes |
| Serial smoke | PASS, 9/9 telemetry checks including `query build` |
| Artifacts | `.pio/rc2_hardware_validation/2026-06-06-173033/summary.json`, `.pio/rc2_hardware_validation/2026-06-06-173033/serial.log` |

Build identity evidence:

```text
@build|fw=beta-0.1.39|git=9543e88a0996|dirty=0|mcore=9a888541efaf|env=SigurdOS_TDeck_telemetry|part=default_16MB.csv|board=t-deck|mcu=esp32s3
```

Telemetry observations:

- `@heap`, `@lvgl`, `@mesh`, `@screen`, `@radio`, `@gps`, and `@nvs`
  records were returned by the firmware.
- GPS UART data path was active during the smoke run:
  `@gps|fix=0|qual=0|sv=0|baud=38400|chars=14967|sent=444|valid=442|csfail=2|sw=1`.
- The passing smoke used only `COM8`; `COM11` and `COM29` were explicitly
  forbidden by the harness.

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

### 2026-06-06 COM8 Non-radio UI Smoke

Branch and commit: `codex/rc2-ui-hardware-validation` at `ab89c7e`

Commands:

```powershell
pio run -e SigurdOS_TDeck_remote_test
pio run -e SigurdOS_TDeck_remote_test -t upload --upload-port COM8
python scripts\validation\remote_test_smoke.py --port COM8 --profile ui --forbid-port COM11 --forbid-port COM29
```

Result:

| Check | Evidence |
| --- | --- |
| Build | PASS, 00:06:35.669 |
| Size | RAM 109,420/327,680 bytes; flash 1,838,309/6,553,600 bytes; merged image 1,904,256 bytes |
| Upload | PASS on manually specified `COM8`, 00:01:12.891; all esptool flash hashes verified |
| Serial smoke | PASS, 10/10 checks |
| Artifacts | `.pio/rc2_hardware_validation/2026-06-06-105938/summary.json`, `.pio/rc2_hardware_validation/2026-06-06-105938/serial.log` |

UI observations:

- `SigurdOS_TDeck_remote_test` ran without `SIGURDOS_REMOTE_TEST_RADIO`, so this
  evidence covers serial UI/input automation without LoRa radio initialization
  or RF transmit.
- The script validated readable screen state, heap/PSRAM status, home to chat
  navigation, simulated `#general` channel message injection, visible-widget
  dumping, settings and signal navigation, backlight query, and return home.
- Build output still carries the known `LORA_FREQ`, `LORA_BW`, and `LORA_SF`
  macro redefinition warnings, plus upstream library warnings. These remain part
  of the warning-cleanup roadmap and did not block this hardware smoke.

### 2026-06-06 Current-dev Release And BLE Build

Branch: `codex/rc2-release-build-validation`, based on `origin/dev` at
`58e5fc5` (`fix: OTA branch/prerelease buttons update inline instead of
rebuilding screen`).

Port safety: no serial port or BLE hardware was opened for this validation
slice. This pass did not touch `COM8`, `COM11`, or `COM29`.

Commands:

```powershell
pio run -e SigurdOS_TDeck
pio run -e SigurdOS_TDeck_ble
pio test -e native_test -v
```

Result:

| Check | Evidence |
| --- | --- |
| Release build | PASS, 00:01:32.604 after local warning cleanup |
| Release size | RAM 110,812/327,680 bytes; flash 1,973,701/6,553,600 bytes; merged image 2,039,648 bytes; webflasher firmware 1,974,112 bytes |
| BLE build | PASS, 00:09:14.596 |
| BLE size | RAM 133,240/327,680 bytes; flash 2,540,825/6,553,600 bytes; merged image 2,606,784 bytes; webflasher firmware 2,541,248 bytes |
| Native tests | PASS, 683 cases collected; 682 succeeded; 1 skipped; duration 00:08:20.499 |

Warning observations:

- Local release warnings fixed in this branch: unused GitHub OTA JSON helper and
  locals, release-only unused BLE validation log stub, and companion DM
  conversation-label truncation.
- Remaining warning debt is local-adjacent or upstream/third-party and remains
  in the roadmap: qrcode warnings, RadioLib policy warnings, MeshCore
  reorder/memset/sensor warnings, Melopero RTC `requestFrom` ambiguity, LVGL
  config messages, and existing RF macro redefinition warnings in some profiles.

## RC2 Validation Matrix

| Area | Minimum RC2 evidence | Current status |
| --- | --- | --- |
| Native tests | `pio test -e native_test -v` passes | PASS on 2026-06-06 current-dev run: 683 cases, 682 succeeded, 1 skipped |
| Release build | `pio run -e SigurdOS_TDeck` passes with size and warning summary recorded | PASS on 2026-06-06 current-dev run; local release warnings cleaned in this branch; third-party/upstream warning debt remains |
| Telemetry smoke | COM8 `SigurdOS_TDeck_telemetry` upload plus `remote_test_smoke.py --profile telemetry` pass, including `@build` identity | PASS on 2026-06-06 COM8 build-identity run: clean `git=9543e88a0996`, `dirty=0`, `env=SigurdOS_TDeck_telemetry` |
| Non-radio remote UI | COM8 `SigurdOS_TDeck_remote_test` upload plus `remote_test_smoke.py --profile ui` pass | PASS on 2026-06-06 COM8 run |
| GPS | COM8 SPIFFS/NVS evidence with fixed records and privacy-safe coordinates | PASS for hardware lock in PR #464: 84 fixed records, first fix `fix=1`, `qual=1`, `sv=7`, `ft=3`, `rmc=A`, `loc=1`; production power/time-sync UX remains |
| Companion BLE | COM8 BLE validation boot/advertising plus official app pairing/auth/RX/TX | Local COM8 boot/advertising and USB BLE pair-only authenticated pairing PASS in PR #466; host USB BLE command RX/TX and one reconnect sync sequence validated locally with an unmerged MeshCore transport patch; clean-checkout firmware still needs that `lib/meshcore` fix plus official phone-app RX/TX, reconnect, and sync |
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
