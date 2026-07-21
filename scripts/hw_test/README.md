# SigurdOS T-Deck hardware tests

This package is the repository-owned entry point for on-device validation. It
replaces the older scripts that lived under `~/.hermes/scripts/` and the
hardware-test skill directory with one persistent-serial runner, one flashing
path, and one report format.

Python 3.11 or newer and `pyserial` are required on the machine that opens the
T-Deck port. The Pi already has the expected serial environment; for local use:

```bash
python3 -m pip install pyserial
```

PNG screenshots are written with the Python standard library. Pillow is not
required.

## Quick start

Run a smoke test through the Pi-connected T-Deck:

```bash
python3 scripts/hw_test/hw_test_runner.py --smoke --pi-mode
```

Run against a directly connected T-Deck. The documented VM default is
`/dev/ttyUSB0`; native USB CDC commonly appears as `/dev/ttyACM0`:

```bash
python3 scripts/hw_test/hw_test_runner.py --smoke --local --port /dev/ttyUSB0
python3 scripts/hw_test/hw_test_runner.py --ui --local --port /dev/ttyACM0 --iterations 5
```

The transport must be explicit. This prevents a locally connected Heltec V3 at
`/dev/ttyUSB0` from being mistaken for the T-Deck.

## Modes

| Mode | Coverage |
|---|---|
| `--smoke` | Detect firmware, query controller state, navigate home/settings, and capture both screens |
| `--ui` | Sweep the standardized screen list and sample heap after each iteration |
| `--radio` | Configure the bench radio profile, reboot, verify it, create a temporary channel, and transmit one low-power packet |
| `--soak` | Monitor `[stat]` telemetry and crashes over one persistent serial session |
| `--full` | Smoke + UI + radio; deliberately excludes a long soak |
| `--all` | Smoke + UI + radio + soak |

With no mode flag the runner defaults to `--smoke`.

The radio phase transmits. Its default bench profile is 868.100 MHz,
SF10/BW250/CR5 at 2 dBm. The operator is responsible for using a legal,
coordinated frequency for the test location:

```bash
python3 scripts/hw_test/hw_test_runner.py --radio --pi-mode \
  --radio-frequency 868.1 --radio-sf 10 --radio-bw 250 \
  --radio-cr 5 --radio-power 2
```

Do not use the UK live mesh frequency (869.525 MHz) for automated test traffic.

## Build and flash

`hw_flash.py` builds any PlatformIO environment and will only flash a file named
`firmware-merged.bin`. It refuses `firmware.bin`, because that app-only image
cannot boot when written at offset zero.

```bash
# Pi-connected device
python3 scripts/hw_test/hw_flash.py \
  --env SigurdOS_TDeck_remote_test_radio --pi-mode

# Direct device
python3 scripts/hw_test/hw_flash.py \
  --env SigurdOS_TDeck_remote_test_radio --local --port /dev/ttyACM0

# Build without flashing
python3 scripts/hw_test/hw_flash.py \
  --env SigurdOS_TDeck_remote_test_radio --build-only --local
```

The Pi path stages the merged image with `scp`, then runs:

```text
~/hermes-venv/bin/esptool --chip esp32s3 --port /dev/ttyACM0 \
  --baud 921600 write-flash 0 firmware-merged.bin
```

After flashing, the tool opens the serial port without explicit DTR/RTS calls
and verifies a clean boot log. Boot-time I2C Error 263 during the first five
seconds and `SPIFFS Already Mounted` messages are ignored. Invalid image, panic,
watchdog, and boot-loop signatures fail verification.

Launcher installations can continue booting the external-flash image after an
esptool reset. Add `--cold-boot` to pause with instructions to unplug and
reconnect USB:

```bash
python3 scripts/hw_test/hw_flash.py \
  --env SigurdOS_TDeck_remote_test_radio --pi-mode --cold-boot
```

The unified runner can build and flash before testing:

```bash
python3 scripts/hw_test/hw_test_runner.py --smoke --pi-mode \
  --build --flash --env SigurdOS_TDeck_remote_test_radio
```

## Pull request testing

`--pr N` fetches `refs/pull/N/head` from `origin`, creates a detached temporary
git worktree, initializes its submodules, builds the selected environment, and
flashes it before running the chosen tests. It does not switch or clean the
developer's current checkout.

