<p align="center">
  <img src="docs/SigurdOS_Banner.png" alt="SigurdOS Banner" width="100%">
</p>

# SigurdOS T-Deck

**Status: Beta testing** — several users have flashed successfully. See [Known Issues](#known-issues) below.

Standalone off-grid LoRa mesh messaging firmware for the **LilyGo T-Deck** (ESP32-S3 + SX1262 + ST7789 240×320 TFT touchscreen + physical QWERTY keyboard).

Built on the [MeshCore](https://github.com/meshcore-dev/MeshCore) mesh networking protocol. Core messaging interoperates with existing MeshCore repeaters, room servers, and companion radios; see the [companion command support matrix](docs/COMPANION_SUPPORT.md) for explicit protocol limits.

Current firmware snapshot: `SIGURDOS_VERSION` in [`src/hal/tdeck_pins.h`](src/hal/tdeck_pins.h) is `beta-0.1.44-RC6`.

Full credit to the MeshCore Dev team! I won't ever accept any money or donations for this project but if you wish to put your money to good use, and not the AI hivemind, then sponsor https://github.com/meshcore-dev/MeshCore

## Test Suite

```bash
# Run the native host-side suite (no hardware needed)
pio test -e native_test

# Run a specific test module
pio test -e native_test -f test_battery

# Build the firmware image for LilyGo T-Deck
pio run -e SigurdOS_TDeck
```

| Test Module | What's Covered |
|-------------|----------------|
| `test_battery` | Battery level monitoring, ADC reading, voltage conversion, percentage calculation |
| `test_build` | All headers compile together, cross-module API consistency |
| `test_build_info` | Firmware version string, git SHA, build environment metadata |
| `test_buzzer` | Buzzer notification patterns, tone durations, active-low control |
| `test_channel_menu` | Channel quick-action menu, private scope handling, leave/join logic |
| `test_channel_validation` | Channel name validation, allowed/hyphenated/mixed-case names, rejection rules |
| `test_chat_config` | Chat screen message capacity, normalization, clamp boundaries |
| `test_chat_truncation` | Chat message truncation, long text handling |
| `test_companion_protocol` | Companion device protocol frames, sync, message enqueue/drain |
| `test_contact_paging` | Contact list pagination, page count, start/end calculations |
| `test_contact_store` | Contact persistence format, magic header, version/bounds checks |
| `test_controller` | RF parameter parsing, frequency/SF/bandwidth/coding rate validation |
| `test_debug` | Debug level clamping, serial debug command stubs, non-debug build guards |
| `test_emoji` | Emoji font rendering, fallback logic, sizing |
| `test_emoji_fallback` | Emoji wrapped font pointers, writable copies, LVGL fallback chain |
| `test_emoji_integrity` | Emoji font glyph count, indexed entry presence, uniqueness |
| `test_github_ota_contract` | GitHub OTA state machine, API signatures, release URL generation |
| `test_gps` | NMEA parsing, coordinate conversion, fix detection |
| `test_hal_contract` | HAL display/gps lifecycle signatures, debug capture APIs, type stability |
| `test_home_screen` | Home screen layout, icon grid, status bars |
| `test_input_contract` | Trackball/keyboard input event types, remote hook signatures |
| `test_keyboard` | ASCII key mode, raw modifier sampling, extended layers, event queue |
| `test_keyboard_layouts` | 12-language UTF-8 maps, persistence, cycle gesture timing |
| `test_launcher_env` | Launcher runtime detection, partition/otadata probing, false-positive guards |
| `test_layout` | Adaptive layout helpers, responsive grid calculations, screen size adjustments |
| `test_lodepng_alloc` | lodepng PSRAM allocator, malloc/realloc fallback, heap caps delegation |
| `test_log` | Logging macro level prefixes, debug-build compile-time gating |
| `test_map` | Tile math, zoom levels, bounding box |
| `test_map_renderer` | Map tile mercator math, zoom validation, lat/lon/tile round-trip |
| `test_mesh_contract` | Mesh advert types, contact flags, login status, wire format stability |
| `test_mesh_messaging` | Message queue, send/receive, channel ops, contact export |
| `test_mesh_wrapper` | API signatures, return value ranges, unread count init |
| `test_message_store` | Message storage append/load/dedup, path length, ring rotation |
| `test_navigation` | Forward/back with history stack, deep nav chains, all pairs |
| `test_navigation_contract` | Screen enum inventory, contiguous values, stable position checks |
| `test_onboarding` | Onboarding wizard date/time validation, leap year, days-in-month |
| `test_pins` | GPIO ranges, SPI/I2C bus conflicts, duplicate detection, LoRa params |
| `test_prefs` | NVS preferences, radio config persistence, identity storage, save/load |
| `test_prefs_defaults` | NVS preference default values, radio/identity/mesh behavior defaults |
| `test_qr_show` | QR code layout, version sizing, canvas fitting, scale calculation |
| `test_regions` | Geographic region registry, add/find/remove, prefix search |
| `test_responsive` | Responsive column offsets, weight distribution, edge cases |
| `test_sdcard` | SPI init, mount, read/write, directory listing, edge cases |
| `test_tdeck_board` | TDeckBoard power management, auto-shutdown threshold, voltage critical levels |
| `test_telemetry_collectors` | Telemetry task watermark collection, null/zero capacity validation |
| `test_telemetry_crash` | Crash backtrace ring buffer, bounded count clamping |
| `test_telemetry_hb_ring` | Heartbeat ring buffer, read/write, wrap-around, logical indexing |
| `test_telemetry_input` | Telemetry input sampling, invalid trackball direction rejection |
| `test_telemetry_packet_log` | Packet log field formatting, null/empty string handling |
| `test_telemetry_protocol` | Telemetry record emission, signed/unsigned/float/string fields |
| `test_terminal` | Terminal buffer management, command parsing |
| `test_theme` | Color darkness, vibrancy, distinctness, readability hierarchy |
| `test_touch` | GT911 coordinate mapping, multitouch parsing, press/release lifecycle |
| `test_trackball` | Direction debounce, deadtime, click detection, idle calibration |
| `test_ui_contract` | UI screen show APIs, home/detail/settings screen signature stability |
| `test_ui_timing` | UI splash screen timing, transition elapsed checks |
| `test_wifi_scan` | WiFi network scanning, connection management, AP-mode OTA |

Representative modules are listed above; full `test/test_<name>/` coverage and per-file detail is maintained in [`test/README.md`](test/README.md).

Full test documentation: [`test/README.md`](test/README.md)

Before flashing or running on-device validation, follow the standardized
[`Hardware Testing Protocol`](docs/HARDWARE_TESTING.md) and its
[`copy-paste checklist`](scripts/hw_test/CHECKLIST.md).

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32-S3, 240 MHz, 16 MB Flash, 8 MB PSRAM |
| Display | ST7789 240×320 TFT (landscape via rotation) |
| Touch | GT911 capacitive (I2C) |
| Keyboard | Physical QWERTY (I2C, ESP32-C3 MCU) |
| LoRa | SX1262 (SPI) |
| GPS | Serial1 (optional) |
| SD Card | SPI (shared bus) |

## Architecture

```
SigurdOS-tdeck/
├── firmware/               ← Pre-built merged binaries (flash at 0x0)
├── lib/meshcore/           ← Git submodule: MeshCore protocol (routing, radio, encryption)
├── src/
│   ├── main.cpp            ← Boot sequence (board → peripheries + display → splash/prefs → mesh → UI)
│   ├── lv_conf.h           ← LVGL v9 config (16-bit, partial render)
│   ├── utils/
│   │   └── utf8_util.h     ← UTF-8 string utilities (validation, truncation)
│   ├── hal/
│   │   ├── tdeck_pins.h    ← Complete T-Deck pinout + version string
│   │   ├── tdeck_board.h   ← TDeckBoard :: mesh::MainBoard
│   │   ├── display.cpp/h   ← LovyanGFX ST7789 + LVGL driver
│   │   ├── trackball.cpp/h ← 5-direction trackball (debounce, event queue)
│   │   ├── battery.cpp/h   ← ADC battery (mV + %)
│   │   ├── touch.cpp/h     ← GT911 touch controller (I2C)
│   │   ├── keyboard.cpp/h  ← I2C keyboard (ESP32-C3 MCU)
│   │   ├── gps.cpp/h       ← NMEA GPS parser (Serial1)
│   │   ├── sdcard.cpp/h    ← microSD card (SPI, shared bus)
│   │   ├── buzzer.cpp/h    ← Active-high buzzer control (GPIO 46)
│   │   ├── prefs.cpp/h     ← NVS preferences (radio config, identity, WiFi OTA)
│   │   ├── wifi_ota.cpp/h  ← AP-mode OTA upload server
│   │   └── github_ota.cpp/h ← GitHub-release OTA downloader
│   ├── mesh/
│   │   ├── mesh_wrapper.cpp/h  ← SX1262 radio init, RTC, mesh API
│   │   ├── message_store.cpp/h ← Shared persistent message store (dedup, ACK flags)
│   │   ├── contact_store.cpp/h ← Versioned contact persistence
│   │   ├── regions.cpp/h       ← Region map / flood-scope support
│   │   └── sigurd_mesh_v2.cpp/h ← SigurdMeshV2 : BaseChatMesh subclass
│   ├── comms/
│   │   └── companion_bridge.cpp/h ← BLE companion protocol (official MeshCore app)
│   ├── diagnostics/
│   │   ├── log.h               ← SIG_LOG* logging macros
│   │   ├── debug.cpp/h         ← Debug dumps (SIGURDOS_DEBUG builds)
│   │   └── telemetry*.cpp/h    ← Structured telemetry (heartbeat ring, crash capture)
│   ├── app/
│   │   ├── map_renderer.cpp/h  ← Offline map tile renderer (PNG, PSRAM canvas)
│   │   ├── tile_cache.cpp/h    ← Tile caching layer (LRU, PSRAM)
│   │   └── qr_show.cpp/h       ← QR code display for contact sharing
│   ├── fonts/
│   │   ├── emoji_font.c/h       ← Emoji font glyph definitions
│   │   ├── emoji_font_setup.cpp ← LVGL emoji font integration
│   │   ├── emoji_data.cpp/h     ← Emoji unicode character tables
│   │   ├── keyboard_layout_font.c/h ← International layout glyphs
│   │   └── latin_ext_font.c/h   ← Latin Extended fallback glyphs
│   └── ui/
│       ├── theme.cpp/h     ← Discord-inspired dark palette
│       ├── responsive.h    ← Adaptive layout helpers (bars, grids, dialogs)
│       ├── home_screen.cpp/h   ← 4×3 icon grid + top/bottom bars
│       ├── chat_screen.cpp/h   ← Discord-like chat (channels, bubbles, input)
│       ├── screens_common.cpp/h ← Shared screen chrome (make_screen_full, PIN gate)
│       ├── screens.cpp/h   ← Slim dispatch shim + shared declarations
│       ├── screens/        ← One module per screen (screen_map, screen_settings_*, …)
│       ├── onboarding_screen.cpp/h  ← First-boot setup wizard
│       ├── navigation.cpp/h    ← Screen routing with animations
│       └── ui.cpp/h        ← Splash → Home transition
├── boards/t-deck.json      ← PlatformIO board definition
├── platformio.ini          ← Build config (ESP32-S3 + LVGL + MeshCore)
└── test/                   ← Native test suite and mocks
```

## Build & Flash

### Prerequisites
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- LilyGo T-Deck with USB-C cable

### Windows Setup

Install everything from a PowerShell terminal:

```powershell
# 1. Git
winget install Git.Git

# 2. Python 3.12
winget install Python.Python.3.12

# 3. PlatformIO CLI
pip install platformio

# 4. CP210x USB driver (for T-Deck USB-to-UART)
# Download from: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
# Unzip → right-click silabser.inf → Install
#
# Verify: Device Manager → Ports (COM & LPT) → "Silicon Labs CP210x USB to UART Bridge"
```

Restart your terminal after installing Python, then verify:

```powershell
git --version
python --version
pio --version
```

### Linux Setup

```bash
# Ubuntu/Debian
sudo apt install git python3 python3-pip
pip install platformio

# Arch
sudo pacman -S git python python-pip
pip install platformio
```

No USB driver needed on Linux — the CP210x kernel module ships with the kernel.

### macOS Setup

```bash
# Homebrew
brew install git python platformio
```

No USB driver needed on macOS — the CP210x driver is built into the OS.

### Clone with submodule

```bash
git clone --recurse-submodules https://github.com/hermes-gadget/SigurdOS-tdeck.git
cd SigurdOS-tdeck
```

If `lib/meshcore/` is empty after clone, run:

```bash
git submodule update --init --recursive
```

### Build

```bash
pio run -e SigurdOS_TDeck
```

First build downloads the ESP32-S3 toolchain (~800 MB). Subsequent builds are fast.

### Flash

Put the T-Deck in download mode: **hold the trackball button while plugging in USB** (or hold BOOT + tap RESET). The screen stays black — that's correct.

```bash
pio run -e SigurdOS_TDeck -t upload
```

### Monitor

```bash
pio device monitor -b 115200
```

### Sanity Check

After cloning, these files must exist or the build will fail:

| File | Purpose |
|------|---------|
| `boards/t-deck.json` | Board definition (16 MB flash, ESP32-S3; firmware overrides flash mode to DIO) |
| `lib/meshcore/src/Mesh.h` | MeshCore submodule (must not be empty) |
| `platformio.ini` | Build configuration |

## Pre-built Firmware

Pre-built merged binaries are in [`firmware/`](firmware/). Flash directly with esptool — no PlatformIO needed:

```bash
pip install esptool
esptool.py --chip esp32s3 --port COM21 --baud 921600 \
  --before default_reset --after hard_reset write_flash \
  0x0 firmware/sigurdos-tdeck-merged.bin
```

Do not add a `--flash_mode qio` override. The merged image contains a DIO
bootloader header required by T-Deck units that boot-loop in QIO mode.

See [`firmware/README.md`](firmware/README.md) for details.

## Screenshots

Representative screenshots from a device running the current firmware build (screenshots may lag behind the latest UI changes).

| Screen | Screenshot | Description |
|--------|-----------|-------------|
| **Home** | ![Home](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/home.png) | 4×3 icon grid launcher with CHATS, DMs, ROOMS, CONTACTS, REPEATERS, ADVERTISE, MAP, TERMINAL, PACKETS, SETTINGS, SETUP, SIGNAL. Top bar shows current channel, bottom bar shows device name + battery. |
| **Onboarding** | ![Onboarding](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/onboarding.png) | First-boot setup wizard (3 steps) — configure node name, radio frequency, and spreading factor before the device is usable. |
| **Chat** | ![Chat](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/chat.png) | Direct message view showing message bubbles between the user and a contact. Includes text input, sent/received messages with timestamps, and navigation to channel chats. |
| **Contacts** | ![Contacts](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/contacts.png) | Lists companions (ADV_TYPE_CHAT) and room servers (ADV_TYPE_ROOM) that have been heard on the mesh. Tap a contact to send a direct message. |
| **Repeaters** | ![Repeaters](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/repeaters.png) | Lists infrastructure relay nodes (ADV_TYPE_REPEATER) heard on the mesh. Repeaters extend network range and are filtered separately from contacts. |
| **Finder** | ![Finder](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/network.png) | Ping Nearby interface — press the button to discover nodes on the local mesh. Shows ping results and known repeaters. |
| **Heard / Packets** | ![Heard](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/heard.png) | Packet log showing all received mesh packets with timestamp, source, RSSI, SNR, and type columns. Useful for network diagnostics. |
| **Map** | ![Map](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/map.png) | Offline tile map renderer showing node locations (from GPS) with pan and zoom. Renders PNG tiles from SD card or PSRAM cache. |
| **Advertise** | ![Advertise](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/advertise.png) | Send an advert (presence beacon) to the mesh so other nodes discover you. Shows advert type, cooldown, and last advertised timestamp. |
| **Settings** | ![Settings](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/settings.png) | Device configuration: node name, radio params (frequency, SF, power, gain), display timeout, backlight, GPS toggle, and factory reset. |
| **Trace** | ![Trace](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/trace.png) | Real-time routing trace showing packet paths through the mesh — source → hops → destination with per-hop RSSI/SNR. |
| **Terminal** | ![Terminal](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/terminal.png) | Serial-style command interface for direct MeshCore CLI commands (e.g. `help`, `status`, `advert`, `ping`, `sign`, `anon`, `fetchmsgs`, `groupdata`, `emoji-list`, `exportkey`, `importkey`). |
| **Signal & SNR** | ![Signal](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/signal.png) | Signal diagnostics screen showing TX/RX flood/direct counts, airtime, duty cycle, RSSI history sparkline, and current radio parameters. |
| **Radio Setup** | ![Radio](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/radio.png) | Advanced radio configuration: frequency band, spreading factor, coding rate, TX power, and RX gain boost. |
| **WiFiNetworks** | ![WiFi](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/wifi.png) | WiFi network scanning and connection management for OTA updates and diagnostics. Shows available access points with encryption status, RSSI, and connect flow. |
| **NodeStats** | *No screenshot* | Node statistics display showing uptime, memory usage, packet counts, and mesh health. |
| **Telemetry** | ![Telemetry](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/telemetry.png) | Environmental telemetry readouts (temperature, humidity, pressure) from sensor-equipped mesh nodes. |
| **NodeStatus** | ![NodeStatus](https://raw.githubusercontent.com/hermes-gadget/SigurdOS-tdeck/dev/docs/screenshots/node-status.png) | Node status overview showing connection state, last heard, and signal quality indicators. |
| **Bluetooth** | *No screenshot* | BLE pairing and companion-connection state (PIN display, enable toggle, last sync age). |
| **Regions** | *No screenshot* | Region list, active-region chip, and `$` private-key region management. |
| **Repeater Detail** | *No screenshot* | Repeater detail and command/status actions for paired infrastructure nodes. |
| **Custom Radio Setup** | *No screenshot* | Advanced radio parameter profile editor and profile-restore controls. |

## License

GPL-3.0-or-later

This project is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

Dependencies remain under their original licenses (MIT, FreeBSD, LGPL-2.1,
zlib/libpng, BSD-3-Clause) — see [Open Source Acknowledgments](#open-source-acknowledgments)
below for the full audit.

## Open Source Acknowledgments

This project builds on and incorporates open source software from the following projects:

| Project | License | Usage in SigurdOS |
|---------|---------|-----------------|
| [MeshCore](https://github.com/meshcore-dev/MeshCore) | MIT | Mesh networking protocol (submodule at `lib/meshcore/`). Also: RTC clock (`ESP32RTCClock`), auto-off display timer, deep sleep patterns, and `NodePrefs` struct — all adapted from MeshCore's companion radio firmware. |
| [LilyGo T-Deck Keyboard_ESP32C3](https://github.com/Xinyuan-LilyGO/T-Deck) | MIT | I2C keyboard protocol reference — our `keyboard.cpp` driver is based on the command set and keymap from this firmware (© 2023 Shenzhen Xin Yuan Electronic Technology Co., Ltd) |
| [wadamesh](https://github.com/ALLFATHER-BV/wadamesh) | GPL-3.0 | Physical-key phonetic mapping tables and 12-layout ordering adapted for the T-Deck keyboard |
| [DejaVu Fonts](https://dejavu-fonts.github.io/) | Bitstream Vera font license | Greek, Cyrillic, and Arabic fallback glyphs for international keyboard layouts; full notice in `LICENSES/DejaVu-Fonts.txt` |
| [Noto Emoji](https://fonts.google.com/noto/specimen/Noto+Emoji) | SIL Open Font License 1.1 | Emoji font glyph data in `src/fonts/emoji_font.c` and emoji picker images in `src/fonts/emoji_images/`; full notice in `LICENSES/Noto-Emoji.txt` |
| [LodePNG](https://github.com/lvandeve/lodepng) | zlib/libpng | PNG decoder for map tile rendering; compiled with PSRAM allocator override in `src/app/lodepng_psram.cpp` |
| [QRCode](https://github.com/ricmoo/QRCode) | MIT | QR-code generation library used by the identity export screen |
| [LVGL](https://github.com/lvgl/lvgl) | MIT | Embedded GUI framework (v9.3.0) |
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | FreeBSD | Display driver for ST7789 TFT |
| [RadioLib](https://github.com/jgromes/RadioLib) | MIT | SX1262 LoRa radio driver |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) | MIT | I2C/SPI bus abstraction for sensor/display drivers |
| [Arduino Crypto](https://github.com/rweather/arduinolibs) | MIT | AES/SHA for MeshCore packet encryption |
| [Google Test](https://github.com/google/googletest) | BSD-3-Clause | Unit testing framework |
| [ed25519](https://github.com/orlp/ed25519) | zlib/libpng | Embedded Ed25519 crypto (Orson Peters) — bundled in MeshCore at `lib/meshcore/lib/ed25519/` |
| [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32) | LGPL-2.1 | ESP32-S3 hardware abstraction and Arduino framework (LGPL→GPLv2+ bridge compatible) |
| [PlatformIO](https://github.com/platformio/platformio-core) | Apache 2.0 | Build system (not linked into firmware) |
