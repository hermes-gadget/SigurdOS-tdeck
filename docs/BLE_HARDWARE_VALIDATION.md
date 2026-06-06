# T-Deck Companion BLE Hardware Validation

This document records the hardware validation path for the companion BLE
firmware used by the MeshCore phone app. It is intended for PR evidence and
field bring-up, not for normal release operation.

## Validation Firmware

Use the BLE validation build when validating ESP32-S3 BLE startup, advertising,
pairing callbacks, app command traffic, and companion bridge responses:

```powershell
$env:SIGURDOS_UPLOAD_PORT = "<approved-tdeck-port>"
pio run -e SigurdOS_TDeck_ble_validation
pio run -e SigurdOS_TDeck_ble_validation -t upload --upload-port $env:SIGURDOS_UPLOAD_PORT
```

Use the serial port assigned to the T-Deck in the current validation setup. The
COM8 port referenced in evidence sections below is this project's local bench
device, not a requirement for other contributors or test hosts.

The validation build extends `SigurdOS_TDeck_ble` with
`SIGURDOS_COMPANION_BLE_VALIDATION=1`. It uses the same MeshCore
`SerialBLEInterface` transport and the same `CompanionBridge` command handler
as the BLE release image, but wraps the serial interface with counters and
persists privacy-safe records to `/ble_hw.txt` in SPIFFS.

The log intentionally does not print the BLE PIN.

Example record:

```text
[ble-validation] log-start
@ble_hw|ms=10230|begun=1|en=1|conn=0|adv=1|authok=0|authfail=0|connect=0|disconnect=0|mtu=0|rxw=0|rxd=0|rx=0|tx=0|txd=0|lrx=0|ltx=0
```

Fields:

| Field | Meaning |
| --- | --- |
| `ms` | Device uptime in milliseconds |
| `begun` | BLE transport `begin()` completed |
| `en` | Companion BLE transport is enabled |
| `conn` | Companion BLE transport reports an authenticated connection |
| `adv` | Firmware expects the transport to be advertising because it is enabled and not connected |
| `authok` | Successful BLE authentication callbacks |
| `authfail` | Failed BLE authentication callbacks |
| `connect` | BLE connect callbacks |
| `disconnect` | BLE disconnect callbacks |
| `mtu` | Last peer MTU reported by the BLE stack |
| `rxw` | BLE characteristic write callbacks observed |
| `rxd` | Oversize BLE writes rejected by the transport |
| `rx` | Frames drained by the companion bridge from the BLE RX queue |
| `tx` | Frames queued for BLE notify by the companion bridge |
| `txd` | Companion bridge writes rejected by the BLE transport |
| `lrx` | Last received companion command code |
| `ltx` | Last transmitted companion response or push code |

## SPIFFS Readback

If app-side serial is not visible, retrieve the validation evidence through the
bootloader:

```powershell
New-Item -ItemType Directory -Force -Path .pio\ble_validation_readback | Out-Null
python -m esptool --chip esp32s3 --port $env:SIGURDOS_UPLOAD_PORT --baud 921600 read-flash 0xc90000 0x360000 .pio\ble_validation_readback\spiffs.bin
& "$env:USERPROFILE\.platformio\packages\tool-mkspiffs\mkspiffs_espressif32_arduino.exe" -b 4096 -p 256 -s 0x360000 -u .pio/ble_validation_readback/unpacked .pio/ble_validation_readback/spiffs.bin
Get-Content .pio\ble_validation_readback\unpacked\ble_hw.txt
```

## Host USB BLE Smoke

When the validation host has a usable BLE radio, it can act as a local bench
client against the same validation firmware. Do not rely on the default scan
when other MeshCore radios are nearby; use the target's advertised name prefix
or BLE address for the current bench:

```powershell
python scripts\validation\companion_ble_smoke.py --name-prefix "<bench-device-prefix>" --scan-timeout 20
```

