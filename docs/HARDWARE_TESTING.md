# SigurdOS T-Deck Hardware Testing Protocol

This is the authoritative protocol for testing SigurdOS on a physical LilyGo
T-Deck. Contributors and AI agents must read it before flashing or operating a
shared hardware fixture. Use [`scripts/hw_test/CHECKLIST.md`](../scripts/hw_test/CHECKLIST.md)
as the copy-paste record for an individual run.

The protocol validates a specific commit and firmware image. A successful
native test or build is not evidence that the display, input devices, storage,
radio, BLE, or power behavior works on a T-Deck.

## Quick Reference Card

### Non-negotiable rules

1. Flash `.pio/build/<env>/firmware-merged.bin` at `0x0`. Never flash
   `firmware.bin` at `0x0`; it is app-only and causes `Invalid image block`.
2. Use one persistent serial connection for a test sequence. Opening and
   closing the port per command can reset the ESP32-S3 and invalidates soak data.
3. Never call `setDTR()`, set `dsrdtr`, or manipulate DTR in a monitor. Default
   pySerial behavior plus one boot wait is the supported pattern.
4. Remote-test builds use lowercase commands such as `help`, `nav`, and
   `capture`. The optional display handler uses uppercase `NAV`, `SCREENSHOT`,
   and `SEND`.
5. Read screenshots with repeated bulk `read()` calls until `[capture] END`.
   Do not use `readline()` for the framebuffer stream.
6. Production builds normally emit no periodic serial output. Silence after a
   clean boot is not proof of a hang.
7. Do not transmit until the frequency, power, antenna, and legal authority for
   the test have been confirmed. The remote-radio default may overlap an active
   mesh frequency.
8. A device previously booted by Launcher may require a physical USB unplug and
   replug after esptool. A reset or RTS pulse is not always enough.

### Minimum build matrix

| Purpose | Environment | Radio | Test controller | BLE | Serial commands |
|---|---|---:|---:|---:|---|
| Release verification | `SigurdOS_TDeck` | Yes | No | Yes | None by default; uppercase handler is build-gated |
| Full diagnostics | `SigurdOS_TDeck_debug` | Yes | No | Yes | Diagnostic output; uppercase handler is build-gated |
| UI-only automation | `SigurdOS_TDeck_remote_test` | No | Yes | Compiled, no live mesh | Lowercase |
| Canonical hardware test | `SigurdOS_TDeck_remote_test_radio` | Yes | Yes | Yes | Lowercase |
| Autonomous BLE validation | `SigurdOS_TDeck_ble_agent` | Yes | Yes | Yes + validation | Lowercase plus `ble ...` |

### Canonical commands

Run builds from the repository root:

```bash
pio test -e native_test -v
pio run -e SigurdOS_TDeck
pio run -e SigurdOS_TDeck_remote_test_radio
```

Flash the canonical test image through the Pi gateway:

```bash
scp .pio/build/SigurdOS_TDeck_remote_test_radio/firmware-merged.bin \
  hermes-pi:/tmp/sigurdos-hw-test.bin

ssh hermes-pi \
  "~/hermes-venv/bin/esptool --chip esp32s3 --port /dev/ttyACM0 \
   --baud 921600 --before default-reset --after hard-reset \
   write-flash 0x0 /tmp/sigurdos-hw-test.bin"
```

Deploy the optional Hermes helpers, when installed on the host:

```bash
scp ~/.hermes/scripts/tdeck_test_suite.py hermes-pi:/tmp/
scp ~/.hermes/scripts/td_cmd.py hermes-pi:/tmp/
scp ~/.hermes/skills/devops/tdeck-hardware-test-suite/scripts/batch_capture_rt.py \
  hermes-pi:/tmp/
scp ~/.hermes/skills/devops/tdeck-hardware-test-suite/scripts/persistent_soak.py \
  hermes-pi:/tmp/
```

Run the fast checks:

```bash
ssh hermes-pi \
  "python3 /tmp/tdeck_test_suite.py --smoke --pi-mode \
   --outdir /tmp/sigurdos-smoke"

ssh hermes-pi \
  "python3 /tmp/tdeck_test_suite.py --ui --iterations 1 --pi-mode \
   --outdir /tmp/sigurdos-ui"

ssh hermes-pi \
  "python3 /tmp/batch_capture_rt.py home settings"
```

Run a true two-hour soak using one serial connection:

```bash
ssh hermes-pi \
  "python3 /tmp/persistent_soak.py --port /dev/ttyACM0 \
   --duration 7200 --outdir /tmp/sigurdos-soak-2h"
```

The files under `~/.hermes/` are fixture tooling, not repository dependencies.
If they are not installed, use the command and capture patterns in this document
or obtain the current scripts from the fixture owner. Do not silently substitute
an old copy.

### Six-phase gate

| Phase | Target time | Required for | Pass condition |
|---|---:|---|---|
| 1. Native tests | under 30 seconds | Every change | Full suite exits 0 |
| 2. Build verification | under 2 minutes | Every change | Release and remote-radio images link; merged artifacts exist |
| 3. Smoke test | under 2 minutes after boot | Firmware-affecting changes | Clean boot, controller response, status, navigation, complete capture |
| 4. UI navigation | under 5 minutes without full captures | Firmware/UI changes | All applicable screens open; no crash, hang, or allocation failure |
| 5. Feature tests | Change-dependent | Affected subsystems | Specific behavior and persistence/interoperability proven |
| 6. Soak | 30 minutes to 2 hours | Pre-release and lifecycle-risk changes | Duration and sample gates met; no crash or leak trend |

## Prerequisites

### Access topology

