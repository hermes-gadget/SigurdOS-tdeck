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
firmware. It emits one structured line per second:

```text
[gps-validation] SigurdOS T-Deck GPS validation firmware
[gps-validation] uart rx=44 tx=43 primary=9600 fallback=38400
@gps_hw|fix=0|qual=0|sv=0|baud=9600|chars=0|sent=0|valid=0|csfail=0|sw=0|loc=0
```

Fields:

| Field | Meaning |
| --- | --- |
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

## Pass Criteria

A hardware GPS pass requires all of the following:

- Firmware uploads to the T-Deck over the approved hardware port.
- The validation harness banner is visible over serial.
- `chars`, `sent`, and `valid` increase while the device has sky view.
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

None produced observable app serial output on COM8 in this environment. The
next validation attempt should either correct the COM8 reset/BOOT line state,
physically reset the device while the COM8 monitor is attached, or use the
app-side USB CDC port after confirming it is allowed by the port safety policy.