The script scans for the MeshCore Nordic UART service
`6E400001-B5A3-F393-E0A9-E50E24DCCA9E` or a `MeshCore-` device name, connects
with encryption/pairing when requested, subscribes to the firmware TX
characteristic, and writes companion command frames to the firmware RX
characteristic.

If multiple MeshCore candidates are visible and no unique name or address is
provided, the script fails closed and prints the candidates instead of choosing
an arbitrary radio. Use `--allow-ambiguous` only for private diagnostics where
the first sorted candidate is known to be correct.

On Windows hosts, the MeshCore PIN flow may require a PIN-capable WinRT pairing
ceremony before GATT writes are accepted. Keep the PIN out of command history
and artifacts by using an environment variable:

```powershell
$env:SIGURDOS_BLE_PIN = "<pin-shown-on-tdeck>"
python scripts\validation\companion_ble_smoke.py --name-prefix "<bench-device-prefix>" --winrt-pin-env SIGURDOS_BLE_PIN --winrt-unpair-first --pair-only --scan-timeout 20
Remove-Item Env:\SIGURDOS_BLE_PIN
```

The pair-only mode proves advertising, host discovery, connect, MTU exchange,
PIN pairing, firmware authentication callback, and disconnect/advertising
recovery. It intentionally does not prove command RX/TX.

The command smoke covers:

- BLE advertising discovery.
- Encrypted GATT connect/pairing through the host Bluetooth stack.
- `CMD_DEVICE_QUERY`.
- `CMD_APP_START`.
- `CMD_GET_CONTACTS`.
- `CMD_GET_DEVICE_TIME`.
- `CMD_GET_BATT_AND_STORAGE`.
- `CMD_GET_STATS`.
- `CMD_SYNC_NEXT_MESSAGE`, accepting either `NO_MORE_MESSAGES` or queued
  message/data frames.

For reconnect validation, run the command smoke with `--reconnects <count>`.
The first session performs the full command smoke above. Each reconnect session
opens a fresh BLE connection, subscribes to TX notifications, runs
`CMD_DEVICE_QUERY`, `CMD_APP_START`, and `CMD_SYNC_NEXT_MESSAGE`, then closes
the connection again:

```powershell
python scripts\validation\companion_ble_smoke.py --name-prefix "<bench-device-prefix>" --scan-timeout 20 --reconnects 2
```

Use the same `--winrt-pin-env` setup shown above when the Windows host requires
an authenticated bond before command traffic. The reconnect smoke validates
transport recovery and companion sync polling, not full official phone-app
behavior.

The script writes privacy-safe artifacts under
`.pio/ble_companion_validation/<timestamp>/summary.json`. The artifact records
per-session command/response codes, frame lengths, reconnect pass/fail state,
and target metadata needed for PR evidence. It does not record the BLE PIN,
WiFi credentials, message text, exact location, or private keys.

Do not publish artifacts generated with `--include-frame-hex`; raw frames can
contain private contact, message, or channel payloads.

The USB BLE smoke is not a substitute for official phone app validation.
Pair-only mode is a repeatable bench proof that the firmware advertises and
pairs through a real BLE controller. Command-smoke mode additionally proves
encrypted GATT writes and companion protocol responses when the host BLE stack
supports the full secured characteristic flow.

## Pass Criteria

A complete companion BLE hardware pass requires all of the following:

- Firmware uploads to the T-Deck over the approved hardware port for the
  current bench.
- The SPIFFS readback contains `/ble_hw.txt` beginning with
  `[ble-validation] log-start`.
- The first records show `begun=1`, `en=1`, and `adv=1` when BLE is enabled.
- A real phone running the official MeshCore app pairs using the displayed PIN.
- Pairing produces `connect>0`, `authok>0`, `conn=1`, and `authfail=0`.
- The app handshake produces `rx>0` and `tx>0`; `lrx` should include
  `22` (`CMD_DEVICE_QUERY`) and `1` (`CMD_APP_START`) during connect.
- Contact/channel sync and message send paths produce companion responses
  without increasing `txd`.
- App disconnect increments `disconnect` and later records return to
  `conn=0`, `adv=1`.