| Host/device | Connection | Expected port | Role |
|---|---|---|---|
| Development host | SSH to `hermes-pi.local` or `hermes-pi` | N/A | Build, deploy, collect evidence |
| Raspberry Pi gateway | USB to T-Deck | `/dev/ttyACM0` | Flash and persistent serial control |
| VM direct T-Deck | Direct USB | `/dev/ttyACM0` or `/dev/ttyUSB0` | Local flash/serial alternative |
| VM direct Heltec V3 | CP2102 USB UART | Usually `/dev/ttyUSB0` | Second mesh node for RF interoperability |

Port names are not identities. Before flashing, resolve the target with
`/dev/serial/by-id` and make sure no second board has taken the expected name.

```bash
ssh hermes-pi 'hostname; ls -l /dev/serial/by-id/ 2>/dev/null || true'
ssh hermes-pi 'ls -l /dev/ttyACM0; fuser -v /dev/ttyACM0 2>/dev/null || true'
```

### Development host

Required:

- Git with the MeshCore submodule initialized.
- Python 3 and PlatformIO CLI.
- SSH and SCP access to the gateway.
- Enough free disk space for the ESP32 toolchain and two build environments.

```bash
git submodule update --init --recursive
python3 --version
pio --version
ssh hermes-pi 'hostname'
```

### Raspberry Pi gateway

Use the existing `~/hermes-venv` so the flash tool and Python modules are
consistent between sessions:

```bash
ssh hermes-pi '~/hermes-venv/bin/python -m pip install --upgrade \
  esptool pyserial Pillow numpy'
ssh hermes-pi '~/hermes-venv/bin/esptool version'
```

The account must be able to open the serial port. Prefer membership in the
gateway's serial group over world-writable device permissions:

```bash
ssh hermes-pi 'id; stat -c "%A %U %G %n" /dev/ttyACM0'
```

Only one process may own `/dev/ttyACM0`. Stop serial monitors, loggers, capture
jobs, and stale test scripts before flashing.

### Physical preparation

- Use a known-good data cable, not a charge-only cable.
- Connect a suitable antenna before enabling LoRa transmission.
- Keep the T-Deck battery charged or use a powered USB hub. Check the Pi for
  undervoltage before diagnosing a repeating watchdog reset.
- Insert the SD card only when the test requires it; record its format and test
  data. GPS tests should record whether they were indoors or outdoors.
- Record the T-Deck revision when known and the identity of every second radio.
- Agree on a dedicated, legally permitted test frequency and power. Do not use
  the live UK mesh frequency for uncoordinated test traffic.

```bash
ssh hermes-pi 'vcgencmd get_throttled 2>/dev/null || true'
```

`throttled=0x0` is clean. Any undervoltage history should be resolved with a
better power supply or powered hub before interpreting boot stability.

## Standard Test Protocol

### Before Phase 1: identify the run

Record the commit, worktree state, environment, fixture, port, intended RF
parameters, and whether Launcher was previously active.

```bash
git rev-parse HEAD
git status --short
git submodule status
```

Do not describe a dirty build as validation of the recorded commit without also
recording the diff. Save logs, images, hashes, and summaries in a unique run
directory on the Pi.

### Phase 1: Native tests

Run the complete host-side suite before consuming fixture time:

```bash
pio test -e native_test -v
```

Run a focused module first when iterating, but the focused result never replaces
the full suite:

```bash
pio test -e native_test -f test_keyboard -v
```

Pass criteria:

- PlatformIO exits 0.
- No newly failing or unexpectedly skipped module.
- The total is reported from this run rather than copied from old documentation.

Stop here on failure. Hardware testing cannot turn a native failure into a pass.

### Phase 2: Build verification

Build both the production image and the canonical controllable image. PlatformIO
uses a shared `.pio/build` tree, so do not run competing builds in parallel.

```bash
pio run -e SigurdOS_TDeck
pio run -e SigurdOS_TDeck_remote_test_radio

test -s .pio/build/SigurdOS_TDeck/firmware-merged.bin
test -s .pio/build/SigurdOS_TDeck_remote_test_radio/firmware-merged.bin

sha256sum \
  .pio/build/SigurdOS_TDeck/firmware-merged.bin \
  .pio/build/SigurdOS_TDeck_remote_test_radio/firmware-merged.bin
```

Pass criteria:

- Both environments compile and link with no new warning that affects safety or
  test validity.
- Both `firmware-merged.bin` files exist and are non-empty.
- Artifact hashes are saved with the run evidence.

### Phase 3: Smoke test

Flash `SigurdOS_TDeck_remote_test_radio` unless the change specifically requires
another environment. This image combines real radio/mesh initialization with the
lowercase test controller.

```bash
scp .pio/build/SigurdOS_TDeck_remote_test_radio/firmware-merged.bin \
  hermes-pi:/tmp/sigurdos-hw-test.bin

ssh hermes-pi \
  "~/hermes-venv/bin/esptool --chip esp32s3 --port /dev/ttyACM0 \
   --baud 921600 --before default-reset --after hard-reset \
   write-flash 0x0 /tmp/sigurdos-hw-test.bin"
```

If Launcher was active, physically unplug and reconnect USB after flashing. Wait
for the complete radio boot, which can take substantially longer than a UI-only
build. Open one serial connection and keep it open for the following sequence:

```text
help
screen
status
nav home
screen
nav settings
screen
capture
```

The optional smoke harness runs the same class of checks:

```bash
ssh hermes-pi \
  "python3 /tmp/tdeck_test_suite.py --smoke --pi-mode \
   --outdir /tmp/sigurdos-smoke"
```

Pass criteria:

- Boot completes without `Invalid image block`, watchdog churn, panic, abort,
  stack overflow, or an unexpected reboot.
