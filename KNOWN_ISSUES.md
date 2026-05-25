# Known Issues

This document tracks known issues, bugs, and missing features in SlopOS. Contributions welcome — pick something from the list and open a PR.

---

## Emoji Support

### Incomplete emoji character coverage
PR #25 added emoji support with LVGL font fallback and an emoji picker, but only a subset of Unicode emoji codepoints have actual glyphs in the font. A large number of emoji render as empty boxes (missing glyph rectangles) both in chat messages and in the picker itself.

**What's needed:** Expand the emoji font to cover more codepoints, particularly the most common ones (smileys, gestures, symbols). The `generate_emoji_font.sh` script in the repo root controls which codepoints are included — update the codepoint range and regenerate the font.

---

## Signal Bars

### RSSI-based signal strength indicator
There's no visual signal strength indicator anywhere in the UI. Users have to navigate to the Heard screen and read raw RSSI numbers to gauge link quality.

**What's needed:** A small signal bar widget (1-5 bars) based on the last received message's RSSI from each contact. Bars should be rendered with the pixel aesthetic — blocky, no curves. Reference threshold levels:

| Bars | RSSI Range |
|------|-----------|
| 5    | > -70 dBm |
| 4    | -70 to -85 dBm |
| 3    | -85 to -95 dBm |
| 2    | -95 to -105 dBm |
| 1    | < -105 dBm |

Could be shown next to each contact in the Contacts screen, in the chat header, and on the home screen mesh status.

---

## Finder

### Zero-hop ping for nearby discovery
The finder feature needs a proper implementation that sends a zero-hop (TTL=1) ping to discover nearby repeaters. Currently there's no way to probe what's in immediate radio range without relying on periodic adverts.

**What's needed:**
- A "Ping Nearby" action that sends a broadcast with hop limit = 1
- A response handler that collects replies over a short window (2-3 seconds)
- Display results grouped by RSSI (strongest first)
- Cooldown of 30 seconds between pings to avoid flooding

---

## Terminal

### Undocumented commands
The built-in serial/diagnostics terminal exposes several internal commands but there's no documentation on what's available or what each command does. Users have to read the source code to discover features.

**What's needed:** A `help` command that lists all available commands with a one-line description. A `help <command>` variant that shows usage details. The help text should be stored as a single `const char*` array in `src/ui/terminal.cpp` so it stays easy to update.

Common commands that should be documented:
| Command | Description |
|---------|-------------|
| `help` | List available commands |
| `status` | Show mesh status, node count, uptime |
| `channels` | List joined channels |
| `nodes` | List known nodes |
| `signal` | Show RSSI/SNR for last heard transmission |
| `send <text>` | Send a text message to the current channel |
| `save` | Force save state to NVS |
| `reset` | Reboot the device |
| `gps` | Show current GPS fix data |

---

## Launcher Compatibility

### SlopOS doesn't work under bmorcelli/Launcher
[Launcher](https://github.com/bmorcelli/Launcher) is an ESP32 app launcher with explicit T-Deck support (display, touch, keyboard, SD card). A user tried running SlopOS as a Launcher-launched app and ran into problems — the keyboard doesn't work properly, and many other things break.

**Root cause:** SlopOS is built as standalone firmware that expects full hardware control at boot. Launcher initialises the display, keyboard, I2C, SPI, and LoRa pins before handing off, which leaves GPIOs, peripheral registers, and I2C bus state in an incompatible state when SlopOS starts.

**What's needed — a `launcher-compatible` build target or a compatibility layer:**
- Ensure all peripheral init (keyboard I2C, display, LoRa SPI, GPIOs, PSRAM) is safe to re-init even if already configured by a bootloader or launcher
- Partition table must coexist with Launcher's OTA partition scheme — likely needs a unified partition layout
- Display handoff: ST7789 registers may be in an unknown state — LovyanGFX handles this on init but needs testing
- LoRa radio: Launcher disables LoRa CS for SD card access — SlopOS needs to fully reset and re-init the SX1262 on boot
- Keyboard: the I2C keyboard MCU (0x55) may already be claimed or in key mode — must force re-init via Wire + backlight commands
- Trackball GPIOs may have internal pull states changed by Launcher — need explicit `pinMode` re-init
- LoRa SPI bus (shared with display) must be safely re-initialised without conflicting with any prior configuration

---

## How to Help

Pick any item from the list above and open a PR against the `dev` branch. See [`CONTRIBUTING.md`](./CONTRIBUTING.md) for the full contribution workflow.