- Published logs redact the PIN and any exact location values.

Host USB BLE bench evidence may be added when available:

- Pair-only mode passes against the same flashed firmware when validating host
  discovery and authenticated pairing.
- Command-smoke mode passes against the same flashed firmware when validating
  companion RX/TX responses through the host BLE stack.
- Reconnect command-smoke mode passes with `--reconnects` when validating
  repeated host reconnect, app-start, and sync-poll recovery.
- `/ble_hw.txt` shows matching `connect` and `authok` activity for pair-only
  validation, and matching `rx` and `tx` activity for command-smoke validation.
- The report identifies the local host radio used for the bench run without
  implying that the same adapter or port is required elsewhere.

## 2026-06-05 Historical COM8 Scope

Port safety constraint: only `COM8` may be opened for this validation in the
current bench setup. `COM11` and `COM29` must not be opened or enumerated.

This earlier run predated discovery of the host USB BLE radio, so it proved BLE
boot and advertising state only. Later runs on this branch add local USB BLE
host evidence.

## 2026-06-06 Local USB BLE Host Inventory

The local Windows validation host for this PR exposes a USB Bluetooth
controller through the Microsoft Bluetooth stack:

- Device: `Generic Bluetooth Radio`.
- Manufacturer: `Cambridge Silicon Radio Ltd.`
- Hardware IDs: `USB\VID_0A12&PID_0001&REV_8891`,
  `USB\VID_0A12&PID_0001`.
- Driver description: `Generic Bluetooth Radio`.
- Driver version: `10.0.19041.5848`.
- Bluetooth support service: `bthserv`, running.

The local host Bluetooth inventory also shows the bench T-Deck as a
`MeshCore-` BLE device with the MeshCore companion UART service present. This is
sufficient for this PR's host USB BLE companion smoke test without opening any
disallowed local serial ports.

This inventory is local bench context only. Other contributors can validate with
their own T-Deck serial port, phone, and/or host BLE adapter.

## 2026-06-06 Local COM8 USB BLE Pairing Run

Validated from branch `codex/companion-ble-validation` on the local T-Deck
attached to `COM8`. The device advertised as `MeshCore-SigurdOS T-Deck`.

Build and unit-test evidence for this branch:

- `pio test -e native_test -f test_companion_protocol`: passed, 53/53.
- `pio run -e SigurdOS_TDeck`: passed; RAM 33.7%, flash 30.0%.
- `pio run -e SigurdOS_TDeck_ble`: passed; RAM 40.6%, flash 38.7%.
- `pio run -e SigurdOS_TDeck_ble_validation`: passed; RAM 40.6%, flash
  38.7%.

Hardware upload evidence:

- Uploaded `SigurdOS_TDeck_ble_validation` to the T-Deck on local `COM8`.
- esptool identified the board as ESP32-S3 rev v0.2; every uploaded range
  reported `Hash of data verified`.
- No disallowed local serial ports were opened.

Host BLE evidence:

- A BLE scan through the local USB adapter found multiple MeshCore UART-service
  radios in range, including the local `MeshCore-SigurdOS T-Deck`. The smoke
  script now requires `--address` or a narrow `--name-prefix` when more than
  one candidate is visible.
- Pair-only validation ran with the PIN supplied from an environment variable:

```powershell
python scripts\validation\companion_ble_smoke.py --name-prefix "MeshCore-SigurdOS" --winrt-pin-env SIGURDOS_LOCAL_BLE_PIN --winrt-unpair-first --pair-only --scan-timeout 20
```

- The script artifact
  `.pio/ble_companion_validation/2026-06-06-055748/summary.json` reported
  `passed=true`, target `MeshCore-SigurdOS T-Deck`, WinRT `pair_status=PAIRED`,
  and no raw frame or PIN fields.
- Windows reported `protection=NONE` for the pairing result even though the
  firmware required `ESP_LE_AUTH_REQ_SC_MITM_BOND`; the firmware callback log is
  the authoritative hardware evidence for authentication.

