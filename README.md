# SlopOS T-Deck

**Status: Beta testing** — several users have flashed successfully. See [Known Issues](#known-issues) below.

Standalone off-grid LoRa mesh messaging firmware for the **LilyGo T-Deck** (ESP32-S3 + SX1262 + ST7789 240×320 TFT touchscreen + physical QWERTY keyboard).

Built on the [MeshCore](https://github.com/meshcore-dev/MeshCore) mesh networking protocol — fully interoperable with existing MeshCore repeaters, room servers, and companion radios.

Full credit to the MeshCore Dev team! I won't ever accept any money or donations for this project but if you wish to put your money to good use, and not the AI hivemind, then sponsor https://github.com/meshcore-dev/MeshCore

## Test Suite

```bash
# Run all 294 tests on native platform (no hardware needed)
pio test -e native_test -v

# Run a specific test module
pio test -e native_test -f test_battery -v
```

| Test Module | Tests | What's Covered |
|-------------|-------|----------------|
| `test_build` | 7 | All headers compile together, cross-module API consistency |
| `test_chat_truncation` | 10 | Chat message truncation, long text handling |
| `test_emoji` | 22 | Emoji font rendering, fallback logic, sizing |
| `test_gps` | 26 | NMEA parsing, coordinate conversion, fix detection |
| `test_home_screen` | 16 | Home screen layout, icon grid, status bars |
| `test_keyboard` | 20 | Matrix scan, keymap, debounce, ghost detection, LVGL mapping |
| `test_map` | 25 | Tile math (lat/lon→tile), zoom levels, bounding box |
| `test_mesh_messaging` | 43 | Message queue, send/receive, channel ops, contact export |
| `test_mesh_wrapper` | 18 | API signatures, return value ranges, unread count init |
| `test_navigation` | 22 | Forward/back with history stack, deep nav chains, all pairs |
| `test_pins` | 9 | GPIO ranges, SPI/I2C bus conflicts, duplicate detection, LoRa params |
| `test_sdcard` | 15 | SPI init, mount, read/write, directory listing, edge cases |
| `test_terminal` | 7 | Terminal buffer management, command parsing |
| `test_theme` | 7 | Color darkness, vibrancy, distinctness, readability hierarchy |
| `test_touch` | 22 | GT911 coordinate mapping, multitouch parsing, press→release lifecycle |
| `test_trackball` | 9 | Direction debounce, deadtime, click detection, idle calibration |

Full test documentation: [`test/README.md`](test/README.md)

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
SlopOS-tdeck/
├── firmware/               ← Pre-built merged binaries (flash at 0x0)
├── lib/meshcore/           ← Git submodule: MeshCore protocol (routing, radio, encryption)
├── src/
│   ├── main.cpp            ← Boot sequence (board → display → mesh → UI)
│   ├── lv_conf.h           ← LVGL v9 config (16-bit, partial render)
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
│   │   └── prefs.cpp/h     ← NVS preferences (radio config, identity)
│   ├── mesh/
│   │   ├── mesh_wrapper.cpp/h  ← SX1262 radio init, RTC, mesh API
│   │   └── slop_mesh.h     ← SlopMesh : mesh::Mesh subclass
│   ├── app/
│   │   └── map_renderer.cpp/h  ← Offline map tile renderer (PNG/JPEG, PSRAM canvas)
│   ├── fonts/
│   │   └── emoji_font_setup.cpp/h  ← Emoji font fallback
│   └── ui/
│       ├── theme.h         ← Discord-inspired dark palette
│       ├── responsive.h    ← Adaptive layout helpers (bars, grids, dialogs)
│       ├── home_screen.cpp/h   ← 4×3 icon grid + top/bottom bars
│       ├── chat_screen.cpp/h   ← Discord-like chat (channels, bubbles, input)
│       ├── screens.cpp/h   ← Heard, Map, Settings, Terminals, etc.
│       ├── onboarding_screen.cpp/h  ← First-boot setup wizard
│       ├── navigation.cpp/h    ← Screen routing with animations
│       └── ui.cpp/h        ← Splash → Home transition
├── boards/t-deck.json      ← PlatformIO board definition
├── platformio.ini          ← Build config (ESP32-S3 + LVGL + MeshCore)
├── test/                   ← Unit test directory (17 modules, 294 tests)
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
git clone --recurse-submodules https://github.com/hermes-gadget/SlopOS-tdeck.git
cd SlopOS-tdeck
```

If `lib/meshcore/` is empty after clone, run:

```bash
git submodule update --init --recursive
```

### Build

```bash
pio run -e SlopOS_TDeck
```

First build downloads the ESP32-S3 toolchain (~800 MB). Subsequent builds are fast.

### Flash

Put the T-Deck in download mode: **hold the trackball button while plugging in USB** (or hold BOOT + tap RESET). The screen stays black — that's correct.

```bash
pio run -e SlopOS_TDeck -t upload
```

### Monitor

```bash
pio device monitor -b 115200
```

### Sanity Check

After cloning, these files must exist or the build will fail:

| File | Purpose |
|------|---------|
| `boards/t-deck.json` | Board definition (16 MB flash, QIO, ESP32-S3) |
| `lib/meshcore/src/Mesh.h` | MeshCore submodule (must not be empty) |
| `platformio.ini` | Build configuration |

## Pre-built Firmware

Pre-built merged binaries are in [`firmware/`](firmware/). Flash directly with esptool — no PlatformIO needed:

```bash
pip install esptool
esptool.py --chip esp32s3 --port COM21 --baud 921600 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode qio --flash_freq 80m --flash_size 16MB \
  0x0 firmware/slopos-tdeck-merged.bin
```

See [`firmware/README.md`](firmware/README.md) for details.

## Screenshots

All screens from the SlopOS T-Deck UI, captured from a live device running the production firmware build.

| Screen | Screenshot | Description |
|--------|-----------|-------------|
| **Home** | ![Home](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/home.png) | 4×3 icon grid launcher with CHATS, CONTACTS, REPEATERS, FINDER, PACKETS, MAP, ADVERTISE, SETTINGS, TRACE, TERMINAL, SETUP, SIGNAL. Top bar shows current channel, bottom bar shows device name + battery. |
| **Onboarding** | ![Onboarding](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/onboarding.png) | First-boot setup wizard (3 steps) — configure node name, radio frequency, and spreading factor before the device is usable. |
| **Chat** | ![Chat](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/chat.png) | Direct message view showing message bubbles between the user and a contact. Includes text input, sent/received messages with timestamps, and navigation to channel chats. |
| **Contacts** | ![Contacts](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/contacts.png) | Lists companions (ADV_TYPE_CHAT) and room servers (ADV_TYPE_ROOM) that have been heard on the mesh. Tap a contact to send a direct message. |
| **Repeaters** | ![Repeaters](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/repeaters.png) | Lists infrastructure relay nodes (ADV_TYPE_REPEATER) heard on the mesh. Repeaters extend network range and are filtered separately from contacts. |
| **Finder** | ![Finder](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/network.png) | Ping Nearby interface — press the button to discover nodes on the local mesh. Shows ping results and known repeaters. |
| **Heard / Packets** | ![Heard](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/heard.png) | Packet log showing all received mesh packets with timestamp, source, RSSI, SNR, and type columns. Useful for network diagnostics. |
| **Map** | ![Map](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/map.png) | Offline tile map renderer showing node locations (from GPS) with pan and zoom. Renders PNG/JPEG tiles from SD card or PSRAM cache. |
| **Advertise** | ![Advertise](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/advertise.png) | Send an advert (presence beacon) to the mesh so other nodes discover you. Shows advert type, cooldown, and last advertised timestamp. |
| **Settings** | ![Settings](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/settings.png) | Device configuration: node name, radio params (frequency, SF, power, gain), display timeout, backlight, GPS toggle, and factory reset. |
| **Trace** | ![Trace](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/trace.png) | Real-time routing trace showing packet paths through the mesh — source → hops → destination with per-hop RSSI/SNR. |
| **Terminal** | ![Terminal](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/terminal.png) | Serial-style command interface for direct MeshCore CLI commands (e.g. `info`, `status`, `nodes`, `channels`). |
| **Signal & SNR** | ![Signal](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/signal.png) | Signal diagnostics screen showing RSSI, SNR, noise floor, and packet success rate for the current radio configuration. |
| **Radio Setup** | ![Radio](https://raw.githubusercontent.com/hermes-gadget/SlopOS-tdeck/dev/docs/screenshots/radio.png) | Advanced radio configuration: frequency band, spreading factor, coding rate, TX power, and RX gain boost. |

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

| Project | License | Usage in SlopOS |
|---------|---------|-----------------|
| [MeshCore](https://github.com/meshcore-dev/MeshCore) | MIT | Mesh networking protocol (submodule at `lib/meshcore/`). Also: RTC clock (`ESP32RTCClock`), auto-off display timer, deep sleep patterns, and `NodePrefs` struct — all adapted from MeshCore's companion radio firmware. |
| [LilyGo T-Deck Keyboard_ESP32C3](https://github.com/Xinyuan-LilyGO/T-Deck) | MIT | I2C keyboard protocol reference — our `keyboard.cpp` driver is based on the command set and keymap from this firmware (© 2023 Shenzhen Xin Yuan Electronic Technology Co., Ltd) |
| [LVGL](https://github.com/lvgl/lvgl) | MIT | Embedded GUI framework (v9.3.0) |
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | FreeBSD | Display driver for ST7789 TFT |
| [RadioLib](https://github.com/jgromes/RadioLib) | MIT | SX1262 LoRa radio driver |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) | MIT | I2C/SPI bus abstraction for sensor/display drivers |
| [Arduino Crypto](https://github.com/rweather/arduinolibs) | MIT | AES/SHA for MeshCore packet encryption |
| [Google Test](https://github.com/google/googletest) | BSD-3-Clause | Unit testing framework |
| [ed25519](https://github.com/orlp/ed25519) | zlib/libpng | Embedded Ed25519 crypto (Orson Peters) — bundled in MeshCore at `lib/meshcore/lib/ed25519/` |
| [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32) | LGPL-2.1 | ESP32-S3 hardware abstraction and Arduino framework (LGPL→GPLv2+ bridge compatible) |
| [PlatformIO](https://github.com/platformio/platformio-core) | Apache 2.0 | Build system (not linked into firmware) |