- `help`, `screen`, and `status` return controller output.
- Home and Settings navigation are confirmed.
- A capture contains the header, the expected byte count, and `[capture] END`.
- Home and Settings screenshots are visually inspected, not merely generated.

An `I2C Error 263` during the first few cold-boot probes is harmless only when
it stops within five seconds and keyboard initialization and input succeed.

### Phase 4: UI navigation

Exercise every screen that the current test controller can open, using the same
persistent serial session:

```text
home chat contacts channels network heard map advertise settings trace terminal
signal radio onboarding nodestatus telemetry repeaters system wifinetworks
nodestats regions s-display s-radio s-gps bluetooth
```

`network` is the Finder screen. Named detail screens require state and a name:

```text
nav contactdetail Alice
nav repeaterdetail "Test Repeater"
```

For each applicable screen:

1. Send `nav <name>` and wait for `[test] nav -> <name>`.
2. Send `screen` when the transition uses `navigate_to()`.
3. Watch for crash signatures, loss of serial response, and allocation errors.
4. Exercise back navigation and at least one input path relevant to the screen.
5. Capture Home, Settings, every changed screen, and any visually suspicious
   screen. A complete capture takes about 30 seconds, so do not capture every
   unchanged screen inside the five-minute navigation gate.

Screens opened directly with `show_screen()` may not update `screen`; use a
capture or render activity to verify those transitions. For scrollable lists,
prefer `scrolllist <pixels>` over injected trackball input unless that screen
explicitly handles the trackball.

Pass criteria:

- Every applicable target opens and remains responsive.
- Back navigation works and does not expose a deleted or blank screen.
- No screen shows corruption, duplicated regions, missing chrome, or an all-black
  framebuffer.
- No unbounded decline is visible in repeated `status` samples.

### Phase 5: Feature-specific tests

Run every row affected by the change. Record `not applicable` for the others;
do not leave the scope ambiguous.

#### Radio and mesh

Use `SigurdOS_TDeck_remote_test_radio` and a second, compatible MeshCore node.
Confirm both devices have the same frequency, bandwidth, spreading factor, and
coding rate. Use a unique message token and prove both directions.

```text
getrf
setrf <approved_mhz> <sf> <bw_khz> <cr> <tx_dbm>
reboot
getrf
addchannel hwtest
sendchannel hwtest tdeck-to-peer-<unique-token>
advert
```

Required evidence:

- The applied parameters are reported after reboot.
- The second node receives the T-Deck message.
- The T-Deck receives a distinct message or advert from the second node.
- Packet counters or logs advance and RSSI/SNR values are plausible.
- The antenna was attached and the chosen test frequency was authorized.

For a Heltec V3 terminal-chat peer at `/dev/ttyUSB0`, terminate its commands
with carriage return (`\r`), not newline alone.

#### Chat, contacts, and room servers

```text
inject Alice hello-from-alice
inject Alice channel=hwtest channel-message
opendm Alice
sendmessage Alice reply-from-tdeck
addrepeater TestRepeater
addroomserver TestRoom
setlogin TestRoom 1
loginstat TestRoom
fetchmsgs TestRoom 0
```

Use real RF-discovered contacts for persistence and interoperability claims.
Injected test contacts prove UI paths, not radio discovery or durable contact
storage. `term-submit` is not a supported controller command; Terminal screen
commands must be typed with the physical keyboard.

#### Keyboard, trackball, and touch

```text
keydiag
inputdiag
type HardwareTest123
press enter
tb up
tb down
tb left
tb right
tb click
tap 160 120
```

Injected events prove the firmware path downstream of the injection hook. They
do not prove the physical keyboard MCU, GT911 touch controller, or GPIO switch.
Physically type, touch, and move/click the trackball when those drivers changed,
then compare `keydiag` or `inputdiag` counters before and after.

#### Settings and persistence

Change one safe setting, reboot, and show that it survives. Restore the previous
value after the test. For radio settings, do not substitute a real network
frequency merely to make the test convenient.

```text
getrf
setrf <approved_mhz> <sf> <bw_khz> <cr> <tx_dbm>
reboot
getrf
```

Capture the relevant Settings screen before and after. A successful command
response without a post-reboot readback is not persistence evidence.

#### Display and UI

Capture every changed state, including empty, populated, error, and dialog
states where applicable. Verify dimensions, text, theme, focus, clipping,
scrolling, and return navigation. Use a targeted vision prompt for each image.

#### SD card and map

Record the card format and fixture. Navigate to Map, load a known tile, pan or
zoom, and verify that radio remains functional because LoRa, display, and SD
share SPI. A missing optional card is not a pass for an SD-specific change.

#### GPS

Use `gpsdiag` for the integrated build and the dedicated validation environments
when parser/UART behavior changed. Record test location and sky view. Valid NMEA
without a fix may be expected indoors; it does not prove outdoor acquisition.

```text
gpsdiag
```

#### BLE companion

Use `SigurdOS_TDeck_ble_agent` for agent-driven BLE validation:

```text
ble status
ble pin
ble off
ble on
```

Advertising and PIN evidence alone does not prove full official-app protocol
parity. Exercise the affected companion command with a real client or the
MeshCore Python harness. The `SigurdOS_TDeck_companion_usb` environment is an
experimental diagnostic build and is not the standard BLE validation path.

#### Wi-Fi, power, buzzer, and other peripherals

Exercise the real peripheral and its error path. Record network/privacy-sensitive
details safely, test display sleep/wake after the configured timeout, and verify
that a wake does not generate phantom repeated input. Do not infer physical
behavior from a mocked or injected response.

### Phase 6: Soak test

Run this phase before a release and for changes involving navigation lifetime,
LVGL allocation, queues, radio loops, storage, power, or background timers.
Use 30 minutes for a focused regression and two hours for release evidence.