Firmware-side COM8 evidence during the same pair-only run:

```text
@ble_hw|ms=93510|begun=1|en=1|conn=0|adv=1|authok=0|authfail=0|connect=1|disconnect=0|mtu=172|rxw=0|rxd=0|rx=0|tx=0|txd=0|lrx=0|ltx=0
@ble_hw|ms=98511|begun=1|en=1|conn=1|adv=0|authok=1|authfail=0|connect=1|disconnect=0|mtu=172|rxw=0|rxd=0|rx=0|tx=0|txd=0|lrx=0|ltx=0
@ble_hw|ms=103511|begun=1|en=1|conn=0|adv=1|authok=1|authfail=0|connect=1|disconnect=1|mtu=172|rxw=0|rxd=0|rx=0|tx=0|txd=0|lrx=0|ltx=0
```

Interpretation:

- The local USB BLE radio found the real flashed T-Deck, not a different
  MeshCore radio in range.
- The firmware advertised, accepted a BLE connection, exchanged MTU `172`,
  completed authenticated pairing (`authok=1`, `authfail=0`), then returned to
  advertising after disconnect.
- Command-smoke mode is not yet a pass on this Windows USB BLE host. Targeted
  GATT attempts reached the correct advertisement but failed in the host stack
  before command traffic (`Could not start notify ... Unreachable`) or before
  authenticated writes (`Protocol Error 0x05: Insufficient Authentication`).
  The firmware log showed no `rx` or `tx` frames for those command attempts.
- Official MeshCore phone-app validation is still required to close the
  companion RX/TX criteria above.

## 2026-06-05 COM8 Validation Run

Validated from branch `codex/companion-ble-channel-data-442` on the T-Deck
attached to `COM8`.

Build and unit-test evidence:

- `pio test -e native_test -f test_companion_protocol`: passed, 15/15.
- `pio run -e SigurdOS_TDeck_ble`: passed.
- `pio run -e SigurdOS_TDeck_ble_validation`: passed.

Hardware evidence:

- Uploaded `SigurdOS_TDeck_ble_validation` to `COM8` with PlatformIO/esptool.
- Upload erased and wrote bootloader, partition table, boot app, and firmware
  ranges; every written range reported `Hash of data verified`.
- After reboot, SPIFFS was read back from `COM8` and unpacked with
  `mkspiffs_espressif32_arduino.exe`.
- `/ble_hw.txt` was present and contained 10 validation records.
- The first record at 4191 ms showed:

```text
@ble_hw|ms=4191|begun=1|en=1|conn=0|adv=1|authok=0|authfail=0|connect=0|disconnect=0|mtu=0|rxw=0|rxd=0|rx=0|tx=0|txd=0|lrx=0|ltx=0
```

- The final record at 49206 ms still showed `begun=1`, `en=1`, `conn=0`,
  `adv=1`, `authfail=0`, `rxd=0`, and `txd=0`.

Interpretation:

- The validation firmware boots on hardware.
- The companion BLE serial transport calls `begin()`, enables cleanly, and
  reaches the expected advertising state.
- No phone was paired during this bench run, so `connect`, `authok`, `rx`, and
  `tx` remain `0`. A phone pairing run with the official MeshCore app is still
  required for a complete companion BLE pass.

## 2026-06-05 COM8 Advert Command Follow-up

Follow-up work added official companion advert setters:

- `CMD_SET_ADVERT_NAME` (`8`) dispatches to the firmware node name, persists
  `NodePrefs::node_name`, and updates the live mesh own-name.
- `CMD_SET_ADVERT_LATLON` (`14`) accepts fixed-point latitude/longitude in
  degrees times 1e6, validates legal coordinate ranges, persists a manual
  advert-location fallback, and enables `share_location`. A live GPS fix still
  takes precedence when present.

Build and unit-test evidence after the advert command change:

- `pio test -e native_test -f test_companion_protocol`: passed, 19/19.
- `pio test -e native_test -f test_prefs_defaults`: passed, 5/5.
- `pio run -e SigurdOS_TDeck_ble`: passed; RAM 39.7%, flash 38.4%.
- `pio run -e SigurdOS_TDeck_ble_validation`: passed; RAM 39.7%, flash 38.4%.

