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
@gps_hw|ms=1000|fix=0|qual=0|sv=0|siv=0|ft=0|rmc=-|snr=0|snrc=0|baud=9600|chars=0|sent=0|valid=0|gga=0|rmc_s=0|gsv=0|gsa=0|csfail=0|sw=0|loc=0
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
| `snr` | Maximum non-zero GSV SNR/CN0 value in the latest GSV message set |
| `snrc` | Count of satellites with non-zero GSV SNR/CN0 in the latest GSV message set |
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

## Optional Local WiFi Uplink

If the T-Deck must be moved away from the workstation for real sky view, use
the validation-only WiFi harness. This is not part of release firmware. It
streams the same privacy-safe `@gps_hw` records to a local HTTP server while
keeping the SPIFFS and NVS evidence paths as the source of truth.

Do not commit WiFi credentials, LAN IPs, or exact coordinates. Provide network
settings through environment variables or through the ignored local file
`.pio/gps_validation_wifi_config.json`:

```json
{
  "ssid": "YOUR_2G_WIFI",
  "password": "YOUR_WIFI_PASSWORD",
  "host": "YOUR_LOCAL_SERVER_IPV4",
  "port": 8765,
  "path": "/gps"
}
```

Start the local capture server on the workstation or ethernet host:

```powershell
python scripts\validation\gps_validation_server.py --host 0.0.0.0 --port 8765 --out-dir .pio\gps_validation_wifi\outdoor-run
Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8765/health
```

Build and upload the WiFi harness to the approved hardware port:

```powershell
pio run -e SigurdOS_TDeck_gps_validation_wifi
pio run -e SigurdOS_TDeck_gps_validation_wifi -t upload --upload-port COM8
python -m esptool --chip esp32s3 --port COM8 --baud 115200 --after watchdog-reset read-mac
```

When `/health` reports increasing `records=<n>`, unplug the T-Deck from COM8
and move it to the sky-view location while keeping the device powered. The
server writes streamed records to `gps_hw_stream.txt`; exact coordinates remain
redacted unless the validation environment is deliberately built with
`SIGURDOS_GPS_VALIDATION_COORDS=1` for a private, local-only log.

After the run, plug the T-Deck back into COM8 and retrieve SPIFFS before NVS.
The WiFi stream is useful for live progress, but the final PR evidence should
still cite COM8 SPIFFS/NVS readbacks where possible.

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
- `snr` and `snrc` are recorded so a no-lock run can distinguish satellites
  merely listed in GSV from satellites with usable signal reports.
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
| Enhanced GPS diagnostics | Passed; after the diagnostic harness update, NVS readback showed `boot_count_value=8` and the 1825.7-second SPIFFS log reached `chars=942351`, `sent=24864`, `valid=24864`, `gga=1821`, `rmc_s=1821`, `gsv=2057`, `gsa=7284`, `csfail=0`, and `baud=38400` |
| GPS sky-view diagnostics | Partial; GSV reported satellites in view up to `siv=17`, but the latest persisted record still showed `ft=1` and `rmc=V` |
| GPS SNR diagnostics | Passed; after the SNR diagnostic update, a 920.5-second SPIFFS log reached max `siv=17`, max `snr=31`, max `snrc=17`, `valid=12496`, and `csfail=0` |
| GPS long SNR run | Passed as signal evidence but not lock proof; a 1815.5-second SPIFFS log reached max `siv=14`, max `snr=29`, max `snrc=14`, `valid=24434`, and all lock indicators stayed negative |
| GPS follow-up readback | Passed as continued signal evidence but not lock proof; a later 700.6-second SPIFFS log reached max `siv=6`, max `snr=27`, max `snrc=6`, `valid=9338`, `csfail=0`, and all lock indicators stayed negative |
| GPS fix proof | Not yet proven; after 1825.7 seconds in the enhanced run the final persisted record still showed `fix=0`, `qual=0`, `sv=0`, `ft=1`, `rmc=V`, and `loc=0` |

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
same watchdog reset flow, left closed for a 30-minute acquisition window, and
read back through SPIFFS. The NVS marker confirmed the new app boot:

```text
.pio\gps_validation_readback\nvs-long-30min.bin: namespace gpsval=present boot_count=present marker=present marker_value gps-validation=present boot_count_value=8 known_namespace sigurdos=present
```

The latest diagnostic records prove GGA, RMC, GSV, and GSA processing, and show
satellites in view without an acquired fix:

```text
@gps_hw|ms=885664|fix=0|qual=0|sv=0|siv=17|ft=1|rmc=V|baud=38400|chars=453987|sent=11995|valid=11995|gga=881|rmc_s=881|gsv=978|gsa=3524|csfail=0|sw=1|loc=0
@gps_hw|ms=1805664|fix=0|qual=0|sv=0|siv=2|ft=1|rmc=V|baud=38400|chars=931303|sent=24580|valid=24580|gga=1801|rmc_s=1801|gsv=2034|gsa=7204|csfail=0|sw=1|loc=0
@gps_hw|ms=1810664|fix=0|qual=0|sv=0|siv=3|ft=1|rmc=V|baud=38400|chars=933963|sent=24650|valid=24650|gga=1806|rmc_s=1806|gsv=2039|gsa=7224|csfail=0|sw=1|loc=0
@gps_hw|ms=1815664|fix=0|qual=0|sv=0|siv=2|ft=1|rmc=V|baud=38400|chars=936519|sent=24716|valid=24716|gga=1811|rmc_s=1811|gsv=2044|gsa=7244|csfail=0|sw=1|loc=0
@gps_hw|ms=1820664|fix=0|qual=0|sv=0|siv=6|ft=1|rmc=V|baud=38400|chars=939419|sent=24790|valid=24790|gga=1816|rmc_s=1816|gsv=2051|gsa=7264|csfail=0|sw=1|loc=0
@gps_hw|ms=1825664|fix=0|qual=0|sv=0|siv=1|ft=1|rmc=V|baud=38400|chars=942351|sent=24864|valid=24864|gga=1821|rmc_s=1821|gsv=2057|gsa=7284|csfail=0|sw=1|loc=0
```

This proves the T-Deck GPS UART and NMEA parser path on COM8 using the approved
hardware port.

The SNR diagnostic harness was then flashed to COM8, started through watchdog
reset, left closed for a 15-minute acquisition window, and read back through
SPIFFS before NVS to preserve the persisted log. The NVS marker confirmed the
new app boot:

```text
.pio\gps_validation_readback\nvs-snr-15min.bin: namespace gpsval=present boot_count=present marker=present marker_value gps-validation=present boot_count_value=15 known_namespace sigurdos=present
```

The 920.5-second log produced 185 persisted `@gps_hw` records. It still did not
capture a GPS lock, but it did show non-zero GSV signal reports:

```text
records=185
fix_records=0
max_ms=920517
max_siv=17
max_snr=31
max_snrc=17
max_ft=1
max_sv=0
max_valid=12496
max_gga=916
max_rmc_s=916
max_gsv=1072
max_gsa=3664
max_csfail=0
snr_positive_records=126
snrc_positive_records=126
final=@gps_hw|ms=920517|fix=0|qual=0|sv=0|siv=2|ft=1|rmc=V|snr=21|snrc=2|baud=38400|chars=473836|sent=12496|valid=12496|gga=916|rmc_s=916|gsv=1072|gsa=3664|csfail=0|sw=1|loc=0
```

The validation app was restarted again through the COM8 watchdog-reset path,
left closed for a full 30-minute acquisition window, and read back through
SPIFFS before NVS. The NVS marker confirmed the app boot:

```text
.pio\gps_validation_readback\nvs-long-continuation.bin: namespace gpsval=present boot_count=present marker=present marker_value gps-validation=present boot_count_value=21 known_namespace sigurdos=present
```

The long continuation run still did not capture lock, but it continued to prove
clean GPS traffic and non-zero GSV signal reports:

```text
records=364
fix_records=0
active_rmc_records=0
loc_records=0
max_ms=1815529
max_siv=14
max_snr=29
max_snrc=14
max_ft=1
max_sv=0
max_valid=24434
max_gga=1811
max_rmc_s=1811
max_gsv=2205
max_gsa=7244
max_csfail=80
snr_positive_records=323
snrc_positive_records=323
final=@gps_hw|ms=1815529|fix=0|qual=0|sv=0|siv=1|ft=1|rmc=V|snr=10|snrc=1|baud=38400|chars=935222|sent=24514|valid=24434|gga=1811|rmc_s=1811|gsv=2205|gsa=7244|csfail=80|sw=1|loc=0
```

After the device was left running the validation harness again, a follow-up
SPIFFS readback over COM8 captured another no-lock acquisition window:

```text
records=141
fix_records=0
active_rmc_records=0
loc_records=0
max_ms=700643
max_siv=6
max_snr=27
max_snrc=6
max_ft=1
max_sv=0
max_valid=9338
max_gga=696
max_rmc_s=696
max_gsv=714
max_gsa=2784
max_csfail=0
final=@gps_hw|ms=700643|fix=0|qual=0|sv=0|siv=0|ft=1|rmc=V|snr=0|snrc=0|baud=38400|chars=348937|sent=9338|valid=9338|gga=696|rmc_s=696|gsv=714|gsa=2784|csfail=0|sw=1|loc=0
```

A final GPS lock is still required: continue with the watchdog reset flow,
longer sky-view runtime, antenna/placement checks, and another SPIFFS readback
until a final record shows `fix=1`, `qual>0`, `sv>0`, and `loc=1`.

The vendored MeshCore FAQ specifically calls out T-Deck Plus GPS modules that
were installed upside down, with the GPS antenna facing down. Because this run
proves `38400` baud, mostly clean NMEA, GGA/RMC/GSV/GSA traffic, satellites in
view, and non-zero SNR/CN0 without reaching GSA 2D/3D fix or RMC active status,
the next physical check should be GPS antenna orientation and placement before
more firmware changes.

## 2026-06-06 Current Dev Follow-Up

Port safety constraint: only `COM8` was opened. `COM11` and `COM29` were not
opened or enumerated.

Repository state:

- Base: `origin/dev` at `8827b30` (`docs: sync AGENT_GUIDE.md with codebase state [auto]`).
- Open upstream PRs: none at the start of this validation pass.
- Merged companion BLE bridge work is present on `origin/dev`.

Validation:

| Check | Result |
| --- | --- |
| `pio run -e SigurdOS_TDeck_gps_validation` | Passed; RAM 6.0%, flash 5.2% |
| `pio run -e SigurdOS_TDeck_gps_validation -t upload --upload-port COM8` | Passed; ESP32-S3 MAC `cc:8d:a2:0d:14:28`; all flashed segments hash-verified |
| `pio test -e native_test -v` | Passed; 679 test cases, 678 succeeded, 1 skipped |
| `pio run -e SigurdOS_TDeck_gps_validation_wifi` with placeholder config | Passed; RAM 13.8%, flash 11.7%; sensitive values were not printed |
| `pio run -e SigurdOS_TDeck_gps_validation_wifi -t upload --upload-port COM8` with ignored local config | Passed; all flashed segments hash-verified |

The first 2026-06-06 readback accidentally captured a short post-readback
restart window because NVS was read before SPIFFS. It was not used as lock
evidence, but it did confirm the validation app booted:

```text
.pio\gps_validation_readback\2026-06-06-5min\nvs.bin: namespace gpsval=present boot_count=present marker=present marker_value gps-validation=present boot_count_value=28 known_namespace sigurdos=present
```

The standing 15-minute run was then restarted through the COM8 watchdog-reset
path, left closed for 900 seconds, and read back through SPIFFS before NVS. The
SPIFFS log was preserved and produced:

```text
records=188
fix_records=0
active_rmc_records=0
loc_records=0
max_ms=935919
max_siv=11
max_snr=29
max_snrc=11
max_ft=1
max_sv=0
max_valid=12576
max_gga=932
max_rmc_s=931
max_gsv=951
max_gsa=3728
max_csfail=1
snr_positive_records=90
snrc_positive_records=90
final=@gps_hw|ms=935919|fix=0|qual=0|sv=0|siv=0|ft=1|rmc=V|snr=0|snrc=0|baud=38400|chars=475078|sent=12577|valid=12576|gga=932|rmc_s=931|gsv=951|gsa=3728|csfail=1|sw=1|loc=0
```

