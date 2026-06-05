# T-Deck GPS Hardware Validation

This document records the GPS hardware validation path for the LilyGo T-Deck
firmware. It is intended for PR evidence and field bring-up, not for normal
release operation.

## Validation Firmware

Use the dedicated GPS harness when validating UART wiring, baud selection, NMEA
parsing, and GPS lock without the full UI, mesh, SD, WiFi, or LVGL startup path:

```powershell
pio run -e SigurdOS_TDeck_gps_validation
pio run -e SigurdOS_TDeck_gps_validation -t upload --upload-port COM8
```

The harness links the same `src/hal/gps.cpp` implementation used by the full
firmware. It marks app startup in NVS namespace `gpsval`, emits one structured
line per second over serial, and appends privacy-safe records to `/gps_hw.txt`
in SPIFFS every five seconds and on the first fix. The NVS marker and SPIFFS
copy are intended for COM8-only readback when the app-side USB CDC serial
endpoint is not observable.

```text
[gps-validation] SigurdOS T-Deck GPS validation firmware
[gps-validation] uart rx=44 tx=43 primary=9600 fallback=38400
[gps-validation] nvs=1 boot_count=1
[gps-validation] spiffs=1 log=/gps_hw.txt
@gps_hw|ms=1000|fix=0|qual=0|sv=0|baud=9600|chars=0|sent=0|valid=0|csfail=0|sw=0|loc=0
```

Fields:

| Field | Meaning |
| --- | --- |
| `ms` | Device uptime in milliseconds when the record was emitted |
| `fix` | `1` once the parser has a valid GPS fix |
| `qual` | GGA fix quality (`0` none, `1` GPS, `2` DGPS, `4` RTK) |
| `sv` | Satellites reported by GGA |
| `baud` | Active UART baud, expected `9600` once valid NMEA is seen |
| `chars` | GPS UART characters processed |
| `sent` | Complete NMEA sentences received |
| `valid` | Checksum-valid NMEA sentences |
| `csfail` | NMEA checksum failures |
| `sw` | Baud-probe switches before checksum lock |
| `loc` | `1` when a fix includes non-zero latitude and longitude |

Exact coordinates are intentionally not emitted by default so PR logs do not
publish the operator's physical location. To include coordinates for a private
bench log, add `-D SIGURDOS_GPS_VALIDATION_COORDS=1` to the validation env.

To verify that the validation app reached `setup()` through the bootloader, read
the NVS partition from Arduino's `default_16MB.csv` layout and scan for the
validation namespace/marker:

```powershell
New-Item -ItemType Directory -Force -Path .pio\gps_validation_readback | Out-Null
python -m esptool --chip esp32s3 --port COM8 --baud 921600 read-flash 0x9000 0x5000 .pio\gps_validation_readback\nvs.bin
python scripts\validation\nvs_boot_marker_check.py .pio\gps_validation_readback\nvs.bin --require
```

The helper decodes NVS entry names and also scans for the marker string. It
exits non-zero until the `gpsval` namespace, `boot_count` key, `marker` key, and
`gps-validation` marker value are present. To retrieve the SPIFFS evidence log
through the bootloader, read and unpack the SPIFFS partition:

```powershell
New-Item -ItemType Directory -Force -Path .pio\gps_validation_readback | Out-Null
python -m esptool --chip esp32s3 --port COM8 --baud 921600 read-flash 0xc90000 0x360000 .pio\gps_validation_readback\spiffs.bin
& "$env:USERPROFILE\.platformio\packages\tool-mkspiffs\mkspiffs_espressif32_arduino.exe" -b 4096 -p 256 -s 0x360000 -u .pio/gps_validation_readback/unpacked .pio/gps_validation_readback/spiffs.bin
Get-Content .pio\gps_validation_readback\unpacked\gps_hw.txt
```

## Pass Criteria

A hardware GPS pass requires all of the following:

- Firmware uploads to the T-Deck over the approved hardware port.
- The NVS readback contains `gpsval` and `gps-validation`, proving the app
  reached validation `setup()`.