Hardware evidence after the advert command change:

- Uploaded `SigurdOS_TDeck_ble_validation` to the T-Deck on `COM8`.
- esptool identified the board as ESP32-S3 rev v0.2, MAC
  `cc:8d:a2:0d:14:28`; every uploaded range reported
  `Hash of data verified`.
- After reset, SPIFFS was read back from `COM8` at offset `0xc90000`, size
  `0x360000`, and unpacked successfully.
- `/ble_hw.txt` was present with 9 validation records over a 43.4 s runtime
  window.
- The first record at 3415 ms showed:

```text
@ble_hw|ms=3415|begun=1|en=1|conn=0|adv=1|authok=0|authfail=0|connect=0|disconnect=0|mtu=0|rxw=0|rxd=0|rx=0|tx=0|txd=0|lrx=0|ltx=0
```

- The final record at 43432 ms still showed `begun=1`, `en=1`, `conn=0`,
  `adv=1`, `authfail=0`, `rxd=0`, and `txd=0`.

Interpretation:

- The updated BLE validation firmware still boots, enables the MeshCore BLE
  transport, and reaches the expected advertising state on real T-Deck
  hardware.
- The native protocol tests cover the new app command dispatch and payload
  validation paths.
- No phone was paired during this follow-up bench run, so app-authenticated
  traffic counters remain `0`. A phone pairing run is still required to close
  the connection/auth/RX/TX criteria above.

## 2026-06-05 COM8 Radio Command Follow-up

Follow-up work added official companion radio setters:

- `CMD_SET_RADIO_PARAMS` (`11`) accepts the official payload shape
  `freq_khz`, `bw_hz`, `sf`, `cr`, and optional `client_repeat`.
- `CMD_SET_RADIO_TX_POWER` (`12`) accepts one signed-byte TX power value.
- Persisted values are constrained to the same T-Deck RF policy used by the
  local UI and serial test controller: 400-1000 MHz, SF 6-12, 7.8-500 kHz
  bandwidth, CR 5-8, and 2-22 dBm TX power.
- Valid radio parameter changes are applied live through the mesh radio wrapper
  before being saved to `NodePrefs`.

Build and unit-test evidence after the radio command change:

- `pio test -e native_test -f test_companion_protocol`: passed, 25/25.
- `pio test -e native_test -f test_prefs_defaults`: passed, 5/5.
- `pio run -e SigurdOS_TDeck_ble`: passed; RAM 39.7%, flash 38.4%.
- `pio run -e SigurdOS_TDeck_ble_validation`: passed; RAM 39.7%, flash 38.4%.

Hardware evidence after the radio command change:

- Uploaded `SigurdOS_TDeck_ble_validation` to the T-Deck on `COM8`.
- esptool identified the board as ESP32-S3 rev v0.2, MAC
  `cc:8d:a2:0d:14:28`; every uploaded range reported
  `Hash of data verified`.
- After reset, SPIFFS was read back from `COM8` at offset `0xc90000`, size
  `0x360000`, and unpacked successfully.
- `/ble_hw.txt` was present with 9 validation records over a 43.4 s runtime
  window.
- The first record at 3410 ms showed:

```text
@ble_hw|ms=3410|begun=1|en=1|conn=0|adv=1|authok=0|authfail=0|connect=0|disconnect=0|mtu=0|rxw=0|rxd=0|rx=0|tx=0|txd=0|lrx=0|ltx=0
```

- The final record at 43427 ms still showed `begun=1`, `en=1`, `conn=0`,
  `adv=1`, `authfail=0`, `rxd=0`, and `txd=0`.

Interpretation:

- The updated BLE validation firmware still boots, enables the MeshCore BLE
  transport, and reaches the expected advertising state on real T-Deck
  hardware.
- The native protocol tests cover the new radio command payload parsing,
  dispatch, and illegal-argument behavior.
