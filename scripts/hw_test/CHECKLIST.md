# T-Deck Hardware Test Checklist

Copy this file or its contents into the test evidence for one commit. The full
procedure and interpretation rules are in
[`docs/HARDWARE_TESTING.md`](../../docs/HARDWARE_TESTING.md).

## Run identity

- [ ] Upstream issue: `#________`
- [ ] Commit SHA: `________________________________________`
- [ ] Worktree clean, or attached diff recorded
- [ ] T-Deck revision/fixture: `____________________________`
- [ ] Gateway and port: `hermes-pi` / `/dev/ttyACM0`
- [ ] Launcher previously active: `yes / no / unknown`
- [ ] RF transmission needed: `yes / no`
- [ ] Authorized RF parameters: `freq ______ / SF ____ / BW ____ / CR ____ / TX ____`
- [ ] Antenna attached before any RF transmission
- [ ] Evidence directory: `/tmp/____________________________`

```bash
git rev-parse HEAD
git status --short
git submodule status
ssh hermes-pi 'hostname; ls -l /dev/serial/by-id/ 2>/dev/null || true'
ssh hermes-pi 'ls -l /dev/ttyACM0; fuser -v /dev/ttyACM0 2>/dev/null || true'
ssh hermes-pi 'vcgencmd get_throttled 2>/dev/null || true'
```

- [ ] Correct physical port resolved; no other board will be overwritten
- [ ] No stale process owns `/dev/ttyACM0`
- [ ] Pi power health is clean, or power issue resolved before testing

## Phase 1 — Native tests

```bash
pio test -e native_test -v
```

- [ ] Exit code 0
- [ ] Current pass/skip/fail totals recorded: `________________________`
- [ ] No new unexpected skip

## Phase 2 — Build verification

```bash
pio run -e SigurdOS_TDeck
pio run -e SigurdOS_TDeck_remote_test_radio

test -s .pio/build/SigurdOS_TDeck/firmware-merged.bin
test -s .pio/build/SigurdOS_TDeck_remote_test_radio/firmware-merged.bin

sha256sum \
  .pio/build/SigurdOS_TDeck/firmware-merged.bin \
  .pio/build/SigurdOS_TDeck_remote_test_radio/firmware-merged.bin
```

- [ ] Release build passes
- [ ] Remote-test-radio build passes
- [ ] No new safety/reliability warning
- [ ] Release merged SHA-256: `________________________________________`
- [ ] Test merged SHA-256: `___________________________________________`

## Phase 3 — Flash and smoke

Only use the trusted flasher with the merged image and the digest recorded above.
It validates ESP32-S3 structure, partition bounds, checksums, and provenance before
writing offset `0x0`:

```bash
python3 scripts/hw_test/hw_flash.py \
  --firmware .pio/build/SigurdOS_TDeck_remote_test_radio/firmware-merged.bin \
  --sha256 <test-merged-sha256-recorded-above> \
  --pi-mode --port /dev/ttyACM0
```

- [ ] Trusted flasher records the expected artifact SHA-256
- [ ] Bootloader, partition table, boot_app0, and application validation passes
- [ ] esptool verifies the write
- [ ] If Launcher was active, USB was physically unplugged/replugged
- [ ] Full radio boot wait completed before first command
- [ ] No `Invalid image block`, panic, watchdog loop, abort, or unexpected reset
- [ ] Cold-boot `I2C Error 263`, if present, stops within five seconds

Deploy fixture helpers when they are installed:

```bash
test -f ~/.hermes/scripts/tdeck_test_suite.py
test -f ~/.hermes/scripts/td_cmd.py

scp ~/.hermes/scripts/tdeck_test_suite.py hermes-pi:/tmp/
scp ~/.hermes/scripts/td_cmd.py hermes-pi:/tmp/
scp ~/.hermes/skills/devops/tdeck-hardware-test-suite/scripts/batch_capture_rt.py \
  hermes-pi:/tmp/
```

Run the smoke flow:

```bash
ssh hermes-pi \
  "python3 /tmp/tdeck_test_suite.py --smoke --pi-mode \
   --outdir /tmp/sigurdos-smoke"
```

Required controller sequence if the helper is unavailable:

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

- [ ] `help` returns the remote controller menu
- [ ] `screen` and `status` return valid values
- [ ] Home navigation passes
- [ ] Settings navigation passes
- [ ] Capture includes valid header, exact `H * S` bytes, and `[capture] END`
- [ ] No serial connection was reopened between commands
- [ ] No script called `setDTR()`

## Phase 4 — UI navigation and visual review

Use one persistent serial connection. Exercise these applicable targets:

```text
home chat contacts channels network heard map advertise settings trace terminal
signal radio onboarding nodestatus telemetry repeaters system wifinetworks
nodestats regions s-display s-radio s-gps bluetooth
```

Optional installed harness:

```bash
ssh hermes-pi \
  "python3 /tmp/tdeck_test_suite.py --ui --iterations 1 --pi-mode \
   --outdir /tmp/sigurdos-ui"
```

Capture Home and Settings in one persistent batch session:

```bash
ssh hermes-pi 'python3 /tmp/batch_capture_rt.py home settings'
scp hermes-pi:/tmp/scr_home.png /tmp/sigurdos-home.png
scp hermes-pi:/tmp/scr_settings.png /tmp/sigurdos-settings.png
```

- [ ] All applicable screen names accept navigation
- [ ] Back navigation tested
- [ ] No blank screen, hang, allocation failure, crash, or unexpected reboot
- [ ] Home screenshot visually inspected
- [ ] Settings screenshot visually inspected
- [ ] Every changed screen/state captured and visually inspected
- [ ] Vision prompt was specific to layout/content changed by the patch
- [ ] Raw capture/log evidence retained

Vision/model or fallback used: `___________________________________________`

Capture paths: `___________________________________________________________`

## Phase 5 — Feature-specific coverage

Mark every row either tested or not applicable.

- [ ] / [ ] N/A — Radio: exact parameters, antenna, two-way peer evidence
- [ ] / [ ] N/A — Chat/DM/channel message send and receive
- [ ] / [ ] N/A — Contact/repeater/room discovery and persistence
- [ ] / [ ] N/A — Physical keyboard plus `keydiag`
- [ ] / [ ] N/A — Physical trackball/touch plus `inputdiag`
- [ ] / [ ] N/A — Settings persistence across reboot
- [ ] / [ ] N/A — SD/map with known card/tile
- [ ] / [ ] N/A — GPS/NMEA/fix with test conditions recorded
- [ ] / [ ] N/A — BLE companion affected command, not advertise-only inference
- [ ] / [ ] N/A — Wi-Fi/OTA affected flow
- [ ] / [ ] N/A — Display sleep/wake and backlight
- [ ] / [ ] N/A — Battery/power/buzzer or other changed peripheral

Commands and peer evidence:

```text
<paste exact commands, responses, RF peer message IDs, and observations here>
```

- [ ] Any changed persistent value restored after testing
- [ ] Injected input/contact behavior is not reported as physical/RF proof
- [ ] No uncoordinated transmission occurred on a live mesh frequency

## Phase 6 — Soak, when required

```bash
scp ~/.hermes/skills/devops/tdeck-hardware-test-suite/scripts/persistent_soak.py \
  hermes-pi:/tmp/

# Focused regression: 30 minutes
ssh hermes-pi \
  "python3 /tmp/persistent_soak.py --port /dev/ttyACM0 \
   --duration 1800 --outdir /tmp/sigurdos-soak-30m"

# Pre-release: 2 hours
ssh hermes-pi \
  "python3 /tmp/persistent_soak.py --port /dev/ttyACM0 \
   --duration 7200 --outdir /tmp/sigurdos-soak-2h"
```

- [ ] / [ ] N/A — Soak required for this change
- [ ] Single persistent connection; no poll/reopen loop and no DTR calls
- [ ] Completed at least 90% of target duration
- [ ] Regular `[stat]` samples collected
- [ ] No panic, abort, watchdog, crash, stack overflow, or unexpected reboot
- [ ] Settled free-heap range <= 184 bytes, or explanation attached
- [ ] Free-heap range <= 500 bytes warning gate
- [ ] No monotonic loss or default >= 1000-byte leak failure
- [ ] PSRAM stable after warm-up
- [ ] Final command/navigation and capture prove responsiveness

Soak duration/samples: `__________________________________________________`

Heap start/end/min/max/range: `_____________________________________________`

PSRAM start/end/min/max/range: `____________________________________________`

## Result and handoff

- [ ] Raw serial log retained
- [ ] Native/build logs retained
- [ ] Merged artifact hashes retained
- [ ] Screenshots attached or linked
- [ ] Feature-specific evidence attached or linked
- [ ] Fixture left in known state; final firmware recorded
- [ ] PR comment states PASS, FAIL, or PARTIAL and lists exact gaps

Final result: `PASS / FAIL / PARTIAL`

Final fixture firmware/environment: `_________________________________________`

Evidence locations: `_______________________________________________________`

Notes/blockers:

```text
<first failing command, raw signature, recovery performed, and untested scope>
```