```bash
python3 scripts/hw_test/hw_test_runner.py --pr 123 --smoke --pi-mode \
  --env SigurdOS_TDeck_remote_test_radio \
  --outdir /tmp/pr-123-hardware
```

The temporary worktree is removed after the merged image has been flashed.

## Pi worker model

`--pi-mode` probes `hermes-pi.local` and then `hermes-pi`. It copies this package
to a timestamped directory under `/tmp`, runs it on the Pi with
`/dev/ttyACM0`, and copies the complete results directory back. The Pi-side
worker opens serial once and keeps it open through every selected phase, so SSH
does not turn individual commands into repeated DTR resets.

## Serial behavior

`hw_serial.py` provides `PersistentSerial` and can also be used directly:

```bash
python3 scripts/hw_test/hw_serial.py --port /dev/ttyACM0 --boot-wait 10 --command status
python3 scripts/hw_test/hw_serial.py --port /dev/ttyACM0 --capture /tmp/home.png
```

The module:

- detects remote-test lowercase `nav`/`capture` and release-style uppercase
  `NAV`/`SCREENSHOT` protocols;
- never calls `setDTR()`, `setRTS()`, or `reset_input_buffer()`;
- retries a silent command after `esptool --before default-reset chip-id` USB
  CDC recovery;
- uses a bulk `read()` loop for the complete framebuffer transfer;
- reconstructs fixed-size `[cdata]` chunks around interleaved bracketed logs,
  verifies the exact `height * stride` byte count, and supports RGB565 or
  RGB888 captures;
- parses complete `[stat]` lines byte by byte so kernel read boundaries cannot
  lose heap samples.

The radio-enabled debug firmware deliberately streams screenshots without
starving mesh/UI work. A complete 320×240 capture can therefore take about two
minutes; the standardized capture budget is 165 seconds.

Production firmware may emit no periodic serial output. That is normal: a soak
against a detected release protocol becomes crash-only monitoring. Remote-test
firmware is expected to emit `[stat]`; losing it for longer than the configured
timeout is a critical soak failure.

## Standalone soak

```bash
python3 scripts/hw_test/hw_soak.py \
  --port /dev/ttyACM0 --duration 7200 \
  --outdir /tmp/tdeck-soak-2h
```

The soak parser keeps one connection open, extracts lines byte by byte, appends
every sample to `samples.jsonl`, detects ROM reboot/PANIC/FATAL/Guru Meditation
signatures, captures periodic screenshots, and writes `results.json`.

Soak exit codes are:

- `0`: PASS
- `1`: LEAK (stable-window free heap fell by the threshold, default 1000 bytes)
- `2`: CRASH, reset, early termination, or missing remote-test telemetry

## Reports and exit codes

Every unified run creates:

```text
results.json       CI-oriented structured results
report.md          human-readable report and pass/fail table
github-comment.md  PR-comment-ready Markdown
screenshots/*.png  framebuffer captures
soak/              soak samples, serial log, screenshots, and summary
```

`results.json` includes UTC timestamps, per-test status and duration, detected
firmware metadata, heap samples, screenshot dimensions/hashes, and artifact
paths. `hw_report.py` can re-render it:

```bash
python3 scripts/hw_test/hw_report.py /tmp/hw/results.json
python3 scripts/hw_test/hw_report.py /tmp/hw/results.json \
  --github-comment --output /tmp/hw/pr-comment.md
```

Unified runner exit codes are:

- `0`: all required checks passed (warnings/skips are allowed)
- `1`: partial/non-critical failure
- `2`: critical failure, such as no serial connection, wrong required protocol,
  crash, failed flash/boot, or unrecovered device

## Firmware capabilities

Capabilities are centralized in `hw_constants.py`. The default environment is
`SigurdOS_TDeck_remote_test_radio`, which provides radio, the full test
controller, and periodic heap telemetry. `SigurdOS_TDeck_remote_test` has the
controller but intentionally does not initialize mesh/radio. Release/debug
builds do not have the test controller; supported serial-debug builds use
uppercase navigation/capture commands. `SigurdOS_TDeck_ble_agent` adds the BLE
agent commands used by companion validation.