- No phone was paired during this follow-up bench run, so app-authenticated
  radio-command execution remains covered by native protocol tests plus BLE
  boot/advertising hardware proof. A phone pairing run is still required to
  close the connection/auth/RX/TX criteria above.

## 2026-06-05 COM8 Tuning Command Follow-up

Follow-up work added the official companion tuning command pair:

- `CMD_SET_TUNING_PARAMS` (`21`) accepts two little-endian uint32 values in
  thousandths, matching the companion-radio payload shape.
- `CMD_GET_TUNING_PARAMS` (`43`) returns `RESP_CODE_TUNING_PARAMS` (`23`) plus
  the same two little-endian uint32 values.
- The first field maps to the existing T-Deck `rx_delay_base` preference with
  the local UI-supported `0.0` to `20.0` range.
- The second field maps to the existing T-Deck `tx_delay_factor` preference
  with the local UI-supported `0.0` to `2.0` range. The separate local
  `direct_tx_delay_factor` setting is intentionally left unchanged because the
  official two-field companion command has no direct-TX field.

Build and unit-test evidence after the tuning command change:

- `pio test -e native_test -f test_companion_protocol`: passed, 32/32.
- `pio test -e native_test -f test_prefs_defaults`: passed, 5/5.
- `pio run -e SigurdOS_TDeck_ble`: passed; RAM 39.7%, flash 38.4%.
- `pio run -e SigurdOS_TDeck_ble_validation`: passed; RAM 39.7%, flash 38.4%.

Hardware evidence after the tuning command change:

- Uploaded `SigurdOS_TDeck_ble_validation` to the T-Deck on `COM8`.
- esptool identified the board as ESP32-S3 rev v0.2, MAC
  `cc:8d:a2:0d:14:28`; every uploaded range reported
  `Hash of data verified`.
- After reset, SPIFFS was read back from `COM8` at offset `0xc90000`, size
  `0x360000`, and unpacked successfully.
- `/ble_hw.txt` was present with 15 validation records over a 74.1 s runtime
  window.
- The first record at 4040 ms showed:

```text
@ble_hw|ms=4040|begun=1|en=1|conn=0|adv=1|authok=0|authfail=0|connect=0|disconnect=0|mtu=0|rxw=0|rxd=0|rx=0|tx=0|txd=0|lrx=0|ltx=0
```

- The final record at 74060 ms still showed `begun=1`, `en=1`, `conn=0`,
  `adv=1`, `authfail=0`, `rxd=0`, and `txd=0`.

Interpretation:

- The updated BLE validation firmware still boots, enables the MeshCore BLE
  transport, and reaches the expected advertising state on real T-Deck
  hardware.
- The native protocol tests cover the new tuning command payload parsing,
  response encoding, dispatch, and illegal-argument behavior.
- No phone was paired during this follow-up bench run, so app-authenticated
  tuning-command execution remains covered by native protocol tests plus BLE
  boot/advertising hardware proof. A phone pairing run is still required to
  close the connection/auth/RX/TX criteria above.

## 2026-06-06 COM8 Host USB BLE Command/Reconnect Investigation

This run used the local T-Deck attached to `COM8` and the host USB BLE radio.
No `COM11` or `COM29` access was used. The host BLE PIN environment variable
was unset, so the command-smoke attempts relied on the existing Windows cached
bond and did not print or store a PIN.

Top-level build and validation evidence:

- `python -m py_compile scripts\validation\companion_ble_smoke.py`: passed.
- `python scripts\validation\companion_ble_smoke.py --help`: showed
  `--reconnects` and `--reconnect-settle`.
- `git diff --check`: passed.
- `pio test -e native_test -f test_companion_protocol -v`: passed, 53/53.
- `pio run -e SigurdOS_TDeck_ble_validation`: passed; RAM 40.7%, flash 39.1%,
  merged image `2,626,688` bytes from the clean `lib/meshcore` checkout.