- The validation harness banner is visible over serial, or `/gps_hw.txt` is
  present in a SPIFFS readback and starts with `[gps-validation] log-start`.
- `chars`, `sent`, and `valid` increase in serial output or the persisted log
  while the device has sky view.
- The active baud settles at `9600` after valid NMEA is received.
- A final record shows `fix=1`, `qual>0`, `sv>0`, and `loc=1`.
- Any published log redacts exact latitude and longitude.

## 2026-06-05 COM8 Attempt

Port safety constraint: only `COM8` was opened. `COM11` and `COM29` were not
opened or enumerated.

Results:

| Check | Result |
| --- | --- |
| `pio run -e SigurdOS_TDeck_telemetry` | Passed; RAM 86.3%, flash 27.8% |
| `pio run -e SigurdOS_TDeck_telemetry -t upload --upload-port COM8` | Passed; ESP32-S3 MAC `cc:8d:a2:0d:14:28`; all flashed segments hash-verified |
| `pio run -e SigurdOS_TDeck_gps_validation` | Passed; RAM 5.9%, flash 4.5% |
| `pio run -e SigurdOS_TDeck_gps_validation -t upload --upload-port COM8` | Passed; all flashed segments hash-verified |
| COM8 ROM serial visibility | Passed; ROM downloader banner is visible |
| COM8 app serial visibility | Not proven; app banner and `@gps_hw` records were not visible |
| COM8 NVS readback | Passed; NVS partition read succeeded over COM8 |
| NVS boot marker | Not present after post-upload, bootloader `run`, no-stub `run`, DTR-low reset, and DTR-high reset windows; `scripts/validation/nvs_boot_marker_check.py` parsed the existing `sigurdos` namespace but found no `gpsval`, `boot_count`, `marker`, or `gps-validation` entries |
| COM8 SPIFFS readback | Passed; SPIFFS partition read and unpack succeeded over COM8 |
| GPS validation app execution | Not proven; `/gps_hw.txt` was absent after post-upload, bootloader `run`, and explicit DTR/RTS app-reset windows |
| GPS fix proof | Not proven because app serial output was not observable on COM8 |

Observed COM8 ROM output after opening the port:

```text
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x15 (USB_UART_CHIP_RESET),boot:0x0 (DOWNLOAD(USB/UART0))
waiting for download
```

Reset attempts kept to COM8:

- `esptool --chip esp32s3 --port COM8 run`
- DTR low / RTS pulse with the same serial session held open
- DTR high / RTS pulse with the same serial session held open
- esptool USB Serial/JTAG reset sequence
- post-upload quiet run followed by SPIFFS readback
- bootloader `run` quiet window followed by SPIFFS readback
- bootloader `run` and no-stub `run` quiet windows followed by NVS readback
- explicit DTR low / RTS app-reset pulse followed by SPIFFS readback
- explicit DTR low and DTR high / RTS app-reset pulses followed by NVS readback

None produced observable app serial output, a validation NVS marker, or a
persisted `/gps_hw.txt` log on COM8 in this environment. The NVS helper output
for the retained readbacks was:

```text
.pio\gps_validation_readback\nvs-before.bin: namespace gpsval=absent boot_count=absent marker=absent marker_value gps-validation=absent known_namespace sigurdos=present
.pio\gps_validation_readback\nvs-after-upload.bin: namespace gpsval=absent boot_count=absent marker=absent marker_value gps-validation=absent known_namespace sigurdos=present
.pio\gps_validation_readback\nvs-after-nostub-run.bin: namespace gpsval=absent boot_count=absent marker=absent marker_value gps-validation=absent known_namespace sigurdos=present
.pio\gps_validation_readback\nvs-after-dtr1-reset.bin: namespace gpsval=absent boot_count=absent marker=absent marker_value gps-validation=absent known_namespace sigurdos=present
```

The next validation attempt should either correct the COM8 reset/BOOT line
state, physically reset the device while the validation firmware is flashed, or
use the app-side USB CDC port after confirming it is allowed by the port safety
policy.