Flash a debug-family build that emits `[stat]` lines, then deploy and run the
persistent monitor:

```bash
scp ~/.hermes/skills/devops/tdeck-hardware-test-suite/scripts/persistent_soak.py \
  hermes-pi:/tmp/

ssh hermes-pi \
  "python3 /tmp/persistent_soak.py --port /dev/ttyACM0 \
   --duration 1800 --outdir /tmp/sigurdos-soak-30m"

ssh hermes-pi \
  "python3 /tmp/persistent_soak.py --port /dev/ttyACM0 \
   --duration 7200 --outdir /tmp/sigurdos-soak-2h"
```

Do not use a harness mode that opens a new connection for every `status` poll;
that measures repeated boot behavior, not runtime stability. Do not call DTR or
send periodic commands during a `[stat]` soak.

Pass criteria:

- At least 90% of the requested duration completes.
- Regular `[stat]` samples are present with no unexpected reconnect pattern.
- No panic, abort, watchdog reset, stack overflow, or reboot signature appears.
- The ordinary settled free-heap range is approximately 184 bytes or less.
  Investigate variance over 500 bytes.
- A monotonic free-heap loss or a range of 1000 bytes or more fails the default
  leak gate unless the run documents a bounded, intentional allocation.
- PSRAM settles after warm-up and does not decline monotonically.
- The device remains responsive and the final screen capture is valid.

## Test Commands Reference

### Command routing and line endings

The remote-test controller owns serial input whenever `SIGURDOS_REMOTE_TEST=1`.
It accepts lowercase commands terminated by `\r`, `\n`, or `\r\n`. In that
mode, uppercase display commands do not reach `display.cpp`.

Stock environments in this checkout do not enable
`SIGURDOS_SERIAL_DEBUG_COMMANDS`; therefore production/debug builds should not
be assumed to accept any interactive serial command. When a custom build
explicitly sets `-D SIGURDOS_SERIAL_DEBUG_COMMANDS=1`, use the uppercase handler
documented below.

### Remote-test controller: navigation and state

| Command | Purpose | Example |
|---|---|---|
| `help` or `?` | Print the controller menu | `help` |
| `nav <screen>` or `navigate <screen>` | Navigate to a registered screen | `nav settings` |
| `nav contactdetail <name>` | Open a contact detail view | `nav contactdetail Alice` |
| `nav repeaterdetail <name>` | Open a repeater view; quoted names supported | `nav repeaterdetail "Test Repeater"` |
| `back` | Pop navigation history | `back` |
| `screen` | Report `current_screen()` | `screen` |
| `status` | Report heap, PSRAM, LVGL pool, and stack metrics | `status` |
| `stresschat [1..1000]` | Repeated Chat/Home lifecycle stress; default 50 | `stresschat 100` |
| `stresschat cancel` | Stop the lifecycle stress job | `stresschat cancel` |

Registered `nav` names are:

```text
home chat contacts channels network heard map advertise settings trace terminal
signal radio onboarding contactdetail nodestatus telemetry repeaters system
wifinetworks nodestats regions s-display s-radio s-gps bluetooth
```

### Remote-test controller: input and visual diagnostics

| Command | Purpose | Example |
|---|---|---|
| `tb <dir>` or `trackball <dir>` | Inject `up/down/left/right/click`; `u/d/l/r/c` aliases | `tb c` |
| `scrolllist <pixels>` | Scroll first list; positive is down | `scrolllist 120` |
| `type <utf8-text>` | Queue text through keyboard injection | `type Hello T-Deck` |
| `picker <ascii-char>` | Inject the configured character-picker codepoint | `picker a` |
| `press <key>` | Inject `enter`, `backspace`/`bksp`, `escape`/`esc`, or `tab` | `press enter` |
| `tap <x> <y>` | Inject a touch in 320 x 240 LVGL coordinates | `tap 160 120` |
| `backlight [0..255]` | Read on/off state or set brightness | `backlight 128` |
| `capture` | Queue an RGB565 LVGL snapshot | `capture` |
| `capture cancel` | Cancel an active capture | `capture cancel` |
| `tree` | Dump a bounded LVGL object tree | `tree` |
| `tree cancel` | Cancel an active tree dump | `tree cancel` |
| `widgets` | List visible text widgets and coordinates | `widgets` |
| `emoji` | Open the paged emoji visual test | `emoji` |
| `emoji-ac <prefix>` | Print emoji autocomplete matches | `emoji-ac smil` |
| `keydiag` or `kbddiag` | Dump physical keyboard diagnostics | `keydiag` |
| `inputdiag`, `touchdiag`, `trackballdiag`, or `tbdiag` | Dump touch and trackball diagnostics | `inputdiag` |
| `gpsdiag` | Dump GPS UART, NMEA, and fix diagnostics | `gpsdiag` |
| `contactstats` | Count stored/exported contacts by type | `contactstats` |

`tree` coordinates for nested labels are relative to their parents. Do not use
them as absolute tap coordinates. Capture the screen or inspect the containing
widget first.

### Remote-test controller: messaging and server state

