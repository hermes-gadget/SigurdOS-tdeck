# SigurdOS T-Deck — Features Overview

**Standalone LoRa mesh messaging firmware for the LilyGo T-Deck (ESP32-S3 + SX1262).**

This document catalogs every feature in the firmware — the 12-grid home screen tiles, system infrastructure, and hardware drivers. Each entry includes a one-line description and a cross-reference to the relevant source file(s) or detail doc.

---

## Table of Contents

- [Overview](#overview)
- [Home Screen — 12-Grid Tiles](#home-screen--12-grid-tiles)
  - [CHATS](#1-chats)
  - [DMs](#2-dms)
  - [ROOMS](#3-rooms)
  - [CONTACTS](#4-contacts)
  - [REPEATERS](#5-repeaters)
  - [PACKETS](#6-packets)
  - [MAP](#7-map)
  - [ADVERTISE](#8-advertise)
  - [SETTINGS](#9-settings)
  - [TERMINAL](#10-terminal)
  - [SETUP](#11-setup)
  - [SIGNAL](#12-signal)
- [System Features](#system-features)
  - [Display & LVGL](#display--lvgl)
  - [Mesh Networking](#mesh-networking)
  - [Screen Navigation](#screen-navigation)
  - [Pixel Theme System](#pixel-theme-system)
  - [Responsive Layout](#responsive-layout)
  - [Emoji Font Support](#emoji-font-support)
  - [NVS Preferences](#nvs-preferences)
  - [Diagnostics & Debug](#diagnostics--debug)
  - [Logging Subsystem](#logging-subsystem)
  - [Packet Log](#packet-log)
  - [RTC & System Time](#rtc--system-time)
  - [SPIFFS Persistence](#spiffs-persistence)
  - [Contact Persistence](#contact-persistence)
  - [Web Flasher Support](#web-flasher-support)
  - [OTA Firmware Update](#ota-firmware-update)
  - [Remote Test Controller](#remote-test-controller)
  - [Structured Telemetry](#structured-telemetry)
  - [Launcher Compatibility](#launcher-compatibility)
  - [Companion BLE](#companion-ble-official-meshcore-app)
- [Hardware Features](#hardware-features)
  - [ST7789 Display](#st7789-display)
  - [GT911 Touch](#gt911-touch)
  - [I2C Keyboard](#i2c-keyboard)
  - [5-Direction Trackball](#5-direction-trackball)
  - [Battery ADC](#battery-adc)
  - [GPS NMEA Parser](#gps-nmea-parser)
  - [SD Card Storage](#sd-card-storage)
  - [LoRa SX1262 Radio](#lora-sx1262-radio)
  - [Buzzer](#buzzer)
  - [Peripheral Power](#peripheral-power)
- [Offline Map Renderer](#offline-map-renderer)

---

## Overview

| Layer | Description | Key Sources |
|-------|-------------|-------------|
| **UI** | Discord-inspired dark pixel interface, 12-tile home grid, chat, settings, and diagnostics screens | `src/ui/*` |
| **Mesh** | Full MeshCore protocol stack — routing, encryption, group channels, direct messages | `src/mesh/*`, `lib/meshcore/` |
| **HAL** | All T-Deck peripherals — display, touch, keyboard, trackball, GPS, battery, SD, buzzer, LoRa | `src/hal/*` |
| **Apps** | Offline map renderer with PNG tile decode and LRU PSRAM cache | `src/app/*` |
| **Boot** | Sequenced startup: board → display → mesh → UI → peripherals | `src/main.cpp` |

---

## Home Screen — 12-Grid Tiles

The home screen is a 4×3 adaptive icon grid with a top bar (channel hashtags, 24h time) and a bottom bar (device name, signal bars, battery %). Navigation uses the 5-direction trackball or capacitive touch.

See [`src/ui/home_screen.cpp`](../src/ui/home_screen.cpp), [`src/ui/home_screen.h`](../src/ui/home_screen.h).

### 1. CHATS
Full multi-channel chat with message bubbles, channel tabs, and text input. Supports hashtag channels and direct messages. Loads and persists per-channel message history from SPIFFS.
**Sources:** [`src/ui/chat_screen.cpp`](../src/ui/chat_screen.cpp), [`src/ui/chat_screen.h`](../src/ui/chat_screen.h)

### 2. DMs
Direct Messages tab within the Chat screen.
**Map:** `Screen::Chat`

### 3. ROOMS
Room servers tab.
**Map:** `Screen::Contacts`

### 4. CONTACTS
List of discovered mesh nodes (up to 350, `-D MAX_CONTACTS=350`) with LRU eviction. Shows contact names, RSSI, location/path metadata, ACL permission role, QR sharing, telemetry action, and promote/demote controls in Contact Detail.
**Sources:** [`src/ui/screens/screen_contacts.cpp`](../src/ui/screens/screen_contacts.cpp), [`src/mesh/mesh_wrapper.h`](../src/mesh/mesh_wrapper.h), [`src/mesh/sigurd_mesh_v2.h`](../src/mesh/sigurd_mesh_v2.h), [`src/app/qr_show.cpp`](../src/app/qr_show.cpp)

### 5. REPEATERS
Lists infrastructure repeater nodes heard on the mesh, with login/command workflows. Routes to the Repeaters screen.
**Sources:** [`src/ui/screens/screen_repeaters.cpp`](../src/ui/screens/screen_repeaters.cpp)

### 6. PACKETS
Heard packets log — a running list of all received radio packets with timestamp, source, RSSI, SNR, and packet type (ADVERT, DM, GRP, TRACE, etc.).
**Sources:** [`src/ui/screens/screen_packets.cpp`](../src/ui/screens/screen_packets.cpp), [`src/mesh/mesh_wrapper.h`](../src/mesh/mesh_wrapper.h)

### 7. MAP
Offline map view using PNG tiles from SD card. Renders a tile grid on an LVGL canvas with pan and zoom, uses PSRAM-backed tile cache, shows own GPS position, and overlays tappable contact-location markers.
**Sources:** [`src/app/map_renderer.cpp`](../src/app/map_renderer.cpp), [`src/app/map_renderer.h`](../src/app/map_renderer.h), [`src/app/tile_cache.h`](../src/app/tile_cache.h), [`src/ui/screens/screen_map.cpp`](../src/ui/screens/screen_map.cpp)

### 8. ADVERTISE
Send a manual mesh advert broadcast to announce your node's presence on the network. Optionally includes GPS coordinates if a fix is available.
**Sources:** [`src/ui/screens/screen_advertise.cpp`](../src/ui/screens/screen_advertise.cpp), [`src/mesh/mesh_wrapper.h`](../src/mesh/mesh_wrapper.h)

### 9. SETTINGS
Settings category hub routing to dedicated sub-screens: WiFi, Bluetooth, Radio / Mesh (RF params, flood/auto-add/timing/duty-cycle, client repeat, regions), GPS / Location, Display / UI (brightness, auto-off, chat cap, theme), System (date/time, PIN, WiFi credentials, AP/GitHub OTA, power controls, version), and Node Stats. See [`docs/SETTINGS_SCREEN.md`](SETTINGS_SCREEN.md).
**Sources:** [`src/ui/screens/screen_settings.cpp`](../src/ui/screens/screen_settings.cpp), [`src/ui/screens/screen_settings_radio.cpp`](../src/ui/screens/screen_settings_radio.cpp), [`src/ui/screens/screen_settings_gps.cpp`](../src/ui/screens/screen_settings_gps.cpp), [`src/ui/screens/screen_settings_display.cpp`](../src/ui/screens/screen_settings_display.cpp), [`src/ui/screens/screen_settings_system.cpp`](../src/ui/screens/screen_settings_system.cpp), [`src/hal/prefs.h`](../src/hal/prefs.h), [`src/hal/wifi_ota.cpp`](../src/hal/wifi_ota.cpp), [`src/hal/github_ota.cpp`](../src/hal/github_ota.cpp)

### 10. TERMINAL
Serial-like terminal screen for diagnostics and utility commands. Includes `help`, status/advert/ping, message signing (`sign <data>`), identity backup (`exportkey`/`importkey`), URI import (`import meshcore://...`), fetch/group-data commands, custom vars, and log clearing.
**Sources:** [`src/ui/screens/screen_terminal.cpp`](../src/ui/screens/screen_terminal.cpp), [`src/ui/screens.h`](../src/ui/screens.h), [`src/mesh/mesh_wrapper.h`](../src/mesh/mesh_wrapper.h)

### 11. SETUP
First-boot onboarding wizard. Guides the user through node name, radio configuration, and channel setup before first use. Saves prefs and reboots when complete.
**Sources:** [`src/ui/onboarding_screen.cpp`](../src/ui/onboarding_screen.cpp), [`src/ui/onboarding_screen.h`](../src/ui/onboarding_screen.h), [`docs/KNOWN_ISSUES.md`](KNOWN_ISSUES.md#onboarding)

### 12. SIGNAL
Signal diagnostics screen showing current RSSI, noise floor, SNR, and signal quality metrics. Visual bar chart representation.
**Sources:** [`src/ui/screens/screen_signal.cpp`](../src/ui/screens/screen_signal.cpp), [`src/ui/theme.h`](../src/ui/theme.h)

---

## System Features

### Display & LVGL
- **LovyanGFX** driver for ST7789 240×320 TFT via SPI
- **LVGL v9.3.0** — full widget toolkit, 16-bit color, animation support, and full-frame double buffering in PSRAM with single/partial fallbacks
- **Auto-off** backlight timeout (30s default) with wake on touch/keyboard input
- **Programmable brightness** via PWM (0–255)
- **Screen capture** via serial hex dump (`lv_snapshot_take_to_draw_buf`)
- **Tick-starvation fix** — periodic `lv_timer_handler()` inside mesh loop (20ms guard) keeps UI responsive
- **Boot splash with live status** — `boot_status()` updates a status label on the splash screen during boot sequence (PR #625), showing progress such as "Mounting storage...", "Starting radio..."
**Sources:** [`src/hal/display.cpp`](../src/hal/display.cpp), [`src/hal/display.h`](../src/hal/display.h), [`src/lv_conf.h`](../src/lv_conf.h), [`docs/KNOWN_ISSUES.md`](KNOWN_ISSUES.md#ui-performance)

### Mesh Networking
- **Full MeshCore protocol** — interoperable with any MeshCore node
- **SX1262 LoRa radio** on shared SPI bus
- **Direct messages** (peer-to-peer encrypted) with path-aware routing (direct or flood)
- **Group channels** — hashtag channels with shared PSK (up to 8 channels)
- **Automatic contact discovery** via advert broadcasts (max 350 contacts via `-D MAX_CONTACTS=350`, LRU eviction)
- **Path learning** — learns and stores outbound/inbound advert paths; Contact Detail shows direct/hop count status
- **Trace route** — per-hop SNR and node hash reporting
- **Ping Nearby** — zero-hop active discovery with tagged PING/PONG exchange
- **Node Discovery Protocol** — answers MeshCore `0x80`/`0x81` discovery requests with `0x90|node_type` responses
- **Telemetry request/answer** — request remote CayenneLPP telemetry and answer inbound telemetry requests with local battery/GPS data
- **Client repeat + multi-ACK** — optional opportunistic relay and redundant ACK transmission settings
- **Regions (Companion Flood Scope)** — RegionMap CRUD with active scope selection, SPIFFS-persisted region list, flood scope stamping on outgoing packets via `SigurdMeshV2::sendFloodScoped()` override (public hashtag `#name`, private `$key`, wildcard). Dedicated UI screen for add/set-active/delete; adverts remain unscoped by design.
- **Packet logging** — per-packet RX log with type, RSSI, SNR
- **RTC time sync** — mesh-synchronised clock for message timestamps
- **Advert broadcast** — manual send with optional GPS coordinates
**Sources:** [`src/mesh/mesh_wrapper.cpp`](../src/mesh/mesh_wrapper.cpp), [`src/mesh/mesh_wrapper.h`](../src/mesh/mesh_wrapper.h), [`src/mesh/sigurd_mesh_v2.h`](../src/mesh/sigurd_mesh_v2.h), [`lib/meshcore/`](../lib/meshcore/)

### Screen Navigation
- **Screen enum** with 26 screen IDs (Home, Chat, Contacts, Channels, Network, Heard, Map, Advertise, Settings, Trace, Terminal, Signal, RadioSetup, Repeaters, Onboarding, ContactDetail, SettingsRadio, SettingsGPS, SettingsDisplay, SettingsSystem, NodeStats, Telemetry, NodeStatus, WiFiNetworks, Bluetooth, Regions)
- **Slide transitions** — configurable `lv_scr_load_anim` with direction and duration
- **Back stack** — linear stack (drops oldest when full, no wrapping), with `can_go_back()` and `go_back()`
- **Universal back-swipe** — two-swipe commit pattern: first Left neutralises, second Left triggers back
- **Top bar** — ← back button, channel hashtag snapshot, 24h time
- **Bottom bar** — device name, signal bars, battery percentage
**Sources:** [`src/ui/navigation.cpp`](../src/ui/navigation.cpp), [`src/ui/navigation.h`](../src/ui/navigation.h), [`src/ui/screens.h`](../src/ui/screens.h)

### Pixel Theme System
- **Discord-inspired dark palette** — deep black `#0F0F0F` background, cyan `#00BFFF` accents
- **Color constants** — 7 background levels, 7 accent colors, 4 text colors, channel colors
- **Style helpers** — `apply_dark_bg()`, `apply_pixel_card()`, `apply_pixel_btn()`, `apply_pixel_btn_outline()`, `apply_pixel_input()`, `apply_pixel_badge()`, `apply_topbar_icon_btn()`
- **Signal dots** — `create_signal_dots()` draws an iOS-style 5-dot RSSI indicator in the top bar (cyan filled dots for active, muted outlines for inactive)
- **Focus style** — yellow accent border for keyboard/trackball focus state
- **Zero radius** on all elements, 2px minimum borders
**Sources:** [`src/ui/theme.h`](../src/ui/theme.h), [`test/test_theme/`](../test/test_theme/)

### Responsive Layout
- **Display-agnostic** — all layout proportional to `TFT_WIDTH`/`TFT_HEIGHT`
- **Adaptive grid** — `compute_grid()` selects 1–4 columns based on available width
- **Proportional bars** — top/bottom bar heights scaled to ~9% of display height (clamped 12–28px)
- **Dialog sizing** — `dialog_size()` caps to display minus margin, `capped_width()` limits as percentage
- **Column distribution** — `column_offsets()` template distributes width by proportional weights
**Sources:** [`src/ui/responsive.h`](../src/ui/responsive.h)

### Emoji Font Support
- **Noto Emoji subset** — compiled as LVGL font (16px, 4bpp grayscale AA)
- **Montserrat wrappers** — 8 size variants (10–28px) with emoji fallback registered
- **Fallback registration** — `emoji_font_register_fallback()` callable once at init
- **Indexed access** — `emoji_font_get_count()` / `emoji_font_get_by_index()` for enumeration
**Sources:** [`src/fonts/emoji_font.h`](../src/fonts/emoji_font.h), [`src/fonts/emoji_font_setup.cpp`](../src/fonts/emoji_font_setup.cpp), [`src/fonts/emoji_data.cpp`](../src/fonts/emoji_data.cpp), [`src/fonts/emoji_data.h`](../src/fonts/emoji_data.h), [`src/fonts/emoji_font.c`](../src/fonts/emoji_font.c), [`src/fonts/emoji_images/emoji_picker_images.h`](../src/fonts/emoji_images/emoji_picker_images.h), [`src/fonts/emoji_images/emoji_picker_index.h`](../src/fonts/emoji_images/emoji_picker_index.h)

### UTF-8 Utilities
- **`utf8_truncate()`** — byte-level truncation that avoids splitting multi-byte codepoints
**Sources:** [`src/utils/utf8_util.h`](../src/utils/utf8_util.h)

### NVS Preferences
- **NodePrefs struct** — persisted in ESP32 NVS: node/radio config, keyboard backlight, message cap, node type, multi-ACK, buzzer quiet, client repeat, device PIN, and WiFi credentials for GitHub OTA
- **load/save/exists** API — `prefs_load()`, `prefs_save()`, `prefs_exists()`
- **Safe defaults** — radio defaults to unconfigured (0 values) so device won't transmit until explicitly set
- **Global accessor** — `prefs_get()` / `prefs_set()` for runtime read/write
**Sources:** [`src/hal/prefs.cpp`](../src/hal/prefs.cpp), [`src/hal/prefs.h`](../src/hal/prefs.h)

### Diagnostics & Debug
- **Compile-time flag** — `SIGURDOS_DEBUG=1` build enables the full debug subsystem
- **Runtime levels** — 1 (quiet), 2 (normal, periodic 5s stats), 3 (verbose)
- **Per-feature toggles** — independent runtime on/off for display, mesh, UI, map, diagnostics
- **Dump functions** — `dump_system()`, `dump_lvgl_rendering()`, `dump_trackball_state()`, `dump_home_screen_layout()`, `dump_memory()`, `dump_display_config()`, `dump_mesh_state()`
- **Test controller integration** — remote test mode can change debug level and feature masks at runtime
**Sources:** [`src/diagnostics/debug.cpp`](../src/diagnostics/debug.cpp), [`src/diagnostics/debug.h`](../src/diagnostics/debug.h), [`src/diagnostics/debug_cfg.h`](../src/diagnostics/debug_cfg.h)

### Logging Subsystem
- **Lightweight serial logging** — header-only macros for tagged log messages (`LOG_I`, `LOG_W`, `LOG_E`)
- **Compile-time gating** — logging compiles away entirely when `SIGURDOS_DEBUG` is not defined
- **Flexible backends** — supports both USB CDC serial and the telemetry framing layer
- **Full documentation:** [`docs/LOGGING.md`](LOGGING.md)
**Sources:** [`src/diagnostics/log.h`](../src/diagnostics/log.h)

### Packet Log
- **Circular packet buffer** — logs every received radio frame with timestamp, source, RSSI, SNR, and payload type string
- **Type classification** — ADVERT_RX, DM_RX, GRP_RX, ANON_RX, ACK, TRACE, PKT_RX
- **Query API** — `getPacketLogCount()`, `getPacketLogEntry()` for UI consumption
**Sources:** [`src/mesh/mesh_wrapper.cpp`](../src/mesh/mesh_wrapper.cpp), [`src/mesh/mesh_wrapper.h`](../src/mesh/mesh_wrapper.h)

### RTC & System Time
- **MeshCore RTC clock** — network-synchronised time for message timestamps and contact `last_seen`
- **Local date/time** — `getCurrentLocalDateTime()` returns year/month/day/hour/minute
- **Epoch helpers** — `makeEpoch()` / `setSystemTime()` for GPS-based or manual time setting
**Sources:** [`src/mesh/mesh_wrapper.h`](../src/mesh/mesh_wrapper.h), [`src/mesh/mesh_wrapper.cpp`](../src/mesh/mesh_wrapper.cpp)

### SPIFFS Persistence
- **State storage** — SPIFFS filesystem for persisting identity keys, contact list, channel config, and message history
- **Graceful fallback** — if SPIFFS mount fails at boot, device continues without persistence (warning logged)
- **Chat persistence** — `chat_save_messages()` / `chat_load_messages()` per-channel history
**Sources:** [`src/main.cpp`](../src/main.cpp), [`src/mesh/mesh_wrapper.cpp`](../src/mesh/mesh_wrapper.cpp), [`src/ui/chat_screen.h`](../src/ui/chat_screen.h)

### Contact Persistence
- **Contact management** — add, remove, lookup, and favourite contacts via `contact_store`
- **On-disk storage** — contacts persisted to SPIFFS via `persistence_store`
- **LRU eviction** — bounded contact list (up to 350 entries) with least-recently-used eviction
- **Full documentation:** [`docs/CONTACT_STORE.md`](CONTACT_STORE.md)
**Sources:** [`src/mesh/contact_store.cpp`](../src/mesh/contact_store.cpp), [`src/mesh/contact_store.h`](../src/mesh/contact_store.h), [`src/mesh/persistence_store.cpp`](../src/mesh/persistence_store.cpp), [`src/mesh/persistence_store.h`](../src/mesh/persistence_store.h)

### Web Flasher Support
- **Pre-built binaries** in `webflasher/` — bootloader, partitions, boot_app0, firmware, merged full image, and the Launcher-named copy
- **Manifest JSON** — versioned metadata (version, git SHA, SHA-256 checksums, offsets) for the `flasher.sigurdos.dev` custom firmware installer
- **4-partition flash layout** — bootloader (0x0000), partitions (0x8000), boot_app0 (0xe000), firmware (0x10000)
**Sources:** [`webflasher/manifest.json`](../webflasher/manifest.json), [`firmware/README.md`](../firmware/README.md), [`webflasher/`](../webflasher/)

### OTA Firmware Update
- **AP upload OTA** — Settings → System → "OTA Update" starts a `SigurdOS-OTA` WiFi AP and upload page at `192.168.4.1`.
- **GitHub pull OTA** — Settings → System → "OTA from GitHub" joins saved WiFi, downloads `firmware.bin` from the latest GitHub release, streams to `Update.write()`, and reboots on success.
- **WiFi credentials** — Settings → System → "WiFi: ..." persists SSID/password in NVS.
- **Dual OTA partitioning** — `default_16MB.csv` provides two app slots for safe OTA updates.
- **Launcher gating** — both OTA paths refuse to start when running under bmorcelli/Launcher (`src/hal/launcher_env.cpp` detection) to protect co-installed firmware.
**Sources:** [`src/hal/wifi_ota.cpp`](../src/hal/wifi_ota.cpp), [`src/hal/github_ota.cpp`](../src/hal/github_ota.cpp), [`src/hal/launcher_env.cpp`](../src/hal/launcher_env.cpp), [`src/ui/screens/screen_settings_system.cpp`](../src/ui/screens/screen_settings_system.cpp), [`platformio.ini`](../platformio.ini)

### Remote Test Controller
- **`SIGURDOS_REMOTE_TEST` build mode** — disables LoRa radio, enables simulated input
- **Inject capabilities** — `keyboard_inject()`, `trackball_inject()`, `test_set_touch()`, `injectMessage()`
- **Remote test loop** — runs alongside normal display/UI loop for automated QA
**Sources:** [`src/test/test_controller.h`](../src/test/test_controller.h), [`src/main.cpp`](../src/main.cpp)

### Structured Telemetry
- **`SIGURDOS_TELEMETRY` build flag** — machine-parseable `@tag|key=value|...` records over USB CDC serial for AI-agent/automated monitoring; zero overhead when disabled
- **Heartbeat ring** — periodic system snapshots (heap, PSRAM, loop timing) in a PSRAM-backed ring buffer (`telemetry_hb_ring`)
- **Crash capture** — backtrace ring buffer that survives reboot (`telemetry_crash`)
- **Collectors** — peripheral state (GPS, SD, WiFi, battery, task watermarks) in `telemetry_collectors`; input-event capture in `telemetry_input`
- The source under `src/diagnostics/` is the authoritative reference for the telemetry system (the earlier design/plan docs were retired once the implementation landed)
**Sources:** [`src/diagnostics/telemetry.cpp`](../src/diagnostics/telemetry.cpp), [`src/diagnostics/telemetry_protocol.cpp`](../src/diagnostics/telemetry_protocol.cpp), [`src/diagnostics/telemetry_crash.cpp`](../src/diagnostics/telemetry_crash.cpp), [`src/diagnostics/telemetry_hb_ring.cpp`](../src/diagnostics/telemetry_hb_ring.cpp), [`src/diagnostics/telemetry_collectors.cpp`](../src/diagnostics/telemetry_collectors.cpp), [`src/diagnostics/telemetry_input.cpp`](../src/diagnostics/telemetry_input.cpp)

### Launcher Compatibility
- **Runtime detection** — `sigurdos_is_under_launcher()` probes for Launcher's resident `test`-subtype app partition, confirmed via the `otadata @ 0xD000` offset; structurally impossible to false-positive on the standalone partition table
- **Self-OTA gating** — WiFi AP and GitHub OTA refuse to run under Launcher to protect co-installed firmware
- **Boot diagnostics** — targeted warning when an app-only Launcher install leaves the device without a SPIFFS partition
- **Install artifact** — releases publish `SigurdOS-tdeck-launcher.bin` (byte-identical to `firmware-merged.bin`) for Launcher SD/WebUI/URL installs
**Full documentation:** [`docs/LAUNCHER.md`](LAUNCHER.md), [`docs/LAUNCHER_ROADMAP.md`](LAUNCHER_ROADMAP.md), [`docs/LAUNCHER_SIZE_AUDIT.md`](LAUNCHER_SIZE_AUDIT.md), [`firmware/README.md`](../firmware/README.md)
**Sources:** [`src/hal/launcher_env.cpp`](../src/hal/launcher_env.cpp), [`src/hal/launcher_env.h`](../src/hal/launcher_env.h), [`test/test_launcher_env/`](../test/test_launcher_env/)

### Companion BLE (Official MeshCore App)

- **CompanionBridge** — broad BLE companion protocol implementation driven by `loop()` via `ObservedSerialBLEInterface` wrapping `SerialBLEInterface`; five recognized upstream command families intentionally return `ERR_CODE_UNSUPPORTED_CMD` (see the [support matrix](COMPANION_SUPPORT.md))
- **Offline queue** — `seedOfflineQueueFromStore()` buffers messages received while phone is disconnected; drained via `CMD_SYNC_NEXT_MESSAGE` on reconnect
- **Dual-consumer hook** — `companion_adapter.inc` fans out message/advert/ack events to both UI and BLE simultaneously
- **SPIFFS message store** — persistent, append-only, dedup-keyed by (conversation, sender, timestamp); survives reboot; shared between chat UI and BLE sync
- **Bluetooth UI screen** — enable/disable toggle, PIN display, connection status indicator, Settings → Network entry
- **Build envs** — BLE is in the default env (`[env:SigurdOS_TDeck]` sets `-D SIGURDOS_COMPANION_BLE=1`; `SigurdOS_TDeck_ble_validation` adds the validation harness); 53 companion protocol + 5 message store native tests
- **PIN pairing** — independently generated six-digit `NodePrefs.ble_pin` with MITM bonding
- **Phased implementation** — Phase 0 (message store persistence), Phase 1 (MVP: handshake, contact sync, DM send/recv), Phase 2 (channels, adverts, radio config), Phase 3 (repeater login, trace, telemetry, private-key export)
**Sources:** [`src/comms/companion_bridge.h`](../src/comms/companion_bridge.h), [`src/comms/companion_bridge.cpp`](../src/comms/companion_bridge.cpp), [`src/comms/companion_adapter.inc`](../src/comms/companion_adapter.inc), [`src/mesh/message_store.h`](../src/mesh/message_store.h), [`src/mesh/message_store.cpp`](../src/mesh/message_store.cpp), [`src/ui/screens/screen_bluetooth.cpp`](../src/ui/screens/screen_bluetooth.cpp), [`platformio.ini`](../platformio.ini), [`test/test_companion_protocol/`](../test/test_companion_protocol/), [`test/test_message_store/`](../test/test_message_store/)

---

## Hardware Features

### ST7789 Display
- **Driver:** LovyanGFX on shared SPI bus (SPI2_HOST, CS=12, DC=11, BL=42)
- **Resolution:** 320×240 (landscape via rotation 1)
- **Color:** 16-bit RGB565; full double buffers in PSRAM with single/full and partial fallbacks
- **Backlight:** PWM via GPIO 42, auto-off timeout, programmable brightness
- **Framebuffer capture** for debugging and screenshots (PSRAM full-buffer mode)
- **Coexistence** with LoRa + SD on same SPI bus (different CS lines)
**Sources:** [`src/hal/display.cpp`](../src/hal/display.cpp), [`src/hal/display.h`](../src/hal/display.h), [`src/hal/tdeck_pins.h`](../src/hal/tdeck_pins.h)

### GT911 Touch
- **Interface:** I2C at 0x5D (SDA=18, SCL=8, INT=16), 400 kHz
- **Coordinate transform:** SWAP_XY=true, MIRROR_X=false, MIRROR_Y=true (matches rotation 1)
- **Polling:** `sigurdos_touch_loop()` called each display frame
- **Multitouch:** Supports multi-point read from GT911 register map
- **Press→release lifecycle** with proper touch-down/touch-up detection
**Sources:** [`src/hal/touch.cpp`](../src/hal/touch.cpp), [`src/hal/touch.h`](../src/hal/touch.h), [`test/test_touch/`](../test/test_touch/)

### I2C Keyboard
- **MCU:** Separate ESP32-C3 slave on I2C address 0x55, shared 400 kHz bus
- **Mode:** Model-independent ASCII key mode, with bounded raw modifier samples
- **Backlight:** I2C commands 0x01/0x02 for brightness control (0–255)
- **Modifiers:** Alt/Mic/Sym extensions retained without raw-decoding normal keys
- **Layouts:** 12 persisted hardware layouts (English, Cyrillic, Greek, Arabic,
  AZERTY/QWERTZ, and accented Latin); double-tap Space within 250 ms to cycle
- **Rendering:** Complete UTF-8 insertion plus Greek/Cyrillic/Arabic font
  fallback; Arabic uses LVGL bidi ordering and contextual shaping
- **Debouncing:** MCU handles matrix scanning and debounce internally
- **Inject API:** `keyboard_inject()` for simulated input in test mode
**Sources:** [`src/hal/keyboard.cpp`](../src/hal/keyboard.cpp), [`src/hal/keyboard_layouts.cpp`](../src/hal/keyboard_layouts.cpp), [`test/test_keyboard/`](../test/test_keyboard/), [`test/test_keyboard_layouts/`](../test/test_keyboard_layouts/)

### 5-Direction Trackball
- **GPIO:** UP=3, DOWN=15, LEFT=1, RIGHT=2, CLICK=0 (BOOT button)
- **Debounce:** Per-direction configurable deadtime (150ms default), falling-edge only
- **Event queue:** `trackball_next_event()` returns queued `SigurdOSTrackballEvent` (None, Up, Down, Left, Right, Click)
- **Idle calibration** — reads and stores idle level at init
- **Resettable** — `trackball_reset_scan_state()` for test and wake recovery
- **Inject API** — `trackball_inject()` for remote testing
- **Home screen navigation** — wraps within the 4×3 grid, wrapping at edges
- **Universal back-swipe** — two-Left-commits pattern for go_back()
**Sources:** [`src/hal/trackball.cpp`](../src/hal/trackball.cpp), [`src/hal/trackball.h`](../src/hal/trackball.h), [`src/ui/navigation.h`](../src/ui/navigation.h), [`test/test_trackball/`](../test/test_trackball/)

### Battery ADC
- **Pin:** GPIO 4 (voltage divider)
- **Math:** ADC_MULTIPLIER = 2 × 3.3 × 1000, 12-bit resolution, 8-sample averaging
- **Outputs:** `battery_mv()` in millivolts, `battery_pct()` clamped 0–100%
- **Ranges:** BAT_MIN_MV = 3000 (0%), BAT_MAX_MV = 4200 (100%)
- **Charging detection:** `battery_charging()` status
- **Auto-shutdown:** `TDeckBoard::isBatteryCritical()` triggers deep sleep at < 3200 mV
- **UI:** Battery % shown in bottom bar, turns red when ≤ 20%
**Sources:** [`src/hal/battery.cpp`](../src/hal/battery.cpp), [`src/hal/battery.h`](../src/hal/battery.h), [`src/hal/tdeck_board.h`](../src/hal/tdeck_board.h), [`test/test_battery/`](../test/test_battery/)

### GPS NMEA Parser
- **Interface:** Serial1 (RX=43, TX=44) at 38400 baud
- **Parsing:** NMEA sentence parser with `$GPGGA`, `$GPRMC`, `$GPGLL` support
- **Checksum:** `nmea_checksum_valid()` validates `*XX` XOR checksum; corrupt sentences silently dropped
- **Data:** Latitude, longitude, altitude (m), speed (kn), heading, satellite count, fix quality (0=none, 1=GPS, 2=DGPS, 4=RTK)
- **Time:** UTC hour/minute/second from GPS
- **Fix state:** `gps_has_fix()` predicate
**Sources:** [`src/hal/gps.cpp`](../src/hal/gps.cpp), [`src/hal/gps.h`](../src/hal/gps.h), [`test/test_gps/`](../test/test_gps/)

### SD Card Storage
- **Interface:** Shared SPI bus (CS=39, SCK=40, MISO=38, MOSI=41) with FATFS VFS at `/sdcard`
- **Bus sharing:** Coexists with LoRa and display on SPI2_HOST (different CS lines)
- **Mount check:** `sdcard_mounted()` for UI gating
- **Capacity:** `capacity_bytes()`, `free_bytes()` with `format_size()` helper
- **File I/O:** `sdcard_read()`, `sdcard_write()`, `sdcard_exists()` for map tile loading
- **Init order:** Initialised after radio init so SPI bus is configured
**Sources:** [`src/hal/sdcard.cpp`](../src/hal/sdcard.cpp), [`src/hal/sdcard.h`](../src/hal/sdcard.h), [`test/test_sdcard/`](../test/test_sdcard/)

### LoRa SX1262 Radio
- **Interface:** SPI (NSS=9, SCK=40, MISO=38, MOSI=41, DIO1=45, RST=17, BUSY=13)
- **Defaults:** 869.618 MHz, BW 62.5 kHz, SF 8, CR 4/5, TX power 22 dBm
- **Driver:** RadioLib-based wrappers via MeshCore
- **Wake-on-radio:** unavailable in deep sleep because DIO1/GPIO45 is not RTC-capable on ESP32-S3
- **Configurable:** Frequency, bandwidth, spreading factor, coding rate, TX power via Radio Setup screen
**Sources:** [`src/hal/tdeck_pins.h`](../src/hal/tdeck_pins.h), [`src/mesh/mesh_wrapper.cpp`](../src/mesh/mesh_wrapper.cpp), [`lib/meshcore/`](../lib/meshcore/)

### Buzzer
- **Pin:** GPIO 46 (active low)
- **Non-blocking pattern playback** — notification patterns (short/double beep) are stepped by `buzzer_loop()` from the main loop instead of blocking delays
- **Quiet mode** — buzzer can be silenced via preferences
**Sources:** [`src/hal/buzzer.cpp`](../src/hal/buzzer.cpp), [`src/hal/buzzer.h`](../src/hal/buzzer.h)

### Peripheral Power
- **Control:** GPIO 10 — `PIN_PERIPH_PWR`
- **Default:** Set HIGH in `TDeckBoard::begin()` to enable all peripherals
- **Sleep:** Set LOW before deep sleep to conserve battery; GPIO hold re-enabled on wake
**Sources:** [`src/hal/tdeck_board.h`](../src/hal/tdeck_board.h)

---

## Offline Map Renderer

A dedicated app-level feature bridging the display, SD card, and GPS systems.

- **Tile source:** PNG format map tiles from SD card (`/sdcard/tiles/{z}/{x}/{y}.png`)
- **Coordinate system:** Slippy-map tile math (lat/lon → tile X/Y at zoom levels)
- **Rendering:** LVGL canvas grid overlaid with decoded tile pixels
- **Cache:** PSRAM-backed LRU tile cache (4 entries @ 256×256 RGB565 ≈ 524 KB)
- **Cache internals:** `tile_cache_init()`, `tile_cache_lookup()`, `tile_cache_evict_slot()` — 64-bit monotonic clock, safe for 584M years
- **Interaction:** Pan by pixel delta, zoom in/out by one level
- **Position overlay:** Renders own GPS position as a marker on the map
**Full documentation:** [`docs/MAP_SCREEN.md`](MAP_SCREEN.md)
**Sources:** [`src/app/map_renderer.cpp`](../src/app/map_renderer.cpp), [`src/app/map_renderer.h`](../src/app/map_renderer.h), [`src/app/tile_cache.h`](../src/app/tile_cache.h), [`src/app/lodepng_alloc.cpp`](../src/app/lodepng_alloc.cpp), [`test/test_map/`](../test/test_map/)

---

## Test Suite

While not a user-facing feature, the comprehensive native test suite (768 cases — 767 passing, 1 always-skipped — across 56 `test/test_<name>/` suites as of 2026-06-11) validates every subsystem: HAL drivers, mesh wrapper and protocol contracts, regions, companion BLE protocol, message/contact stores, navigation, layout, theme, telemetry, emoji fonts, OTA contracts, and the Launcher detection helper.

Run it with `pio test -e native_test`. See [`test/README.md`](../test/README.md) for the full per-suite listing and mock structure — per-module counts are not duplicated here because they change with nearly every PR.

---

## Related Documents

| Document | Description |
|----------|-------------|
| [`README.md`](../README.md) | Project overview, quick start, hardware table |
| [`AGENTS.md`](../AGENTS.md) | Full architecture guide, conventions, pitfalls (agent context) |
| [`CLAUDE.md`](../CLAUDE.md) | Claude Code agent context (mirror of AGENTS.md) |
| [`CONTRIBUTING.md`](../CONTRIBUTING.md) | Contribution workflow, PR checklist, coding standards |
| [`docs/KNOWN_ISSUES.md`](KNOWN_ISSUES.md) | Tracked bugs, fixes, and workarounds |
| [`docs/CHAT_SCREEN.md`](CHAT_SCREEN.md) | Chat screen UI and messaging documentation |
| [`docs/CONTACT_STORE.md`](CONTACT_STORE.md) | Contact store API, persistence, and data model |
| [`docs/HARDWARE.md`](HARDWARE.md) | Hardware pinout, peripherals, and configuration |
| [`docs/HOME_SCREEN.md`](HOME_SCREEN.md) | Home screen layout and tile system documentation |
| [`docs/LAUNCHER.md`](LAUNCHER.md) | Launcher detection, OTA gating, partition layout |
| [`docs/LOGGING.md`](LOGGING.md) | Logging subsystem API, verbosity levels, and configuration |
| [`docs/MAP_SCREEN.md`](MAP_SCREEN.md) | Map screen and tile cache system documentation |
| [`docs/MESH_NETWORKING.md`](MESH_NETWORKING.md) | Mesh networking protocol and features documentation |
| [`docs/MISSING_FEATURES.md`](MISSING_FEATURES.md) | Companion parity audit: implemented, declined, and out-of-scope MeshCore deltas |
| [`docs/COMPANION_SUPPORT.md`](COMPANION_SUPPORT.md) | Current BLE companion command support matrix and interoperability limits |
| [`docs/NETWORK_SCREEN.md`](NETWORK_SCREEN.md) | Network screen documentation |
| [`docs/ROADMAP.md`](ROADMAP.md) | Development roadmap and planned features |
| [`docs/SETTINGS_SCREEN.md`](SETTINGS_SCREEN.md) | Settings screen documentation |
| [`docs/SIGNAL_SCREEN.md`](SIGNAL_SCREEN.md) | Signal diagnostics screen documentation |
| [`docs/TERMINAL.md`](TERMINAL.md) | Terminal screen documentation |
| [`test/README.md`](../test/README.md) | Test suite structure, mock guidelines, running tests |
| [`firmware/README.md`](../firmware/README.md) | Flash instructions, binary layout, web flasher |