The NVS marker after that run confirmed the app boot and persistence path:

```text
.pio\gps_validation_readback\2026-06-06-15min-standing\nvs.bin: namespace gpsval=present boot_count=present marker=present marker_value gps-validation=present boot_count_value=32 known_namespace sigurdos=present
```

The standing placement still did not satisfy the final GPS pass criteria. The
validation-only WiFi uplink was therefore flashed to COM8 with credentials from
an ignored `.pio` config file, started through watchdog reset, and verified
locally by the capture server:

```text
ok records=14 lock_seen=0
@gps_hw|ms=55335|fix=0|qual=0|sv=0|siv=1|ft=1|rmc=V|snr=8|snrc=1|baud=38400|chars=28000|sent=726|valid=726|gga=50|rmc_s=50|gsv=50|gsa=200|csfail=0|sw=1|loc=0
```

The T-Deck was then moved outside for sky view. The local WiFi stream first
produced privacy-safe GPS lock proof without publishing coordinates:

```text
records=67
fix_records=15
active_rmc_records=15
loc_records=15
max_ms=306335
max_siv=7
max_snr=28
max_snrc=2
max_ft=3
max_sv=8
max_valid=4112
max_gga=302
max_rmc_s=302
max_gsv=620
max_gsa=1208
max_csfail=1
first_fix=@gps_hw|ms=236335|fix=1|qual=1|sv=7|siv=5|ft=3|rmc=A|snr=0|snrc=0|baud=38400|chars=115509|sent=3062|valid=3062|gga=232|rmc_s=232|gsv=340|gsa=928|csfail=0|sw=1|loc=1
final=@gps_hw|ms=306335|fix=1|qual=1|sv=8|siv=5|ft=3|rmc=A|snr=0|snrc=0|baud=38400|chars=168999|sent=4112|valid=4112|gga=302|rmc_s=302|gsv=620|gsa=1208|csfail=0|sw=1|loc=1
```

After the device returned to the workstation, SPIFFS was read before NVS on
COM8. The on-device log confirmed the outdoor lock persisted in flash:

```text
records=152
fix_records=84
active_rmc_records=84
loc_records=84
max_ms=751335
max_siv=12
max_snr=28
max_snrc=2
max_ft=3
max_sv=12
max_valid=11398
max_gga=747
max_rmc_s=747
max_gsv=2412
max_gsa=2988
max_csfail=0
first_fix=@gps_hw|ms=236335|fix=1|qual=1|sv=7|siv=5|ft=3|rmc=A|snr=0|snrc=0|baud=38400|chars=115509|sent=3062|valid=3062|gga=232|rmc_s=232|gsv=340|gsa=928|csfail=0|sw=1|loc=1
final=@gps_hw|ms=751335|fix=0|qual=0|sv=0|siv=11|ft=1|rmc=V|snr=0|snrc=0|baud=38400|chars=534706|sent=11398|valid=11398|gga=747|rmc_s=747|gsv=2412|gsa=2988|csfail=0|sw=1|loc=0
```

The final record lost fix after the device was moved back indoors, but the same
SPIFFS image contains 84 fixed records and the first fixed record satisfies the
hardware lock criteria: `fix=1`, `qual=1`, `sv=7`, `ft=3`, `rmc=A`, and
`loc=1`. Coordinate publishing remained disabled (`coords=0`), and the WiFi
SSID, password, and local server address stayed in an ignored `.pio` config file
instead of project files.

The final NVS marker after the return readback confirmed the validation app boot
and marker persistence path:

```text
.pio\gps_validation_readback\2026-06-06-outdoor-return\nvs.bin: namespace gpsval=present boot_count=present marker=present marker_value gps-validation=present boot_count_value=36 known_namespace sigurdos=present
```