| Command | Purpose | Example |
|---|---|---|
| `inject ...` or `msg ...` | Inject a DM or channel message | `inject Alice channel=hwtest hello` |
| `sendchannel <channel> <text>` | Send a real channel message when mesh is active | `sendchannel hwtest hello` |
| `sendmessage <contact> <text>` or `senddm ...` | Send a DM; quote a spaced name | `sendmessage "Test Room" hello` |
| `opendm <contact>` | Open a DM conversation | `opendm Alice` |
| `addchannel <name> [psk]` or `addchan ...` | Add a hashtag channel or PSK channel | `addchannel hwtest` |
| `removechannel <index-or-name>` | Remove a channel | `removechannel hwtest` |
| `rmchannel`, `removechan`, `rmchan` | Aliases for `removechannel` | `rmchan 1` |
| `addrepeater <name>` | Inject a test repeater contact | `addrepeater TestRepeater` |
| `addroomserver <name>` | Inject a test room-server contact | `addroomserver TestRoom` |
| `login <name> <password>` | Send a real room login request | `login "Test Room" password` |
| `setlogin <name> [perm]` | Force successful login state; default permission 1 | `setlogin TestRoom 1` |
| `setloginguest <name>` | Force guest permission 2 | `setloginguest TestRoom` |
| `loginstat <name>` | Report login status and permission | `loginstat TestRoom` |
| `fetchmsgs <name> [channel]` | Request room messages; channel defaults to `0` | `fetchmsgs TestRoom 0` |
| `acmd <name>` | Open the admin-command UI for a contact | `acmd TestRepeater` |
| `advert` | Send an advert when radio/mesh is active | `advert` |

`SigurdOS_TDeck_remote_test` deliberately has no radio. Mesh sends and live
login operations require `SigurdOS_TDeck_remote_test_radio` or another
radio-enabled controller environment.

### Remote-test controller: radio, system, BLE, and debug

| Command | Purpose | Example |
|---|---|---|
| `getrf` | Report saved RF settings, live signal/counters, and profile | `getrf` |
| `setrf <freq> <sf> <bw> <cr> <power>` | Save validated RF values to NVS; reboot required | `setrf <approved_mhz> 10 250 5 2` |
| `reboot` or `restart` | Restart the ESP32-S3 | `reboot` |
| `factoryreset` or `wipe` | **Destructive:** erase SigurdOS state and reboot | `factoryreset` |
| `ble [status]` | Report BLE availability, enabled/connected state, PIN, name, sync time | `ble status` |
| `ble on` / `ble off` | Enable or disable companion BLE | `ble on` |
| `ble pin` | Print the six-digit pairing PIN | `ble pin` |
| `debug` | Print debug level and feature mask | `debug` |
| `debug <1..3>` | Set quiet, normal, or verbose level | `debug 2` |
| `debug level <1..3>` | Explicit form of level control | `debug level 3` |
| `debug <feature> <0|1>` | Toggle `display`, `mesh`, `ui`, `map`, or `diag` | `debug mesh 0` |
| `debug all <0|1>` | Toggle all runtime debug features | `debug all 0` |

Treat `factoryreset` as destructive. Do not run it on a fixture whose identity,
contacts, channels, or settings have not been backed up or explicitly placed in
scope.

### Structured telemetry commands

These controller verbs produce useful output only when
`SIGURDOS_TELEMETRY=1`, normally in `SigurdOS_TDeck_telemetry` or
`SigurdOS_TDeck_telemetry_diff`.

| Command | Purpose | Example |
|---|---|---|
| `telemetry` | Show telemetry enabled state, interval, and tick count | `telemetry` |
| `telemetry on` / `off` | Enable or disable periodic telemetry | `telemetry on` |
| `telemetry diff on|off` | Toggle diff emission | `telemetry diff on` |
| `telemetry hb <1000..60000>` | Set heartbeat milliseconds | `telemetry hb 5000` |
| `telemetry full` | Emit a complete heartbeat immediately | `telemetry full` |
| `query <name>` | Emit one structured query | `query heap` |
| `drift` | Report all active drift detectors | `drift` |
| `crash` or `crash report` | Report retained crash data | `crash report` |
| `crash clear` | Clear retained crash data | `crash clear` |
| `crash test` | **Deliberately crashes the device** | `crash test` |

Supported queries are:

```text
build state heap lvgl mesh crash crash clear crash test drift screen wifi gps
radio sd nvs temp task hb-ring pktlog full
```

Run `crash test` only in an explicitly destructive crash-recovery test. It is
never part of an ordinary smoke or soak run.

### Optional uppercase display handler

When a non-remote build is explicitly compiled with
`SIGURDOS_SERIAL_DEBUG_COMMANDS=1`, `display.cpp` accepts exactly these commands:

| Command | Purpose | Example |
|---|---|---|
| `NAV <screen>` | Navigate using the display handler's table | `NAV settings` |
| `SCREENSHOT` | Stream an RGB565 snapshot | `SCREENSHOT` |
| `SEND <channel> <text>` | Send a channel message | `SEND hwtest hello` |

The `NAV` comparison is case-insensitive, but the command prefix is uppercase.
Its target names are:

```text
home chat contacts channels network heard map advertise settings trace terminal
signal radio repeaters onboarding s-radio s-gps s-display s-system packets
node-status telemetry
```

In this handler, `packets` currently maps to the same enum as `network`; use the
remote controller for authoritative screen automation. A custom production
capture must record the extra build flag so the result is reproducible.

## Build Environment Reference

The table reflects `platformio.ini` in this checkout. “Debug” means the notable
diagnostic specialization, not merely the compiler optimization level.