- Uploaded `SigurdOS_TDeck_ble_validation` to `COM8`; esptool identified
  ESP32-S3 rev v0.2, MAC `cc:8d:a2:0d:14:28`; every written range reported
  `Hash of data verified`.

Initial host BLE command/reconnect smoke:

- Command:

```powershell
python scripts\validation\companion_ble_smoke.py --name-prefix "MeshCore-SigurdOS" --scan-timeout 20 --reconnects 1
```

- The host found `MeshCore-SigurdOS T-Deck` at BLE address
  `CC:8D:A2:0D:14:29` with the MeshCore UART service UUID.
- Primary session failed before command traffic with
  `Could not start notify on 0029: Unreachable`.
- Reconnect session wrote `CMD_DEVICE_QUERY`, `CMD_APP_START`, and
  `CMD_SYNC_NEXT_MESSAGE`, but received no notifications.
- SPIFFS readback from `COM8` at `0xc90000` size `0x360000` showed
  `/ble_hw.txt` receiving those commands but dropping all responses:

```text
@ble_hw|ms=103718|begun=1|en=1|conn=0|adv=1|authok=1|authfail=0|connect=2|disconnect=1|mtu=172|rxw=3|rxd=0|rx=3|tx=0|txd=3|lrx=10|ltx=0
```

Interpretation:

- The Windows host could discover, connect, authenticate, negotiate MTU `172`,
  and deliver secured RX writes to the firmware.
- The firmware-side transport accepted companion commands but dropped notify
  responses after a cached-bond reconnect because the ESP32 MeshCore BLE
  transport only marked `deviceConnected=true` on an auth-complete callback.
- A local one-line MeshCore transport patch was tested to confirm the failure
  mode, but that patch is not present in this top-level branch.

Local MeshCore-transport-patched validation:

- Local patch under `lib/meshcore`: mark the ESP32 BLE transport connected
  after a secured RX write arrives while the server still has a connected
  central.
- Rebuilt `SigurdOS_TDeck_ble_validation`: passed; RAM 40.7%, flash 39.1%,
  merged image `2,626,704` bytes.
- Uploaded the patched validation image to `COM8`; every written range reported
  `Hash of data verified`.
- A reconnect-smoke run with `--reconnects 3` had one successful reconnect
  command session:

```text
session: PASS reconnect-1
  PASS device query: cmd=22 resp=13 len=82
  PASS app start: cmd=1 resp=5 len=73
  PASS sync empty or queued: cmd=10 resp=10 len=1
```

- A later `--reconnects 1` run had a full primary command-smoke pass:

```text
session: PASS primary
  PASS device query: cmd=22 resp=13 len=82
  PASS app start: cmd=1 resp=5 len=73
  PASS contacts start: cmd=4 resp=2 len=5
  PASS current time: cmd=5 resp=9 len=5
  PASS battery and storage: cmd=20 resp=12 len=11
  PASS core stats: cmd=56 resp=24 len=11
  PASS sync empty or queued: cmd=10 resp=10 len=1
```

- The same host USB BLE stack remained intermittent: other sessions failed at
  `start_notify` with `Unreachable` or `Operation aborted`.
- Post-fix SPIFFS readback showed successful firmware RX/TX and no response
  drops:

```text
@ble_hw|ms=318706|begun=1|en=1|conn=0|adv=1|authok=2|authfail=0|connect=7|disconnect=7|mtu=172|rxw=10|rxd=0|rx=10|tx=11|txd=0|lrx=10|ltx=10
```

Interpretation:

- The top-level validation tool can now record per-session command and
  reconnect evidence while keeping artifacts privacy-safe.
- With the local MeshCore transport patch, hardware proved companion RX/TX for
  the full command-smoke sequence and for one reconnect sync sequence.
- The host USB BLE adapter is still not stable enough to close the official
  RC2 companion BLE gate by itself. Official phone-app validation is still
  required for pairing, reconnect, sync, send/receive, and app shutdown.
- The MeshCore transport fix must land in the actual `lib/meshcore` source used
  by this firmware before this behavior is production-ready from a clean
  checkout.
