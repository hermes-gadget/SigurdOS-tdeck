# T-Deck Companion BLE Hardware Validation

This document records the hardware validation path for the companion BLE
firmware used by the MeshCore phone app. It is intended for PR evidence and
field bring-up, not for normal release operation.

## Validation Firmware

Use the BLE validation build when validating ESP32-S3 BLE startup, advertising,
pairing callbacks, app command traffic, and companion bridge responses:

```powershell
pio run -e SigurdOS_TDeck_ble_validation
pio run -e SigurdOS_TDeck_ble_validation -t upload --upload-port COM8
```

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
python -m esptool --chip esp32s3 --port COM8 --baud 921600 read-flash 0xc90000 0x360000 .pio\ble_validation_readback\spiffs.bin
& "$env:USERPROFILE\.platformio\packages\tool-mkspiffs\mkspiffs_espressif32_arduino.exe" -b 4096 -p 256 -s 0x360000 -u .pio/ble_validation_readback/unpacked .pio/ble_validation_readback/spiffs.bin
Get-Content .pio\ble_validation_readback\unpacked\ble_hw.txt
```

## Pass Criteria

A complete companion BLE hardware pass requires all of the following:

- Firmware uploads to the T-Deck over the approved hardware port.
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

## 2026-06-05 COM8 Scope

Port safety constraint: only `COM8` may be opened for this validation in the
current bench setup. `COM11` and `COM29` must not be opened or enumerated.

The current Windows host does not expose a present Bluetooth radio, so a host
BLE scan or GATT session cannot prove phone/app behavior here. The validation
build still provides useful hardware proof for BLE boot and advertising state,
and it captures the required counters when a real phone is paired.

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