| Environment | Target | Radio | Controller | BLE | Debug/validation purpose |
|---|---|---:|---:|---:|---|
| `SigurdOS_TDeck` | T-Deck | Yes | No | Yes | Production/release baseline |
| `SigurdOS_TDeck_ble_validation` | T-Deck | Yes | No | Yes | BLE validation instrumentation |
| `SigurdOS_TDeck_ble_agent` | T-Deck | Yes | Yes | Yes | Remote radio controller plus BLE agent validation |
| `SigurdOS_TDeck_meshv2` | T-Deck | Yes | No | Yes | Explicit Mesh V2 build |
| `SigurdOS_TDeck_trackball_debug` | T-Deck | Yes | No | Yes | Trackball raw/shadow diagnostics |
| `SigurdOS_TDeck_debug` | T-Deck | Yes | No | Yes | Full debug, crash ring, periodic `[stat]` output |
| `SigurdOS_TDeck_debug_869` | T-Deck | Yes | No | Yes | Full debug preset at 869.525/SF10/BW250 |
| `SigurdOS_TDeck_debug_display` | T-Deck | Yes | No | Yes | Display-specific compile-time logging |
| `SigurdOS_TDeck_debug_mesh` | T-Deck | Yes | No | Yes | Mesh-specific compile-time logging |
| `SigurdOS_TDeck_debug_ui` | T-Deck | Yes | No | Yes | UI-specific compile-time logging |
| `SigurdOS_TDeck_debug_map` | T-Deck | Yes | No | Yes | Map-specific compile-time logging |
| `SigurdOS_TDeck_debug_diag` | T-Deck | Yes | No | Yes | Diagnostic feature flag; not the full debug module |
| `SigurdOS_TDeck_telemetry` | T-Deck | No | Yes | Compiled, no live mesh | Structured telemetry and query controller |
| `SigurdOS_TDeck_telemetry_diff` | T-Deck | No | Yes | Compiled, no live mesh | Diff-based structured telemetry |
| `SigurdOS_TDeck_remote_test` | T-Deck | No | Yes | Compiled, no live mesh | UI/input automation and screenshots |
| `SigurdOS_TDeck_remote_test_radio` | T-Deck | Yes | Yes | Yes | Canonical physical fixture and bidirectional RF testing |
| `SigurdOS_TDeck_remote_test_radio_testfreq` | T-Deck | Yes | Yes | Yes | Alias of the canonical remote-radio preset |
| `SigurdOS_TDeck_remote_test_radio_roomtest` | T-Deck | Yes | Yes | Yes | Room-server preset, SF11 |
| `SigurdOS_TDeck_remote_test_radio_usca` | T-Deck | Yes | Yes | Yes | USA/Canada 915 MHz, BW62.5 validation preset |
| `SigurdOS_TDeck_remote_test_radio_usca_rxonly` | T-Deck | Receive only | Yes | Yes | USA/Canada receive-only validation; TX blocked |
| `SigurdOS_TDeck_remote_test_radio_meshv2` | T-Deck | Yes | Yes | Yes | Remote-radio controller plus explicit Mesh V2 |
| `SigurdOS_TDeck_companion_usb` | T-Deck | Yes | No | No | Experimental USB companion build; known native-USB conflict risk |
| `SigurdOS_TDeck_gps_validation` | T-Deck | No | No | No | Minimal GPS UART/NMEA validation harness |
| `SigurdOS_TDeck_gps_validation_wifi` | T-Deck | No | No | No | GPS validation with Wi-Fi co-location instrumentation |
| `native_test` | Host | N/A | N/A | N/A | GoogleTest suite |
| `native_sanitize` | Host | N/A | N/A | N/A | Address/undefined-behavior sanitizers |
| `native_coverage` | Host | N/A | N/A | N/A | Coverage instrumentation |

Every T-Deck environment inherits the merged-image post-build action unless it
overrides the base scripts. Always inspect the requested environment's build
directory and flash its own `firmware-merged.bin`.

## Troubleshooting Guide

### Port opens but returns zero bytes

Check these causes in order:

1. Production firmware may be healthy and intentionally silent after boot.
2. Another process may hold the port: `fuser -v /dev/ttyACM0`.
3. A killed process may have left USB CDC in a dead state.
4. The device may need a cold power cycle after Launcher.
5. The Pi may be undervoltage-throttled or the cable may be power-only.

Recover USB CDC with esptool, then wait for a full boot before reopening:

```bash
ssh hermes-pi 'fuser -k /dev/ttyACM0 2>/dev/null || true; sleep 3'
ssh hermes-pi \
  '~/hermes-venv/bin/esptool --chip esp32s3 --port /dev/ttyACM0 \
   --before default-reset chip-id'
```

Do not add `setDTR()` to the recovery script. If recovery still yields zero
bytes, physically unplug and reconnect the T-Deck and re-check Pi power.

### Commands return nothing

- Confirm the flashed image and command dialect. `help`, `nav`, `status`, and
  `capture` require a remote-test controller build.
- In a remote-test build, uppercase `NAV`/`SCREENSHOT` are intercepted and do
  not reach `display.cpp`.
- In a production/debug build, the uppercase handler exists only when
  `SIGURDOS_SERIAL_DEBUG_COMMANDS=1` was explicitly compiled.
- Wait for full boot before sending the first command. Radio builds take longer
  than UI-only builds.
- Keep the port open; a command sent during a reset is lost.
- After erased state, onboarding may block normal navigation. Configure safe RF
  preferences or complete onboarding before retrying.

### Radio never initializes or sends fail

- `SigurdOS_TDeck_remote_test` intentionally has no LoRa radio. Flash
  `SigurdOS_TDeck_remote_test_radio`.
- A production build with unconfigured preferences safely holds the SX1262 in
  reset and prints `[mesh] Radio not configured — holding SX1262 in reset`.
  Complete onboarding/Settings, or use `setrf` in the remote-radio build.
- Run `getrf` and confirm the post-reboot frequency, SF, BW, CR, and power.
- Confirm an antenna is connected and that both nodes use identical parameters.
- If `addchannel` says `FAILED`, the channel may already exist; verify by name
  and attempt the intended send.
- Filter repeated `SPIFFS Already Mounted` warnings before evaluating the actual
  mesh response; the warning spam is not itself a radio failure.

### Boot loop or `Invalid image block`

`firmware.bin` was probably flashed at `0x0`. Rebuild and flash the merged image:

```bash
scp .pio/build/SigurdOS_TDeck_remote_test_radio/firmware-merged.bin \
  hermes-pi:/tmp/recovery.bin
ssh hermes-pi \
  "~/hermes-venv/bin/esptool --chip esp32s3 --port /dev/ttyACM0 \
   --baud 921600 write-flash 0x0 /tmp/recovery.bin"
```

Do not force QIO flash mode. The merged image preserves the DIO bootloader
header required by affected T-Deck units.

### Repeating `TG0WDT_SYS_RST` before application output

If every known-good image shows the same ROM reset with no SigurdOS banner,
suspect power starvation during PSRAM initialization:

```bash
ssh hermes-pi 'vcgencmd get_throttled; vcgencmd measure_volts core'
```

Use a 5 V/3 A or better Pi supply or a powered USB hub. Do not classify the
reset as a firmware regression until power has been ruled out.

### New image flashes, but old Launcher image still boots

An esptool reset may not change the selected flash after Launcher handoff.
Physically unplug and reconnect the USB cable. Confirm the expected build from
its boot timing and test-controller response after the cold power cycle.

### `I2C Error 263` on cold boot

Up to a few early timeouts can occur while the independent keyboard ESP32-C3
starts. Treat them as harmless only when they stop within five seconds and the
keyboard is usable. Persistent errors, missing input readiness, or simultaneous
touch failures indicate a real shared-I2C problem.

### Screenshot is truncated, corrupt, or fails hex conversion

- Read with repeated `read(in_waiting)`/`read(4096)` calls until
  `[capture] END`; never use `readline()` for the capture stream.
- Allow at least 45 to 60 seconds at 115200 baud.
- Parse `W`, `H`, and `S` from the header and require exactly `H * S` decoded
  bytes. Do not pad a short frame and call it a pass.
- Keep only complete `[cdata]` payloads and filter non-hex serial noise before
  `bytes.fromhex()`.
- At this commit both capture paths request RGB565 and normally report
  `W=320 H=240 S=640`, or 153,600 bytes / 307,200 hex characters / 4,800 data
  lines. Still derive bytes-per-pixel from `S / W` for future compatibility.
- Heavy mesh/display debug output can starve a capture. Temporarily reduce
  runtime debug features or use the UI-only remote-test build for visual work.

### `capture` or navigation reports an unknown command/screen

Run `help` and compare the build to this document. A new screen must be present
in the navigation enum/dispatch, the remote controller table, and any optional
display-handler table. Do not reinterpret an unknown response as a successful
transition.

### Production soak reads no `[stat]` lines

That is expected. Production builds have no periodic debug output. Use
`SigurdOS_TDeck_debug`, `SigurdOS_TDeck_remote_test`, or
`SigurdOS_TDeck_remote_test_radio` for `[stat]`-based runtime measurements. A
production soak must use external liveness evidence and a final interaction; it
cannot use absence of serial text as a failure condition.

### `SPIFFS Already Mounted` floods the log

Filter it for readability while preserving the unfiltered raw log:

```bash
grep -v 'SPIFFS Already Mounted' raw-serial.log > filtered-serial.log
```

This warning is noisy but not, by itself, a test failure.

### USB companion build stops after ROM entry

The ESP32-S3 native USB JTAG/CDC combination can conflict with the USB companion
transport. Use BLE (`SigurdOS_TDeck_ble_agent`) for standard companion hardware
testing. Do not claim a USB companion regression from this signature without a
dedicated UART transport setup.

## Pi Gateway Reference

### Connect and identify devices

```bash
ssh hermes-pi.local
ssh hermes-pi

ssh hermes-pi 'ls -l /dev/serial/by-id/ 2>/dev/null || true'
ssh hermes-pi 'ls -l /dev/ttyACM0 /dev/ttyUSB0 2>/dev/null || true'
ssh hermes-pi 'fuser -v /dev/ttyACM0 2>/dev/null || true'
```

The standard gateway T-Deck port is `/dev/ttyACM0`. On a VM, a T-Deck can be
`/dev/ttyACM0` or `/dev/ttyUSB0`; a Heltec V3 is normally `/dev/ttyUSB0`.
Resolve ambiguity before issuing a flash command.

### Deploy artifacts and scripts

```bash
scp .pio/build/SigurdOS_TDeck_remote_test_radio/firmware-merged.bin \
  hermes-pi:/tmp/sigurdos-hw-test.bin
scp script.py hermes-pi:/tmp/
ssh hermes-pi 'python3 /tmp/script.py --args'
```

Use unique names under `/tmp` when multiple runs are active. Save the local and
remote SHA-256 values if artifact identity matters:

```bash
sha256sum .pio/build/SigurdOS_TDeck_remote_test_radio/firmware-merged.bin
ssh hermes-pi 'sha256sum /tmp/sigurdos-hw-test.bin'
```

### Flash

```bash
ssh hermes-pi \
  "~/hermes-venv/bin/esptool --chip esp32s3 --port /dev/ttyACM0 \
   --baud 921600 --before default-reset --after hard-reset \
   write-flash 0x0 /tmp/sigurdos-hw-test.bin"
```

The `0x0` target requires the merged image. An app-only image is valid only for
an explicitly planned app-offset update and is outside this standard fixture
protocol.

### Recovery sequence

1. Stop the current serial owner and wait three seconds.
2. Run esptool `chip-id` to restore USB CDC.
3. Wait for a full boot before opening the persistent monitor.
4. If Launcher was active or the old image still boots, cold power-cycle USB.
5. If watchdog churn persists across images, check Pi power and the cable.
6. Reflash the correct merged artifact and record its hash.

```bash
ssh hermes-pi 'fuser -k /dev/ttyACM0 2>/dev/null || true; sleep 3'
ssh hermes-pi \
  '~/hermes-venv/bin/esptool --chip esp32s3 --port /dev/ttyACM0 \
   --before default-reset chip-id'
```

