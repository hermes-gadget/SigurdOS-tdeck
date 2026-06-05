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
@gps_hw|ms=1000|fix=0|qual=0|sv=0|siv=0|ft=0|rmc=-|baud=9600|chars=0|sent=0|valid=0|gga=0|rmc_s=0|gsv=0|gsa=0|csfail=0|sw=0|loc=0
```

Fields:

| Field | Meaning |
| --- | --- |
| `ms` | Device uptime in milliseconds when the record was emitted |
| `fix` | `1` once the parser has a valid GPS fix |
| `qual` | GGA fix quality (`0` none, `1` GPS, `2` DGPS, `4` RTK) |
| `sv` | Satellites reported by GGA |
| `siv` | Satellites in view reported by the latest GSV sentence |
| `ft` | GSA fix type (`1` none, `2` 2D, `3` 3D) |
| `rmc` | RMC status (`A` active, `V` void, `-` unknown before RMC arrives) |
| `baud` | Active UART baud; record whether valid NMEA is seen at the primary `9600` baud or fallback `38400` baud |
| `chars` | GPS UART characters processed |
| `sent` | Complete NMEA sentences received |
| `valid` | Checksum-valid NMEA sentences |
| `gga` | Checksum-valid GGA sentences processed |
| `rmc_s` | Checksum-valid RMC sentences processed |
| `gsv` | Checksum-valid GSV sentences processed |
| `gsa` | Checksum-valid GSA sentences processed |
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
`gps-validation` marker value are present. When available, it also prints
`boot_count_value=<n>` so reset attempts can be checked quickly.

If the device is already in the ROM bootloader, the watchdog reset path starts
the validation app without relying on the COM8 RTS/DTR boot-strapping state:

```powershell
python -m esptool --chip esp32s3 --port COM8 --baud 115200 --after watchdog-reset read-mac
```

After this command returns, leave COM8 closed for the intended GPS acquisition
window. To retrieve the SPIFFS evidence log through the bootloader, read and
unpack the SPIFFS partition:

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
- GGA, RMC, GSV, and GSA counters increase in the persisted log, proving the
  parser is observing the sentence families needed to diagnose acquisition.
- The active baud settles at either the primary `9600` baud or fallback `38400`
  baud after valid NMEA is received, and the observed baud is recorded.
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
| Early NVS boot marker attempts | Not present after post-upload, bootloader `run`, no-stub `run`, DTR-low reset, and DTR-high reset windows; `scripts/validation/nvs_boot_marker_check.py` parsed the existing `sigurdos` namespace but found no `gpsval`, `boot_count`, `marker`, or `gps-validation` entries |
| COM8 watchdog reset app start | Passed; `python -m esptool --chip esp32s3 --port COM8 --baud 115200 --after watchdog-reset read-mac` started the app and advanced `boot_count_value` from `2` to `3`, then `4` on the 10-minute run |
| COM8 SPIFFS readback | Passed; SPIFFS partition read and unpack succeeded over COM8 |
| GPS validation app execution | Passed through NVS/SPIFFS evidence; `/gps_hw.txt` starts with `[gps-validation] log-start` after watchdog reset |
| GPS UART/NMEA hardware path | Passed; 10-minute persisted log reached `chars=294169`, `sent=8286`, `valid=8286`, `csfail=0`, `baud=38400` |
| Enhanced GPS diagnostics | Passed; after the diagnostic harness update, NVS readback showed `boot_count_value=7` and the 930.6-second SPIFFS log reached `chars=467784`, `sent=12689`, `valid=12688`, `gga=926`, `rmc_s=926`, `gsv=1065`, `gsa=3708`, `csfail=1`, and `baud=38400` |
| GPS sky-view diagnostics | Partial; GSV reported satellites in view up to `siv=10`, but the latest persisted record still showed `ft=1` and `rmc=V` |
| GPS fix proof | Not yet proven; after 930.6 seconds in the enhanced run the final persisted record still showed `fix=0`, `qual=0`, `sv=0`, `ft=1`, `rmc=V`, and `loc=0` |

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
- watchdog reset from ROM bootloader followed by NVS and SPIFFS readback

The first reset group produced no observable app serial output, validation NVS
marker, or persisted `/gps_hw.txt` log on COM8 in this environment. The NVS
helper output for the retained early readbacks was:

```text
.pio\gps_validation_readback\nvs-before.bin: namespace gpsval=absent boot_count=absent marker=absent marker_value gps-validation=absent known_namespace sigurdos=present
.pio\gps_validation_readback\nvs-after-upload.bin: namespace gpsval=absent boot_count=absent marker=absent marker_value gps-validation=absent known_namespace sigurdos=present
.pio\gps_validation_readback\nvs-after-nostub-run.bin: namespace gpsval=absent boot_count=absent marker=absent marker_value gps-validation=absent known_namespace sigurdos=present
.pio\gps_validation_readback\nvs-after-dtr1-reset.bin: namespace gpsval=absent boot_count=absent marker=absent marker_value gps-validation=absent known_namespace sigurdos=present
```

The watchdog reset path did start the validation app:

```text
.pio\gps_validation_readback\nvs-after-watchdog-reset.bin: namespace gpsval=present boot_count=present marker=present marker_value gps-validation=present boot_count_value=3 known_namespace sigurdos=present
.pio\gps_validation_readback\nvs-watchdog-10min.bin: namespace gpsval=present boot_count=present marker=present marker_value gps-validation=present boot_count_value=4 known_namespace sigurdos=present
```

The 10-minute SPIFFS readback produced `/gps_hw.txt` with continuous valid NMEA
traffic at fallback baud and no checksum failures:

```text
@gps_hw|ms=595588|fix=0|qual=0|sv=0|baud=38400|chars=284249|sent=8007|valid=8007|csfail=0|sw=1|loc=0
@gps_hw|ms=600588|fix=0|qual=0|sv=0|baud=38400|chars=286582|sent=8075|valid=8075|csfail=0|sw=1|loc=0
@gps_hw|ms=605588|fix=0|qual=0|sv=0|baud=38400|chars=289043|sent=8145|valid=8145|csfail=0|sw=1|loc=0
@gps_hw|ms=610588|fix=0|qual=0|sv=0|baud=38400|chars=291628|sent=8216|valid=8216|csfail=0|sw=1|loc=0
@gps_hw|ms=615588|fix=0|qual=0|sv=0|baud=38400|chars=294169|sent=8286|valid=8286|csfail=0|sw=1|loc=0
```

The enhanced diagnostic harness was then flashed to COM8, started through the
same watchdog reset flow, left closed for another 15-minute acquisition window,
and read back through SPIFFS. The NVS marker confirmed the new app boot:

```text
.pio\gps_validation_readback\nvs-enhanced-15min.bin: namespace gpsval=present boot_count=present marker=present marker_value gps-validation=present boot_count_value=7 known_namespace sigurdos=present
```

The latest diagnostic records prove GGA, RMC, GSV, and GSA processing, and show
satellites in view without an acquired fix:

```text
@gps_hw|ms=220602|fix=0|qual=0|sv=0|siv=10|ft=1|rmc=V|baud=38400|chars=108677|sent=2999|valid=2998|gga=216|rmc_s=216|gsv=268|gsa=868|csfail=1|sw=1|loc=0
@gps_hw|ms=905602|fix=0|qual=0|sv=0|siv=5|ft=1|rmc=V|baud=38400|chars=453948|sent=12339|valid=12338|gga=901|rmc_s=901|gsv=1033|gsa=3608|csfail=1|sw=1|loc=0
@gps_hw|ms=910602|fix=0|qual=0|sv=0|siv=5|ft=1|rmc=V|baud=38400|chars=456788|sent=12412|valid=12411|gga=906|rmc_s=906|gsv=1042|gsa=3628|csfail=1|sw=1|loc=0
@gps_hw|ms=915602|fix=0|qual=0|sv=0|siv=4|ft=1|rmc=V|baud=38400|chars=459364|sent=12477|valid=12476|gga=911|rmc_s=911|gsv=1047|gsa=3648|csfail=1|sw=1|loc=0
@gps_hw|ms=920602|fix=0|qual=0|sv=0|siv=5|ft=1|rmc=V|baud=38400|chars=462059|sent=12546|valid=12545|gga=916|rmc_s=916|gsv=1053|gsa=3668|csfail=1|sw=1|loc=0
@gps_hw|ms=925602|fix=0|qual=0|sv=0|siv=4|ft=1|rmc=V|baud=38400|chars=464944|sent=12618|valid=12617|gga=921|rmc_s=921|gsv=1059|gsa=3688|csfail=1|sw=1|loc=0
@gps_hw|ms=930602|fix=0|qual=0|sv=0|siv=5|ft=1|rmc=V|baud=38400|chars=467784|sent=12689|valid=12688|gga=926|rmc_s=926|gsv=1065|gsa=3708|csfail=1|sw=1|loc=0
```

This proves the T-Deck GPS UART and NMEA parser path on COM8 using the approved
hardware port. A final GPS lock is still required: continue with the watchdog
reset flow, longer sky-view runtime, antenna/placement checks, and another
SPIFFS readback until a final record shows `fix=1`, `qual>0`, `sv>0`, and
`loc=1`.