Killing a process is a recovery action, not normal flow. Prefer scripts that
close the port cleanly, and run long jobs on the Pi with their results written to
Pi-side files.

## Test Result Interpretation

### Controller and boot responses

| Evidence | Interpretation |
|---|---|
| Controller banner and `help` menu | Correct remote-test family is running |
| `[test] current screen: home` | Controller and `current_screen()` respond |
| `[test] nav -> settings` | Navigation command was accepted, not necessarily visually correct |
| `[test] heap=... psram=...` | On-demand controller state sample |
| `[mesh] SigurdMeshV2 initialized` | Mesh initialization completed in a mesh-debug build |
| `[mesh] Radio not configured ...` | Production safety gate is active; no radio test is valid yet |
| `[capture] ...` followed by `[capture] END` | Transfer completed; byte count and image still require validation |

### `[stat]` lines

Example:

```text
[stat] t=1599  heap=168864/163372  psram=7949451  batt=100%  flush=0  feat=1f  lvgl=...
```

- `t` is uptime in seconds.
- The first heap number is current free internal heap.
- The second heap number is minimum free internal heap since boot.
- `psram` is current free PSRAM.
- `batt` is the estimated battery percentage.
- `feat` is the runtime debug-feature mask.
- The trailing LVGL fields describe pool free/total/largest, used percentage,
  and fragmentation.

Allow initial LVGL and subsystem warm-up. A settled free-heap range around 184
bytes is normal. Flag range over 500 bytes for investigation and fail a monotonic
loss or default range at/above 1000 bytes unless a bounded allocation is proven.
A sudden large recovery can indicate a reboot, not a miraculous leak fix.

### Crash and reset signatures

Treat these as failures unless the test explicitly triggered and verified crash
recovery:

```text
FATAL
PANIC
Guru Meditation
abort() was called
assert failed
Stack overflow
Backtrace
rst:0x
TG0WDT_SYS_RST
SW_CPU_RESET
Invalid image block
PREVIOUS BOOT ENDED IN A CRASH
```

Save the complete boot/reset block, not only the matching line. Distinguish a
single expected reset caused by flashing or the initial port open from an
uncommanded runtime reset.

### Screenshot quality

A screenshot passes only when:

- Header dimensions and stride are valid and `decoded_bytes == H * S`.
- `[capture] END` was received before timeout.
- The image opens at 320 x 240 and is not padded from a short transfer.
- Top and bottom chrome, content, fonts, colors, and focus are plausible.
- There is no static/noise band, repeated horizontal content, stale previous
  screen, all-black area, clipping, or unintended overlap.
- The image is visually inspected with a targeted prompt. File existence, PNG
  decode success, or a nonzero file size is insufficient.

## Agent-Specific Instructions

AI agents must apply the following evidence standard:

1. Read this protocol and `CONTRIBUTING.md` before using the fixture.
2. Confirm the exact commit, dirty state, environment, artifact hash, gateway,
   port, device revision, and authorized RF parameters.
3. Never claim a physical input, RF exchange, persistence result, screenshot
   review, or soak duration that was not actually observed.
4. Use one persistent serial session. Do not generate false soak samples by
   reopening the port for each command, and never call `setDTR()`.
5. For a firmware-affecting PR, build, flash, verify a clean boot, navigate at
   least Home and Settings, capture both, and inspect both images. Run every
   feature-specific test implied by the diff.
6. Use a vision-capable model for changed-screen captures. Ask a specific
   question such as: “Is the 4 x 3 Home grid complete, with no clipping,
   duplicated bands, or corrupt pixels?” Use pixel analysis only as a documented
   fallback, not as an unstated replacement.
7. Preserve raw logs and raw captures. Filtering noise is acceptable only when
   the unfiltered evidence is retained.
8. A docs-only or native-test-only PR may record hardware testing as not
   applicable. Do not flash merely to manufacture evidence unrelated to the
   change.
9. Report blockers and partial coverage explicitly. “Not run” is more useful
   than an inferred pass.

### Vision review prompts

Use one focused prompt per changed state. Examples:

```text
Confirm the Home screen has twelve distinct tiles in a 4 x 3 grid, intact top
and bottom bars, legible labels, and no duplicated or noisy framebuffer region.

Confirm the Settings list is fully rendered inside the content area, with no
clipped rows, overlapping text, unexpected color, or stale Home-screen pixels.

Compare the before and after captures. Did only the intended control/state
change, while screen chrome, alignment, and unrelated content remain stable?
```

### PR hardware-test comment template

```markdown
## Hardware test — <commit SHA>

- Fixture: LilyGo T-Deck <revision>, gateway `hermes-pi`, port `/dev/ttyACM0`
- Firmware: `<environment>`, merged SHA-256 `<hash>`
- RF: `<frequency/SF/BW/CR/power>` or `not used`
- Native tests: `pio test -e native_test -v` — PASS (`<current result>`)
- Builds: `SigurdOS_TDeck`, `SigurdOS_TDeck_remote_test_radio` — PASS
- Smoke: clean boot; `help`, `screen`, `status`, Home, Settings — PASS
- UI: `<screen list>` — PASS; captures inspected with `<vision/model or fallback>`
- Feature tests: `<commands and peer evidence>`
- Soak: `<duration, sample count, heap range, PSRAM range>` or `not required`
- Evidence: `<log/capture locations or attachments>`
- Result: **PASS / FAIL / PARTIAL**
- Gaps: `<none or exact untested behavior>`
```

For a failed or partial run, include the first failing command, the relevant raw
serial block, whether the device recovered, and whether the fixture was restored
to a known firmware image.
