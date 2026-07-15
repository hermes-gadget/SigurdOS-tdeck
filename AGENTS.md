# SigurdOS T-Deck — Agent Onboarding

**You are an AI agent working on the SigurdOS T-Deck firmware.** This file is your instruction manual. Read it before modifying code.

**Do not modify this file or `CLAUDE.md` in any PR.** They are AI agent context. Only the repo owner changes them. Any PR that touches `AGENTS.md` or `CLAUDE.md` will be rejected without review.

---

## Markdown Files in This Repo

These are the reference documents you should load before starting work. Which one to read depends on what you're doing:

| File | When to read it | Purpose |
|------|----------------|---------|
| **`README.md`** | First time in the repo | Project overview, quick start, license |
| **`AGENTS.md`** ← you are here | Every session | Agent instructions, architecture, conventions, pitfalls |
| **`CLAUDE.md`** | Claude Code sessions | Same content — mirror of AGENTS.md |
| **`CONTRIBUTING.md`** | Before ANY task or PR | **Mandatory.** Full contribution workflow, issue-first requirement, PR checklist. AI agents must follow every step. |
| **`docs/KNOWN_ISSUES.md`** | Before feature work | What's broken or unfinished — don't duplicate effort |
| **`docs/CONTACT_STORE.md`** | When working on contact management | Contact store API, persistence, and data model |
| **`docs/LAUNCHER.md`** | When working on Launcher compatibility | Launcher detection, OTA gating, partition layout |
| **`docs/LOGGING.md`** | Before debugging serial output | Logging subsystem API, verbosity levels, and configuration |
| **`docs/MISSING_FEATURES.md`** | Before implementing new features | Catalog of MeshCore protocol features not yet implemented, with source references and effort estimates |
| **`firmware/README.md`** | Releasing or CI work | Release artifact structure, web flasher manifest format |
| **`test/README.md`** | Writing new tests | Test framework, mock structure, naming conventions |
| **`docs/HARDWARE.md`** | When working on hardware/drivers | Full hardware reference: pinout, boot sequence, peripheral details |
| **`docs/CHAT_SCREEN.md`** | When working on chat UI | Chat screen architecture, data model, input routing, persistence |
| **`docs/FEATURES_OVERVIEW.md`** | Getting oriented on features | Top-level index of all features with source cross-references |
| **`docs/HOME_SCREEN.md`** | When working on home screen | Home screen layout, tile grid, icon mapping |
| **`docs/ROADMAP.md`** | Understanding project direction | Development roadmap and planned features |
| **`docs/TERMINAL.md`** | When working on terminal | Terminal screen commands, serial protocol, debug levels |
| **`docs/MAP_SCREEN.md`** | When working on the map | Map screen rendering, tile cache, PSRAM usage |
| **`docs/NETWORK_SCREEN.md`** | When working on network/finder | Node discovery, Ping Nearby, network neighbourhood view |
| **`docs/NOTIFICATIONS.md`** | When working on UI notifications | Four-entry notification queue, renderer, banner lifecycle |
| **`docs/SIGNAL_SCREEN.md`** | When working on signal display | Live RSSI/SNR sparkline, radio parameter display |
| **`docs/SETTINGS_SCREEN.md`** | When working on settings | Settings screen hierarchy, sub-screen routing |
| **`docs/COMPANION_SUPPORT.md`** | When working on BLE companion | Companion command support matrix, protocol coverage |
| **`docs/MESH_NETWORKING.md`** | When working on mesh protocol internals | MeshCore integration, addressing, routing details |
| **`docs/RELEASE_EVIDENCE.md`** | Before cutting a release | Release PR requirements, CI evidence checks, golden frames |

**Critical rules — follow all:**
1. **Check for an existing issue on the upstream repo.** Before writing any code, check if there's already an open GitHub issue on `hermes-gadget/SigurdOS-tdeck` covering what you plan to do. If not, open one. No issue = no PR accepted.
2. **Read `docs/KNOWN_ISSUES.md`** before starting any feature work. If someone already tried and documented a problem, you'll find it there.
3. **Read `docs/MISSING_FEATURES.md`** before implementing any new feature. If the capability is listed there, don't duplicate the research — use the MeshCore source references and effort estimates provided.
4. **Follow `CONTRIBUTING.md`** — it's not optional. Every step applies to AI agents the same as human contributors.

---

## What Is This

Standalone T-Deck LVGL firmware that runs in the MeshCore mesh network. Think "Discord UI on a LoRa radio." Full mesh protocol compatibility — interoperates with any MeshCore node.

Three repos form the SigurdOS ecosystem:

| Repo | What | Stack |
|------|------|-------|
| `hermes-gadget/SigurdOS` | MeshCore fork (core library) | C++/PlatformIO, ESP32 |
| **`hermes-gadget/SigurdOS-tdeck`** ← **you are here** | T-Deck LVGL firmware | C++/PlatformIO, LVGL v9, LovyanGFX |
| `hermes-gadget/SigurdOS-client` | Flutter mobile app | Dart/Flutter, BLE/USB/TCP |

---

## Quick Start

```bash
git clone --recurse-submodules git@github.com:hermes-gadget/SigurdOS-tdeck.git
cd SigurdOS-tdeck

# Run all native tests (no hardware, fast — do this repeatedly)
pio test -e native_test -v

# Run one test module
pio test -e native_test -f test_keyboard -v

# Build firmware
pio run -e SigurdOS_TDeck

# Check test count (varies as tests are added)
pio test -e native_test --list-tests
```

MeshCore is at `lib/meshcore/` — a git submodule. `git submodule update --init` if you cloned without `--recurse-submodules`.

---

## Architecture

```
src/
├── main.cpp               # Boot: board→display→SPIFFS→splash→mesh→UI (deferred input init)
├── lv_conf.h              # LVGL v9 config (16-bit, partial render)
├── hal/
│   ├── tdeck_pins.h       # Full T-Deck pinout + SPI/I2C aliases + SIGURDOS_VERSION
│   ├── tdeck_board.h      # MainBoard impl (sleep, battery, power)
│   ├── display.cpp/h      # LovyanGFX ST7789 + LVGL + touch/keyboard callbacks
│   ├── display_buffer_policy.h  # Display buffer configuration policy
│   ├── battery.cpp/h      # ADC mV→%
│   ├── touch.cpp/h        # GT911 capacitive touch (I2C, 400 kHz)
│   ├── keyboard.cpp/h     # I2C keyboard (ESP32-C3 MCU at 0x55)
│   ├── keyboard_layouts.cpp/h  # Keyboard layout selection (QWERTY/AZERTY/etc.)
│   ├── trackball.cpp/h    # 5-direction trackball (debounce, event queue)
│   ├── prefs.cpp/h        # NodePrefs persisted in NVS (freq, SF, power, etc.)
│   ├── gps.cpp/h          # GPS NMEA parser
│   ├── gps_demand.h       # GPS power/demand control
│   ├── sdcard.cpp/h       # SD card init, status, path helpers
│   ├── spi_shared.cpp/h   # Shared SPI bus init (display + LoRa + SD)
│   ├── i2c_bus.cpp/h      # I2C bus abstraction (keyboard + touch on shared bus)
│   ├── buzzer.cpp/h       # GPIO buzzer (pin 46)
│   ├── wifi_ota.cpp/h     # WiFi + OTA update
│   ├── github_ota.cpp/h   # OTA from GitHub releases
│   ├── github_ota_plan.cpp/h  # OTA plan/rollback logic
│   ├── launcher_env.cpp/h # Launcher compatibility detection
│   ├── radio_profiles.cpp/h  # Radio frequency/profile presets
│   ├── storage.cpp/h      # Storage abstraction (SPIFFS wrapper, atomic writes)
│   ├── atomic_file.cpp/h  # Atomic file I/O operations (rename-based)
│   └── lv_pool.cpp        # LVGL object pool allocator
├── mesh/
│   ├── sigurd_mesh_v2.cpp/h  # SigurdMeshV2 — BaseChatMesh subclass (routing, channels, messages)
│   ├── mesh_wrapper.cpp/h # Public API for the UI layer (sigurdos::mesh::*)
│   ├── mesh_wrapper_internal.h  # Internal mesh wrapper API (not for UI)
│   ├── companion_adapter.cpp/h  # Companion BLE bridge adapter (separately compiled, included from mesh_wrapper.cpp)
│   ├── companion_message_policy.h  # Companion message filtering policy
│   ├── persistence_store.cpp/h  # On-disk state persistence (contacts, channels via SPIFFS)
│   ├── contact_store.cpp/h     # Contact management (add, remove, lookup, favourites)
│   ├── message_store.cpp/h     # Per-channel message storage + unread tracking
│   ├── channel_validation.cpp/h # Channel name/PSK input validation
│   ├── regions.cpp/h       # Flood-scope region management
│   ├── time_state.cpp/h    # Network time synchronisation
│   ├── advert_blob.h       # Advertise blob data structure
│   ├── cmd_response_queue.h    # Command response queue
│   ├── control_parser.h    # Control command parser
│   ├── durable_fanout.h    # Durable message fan-out for replay
│   ├── login_session.h     # Login session management (room servers)
│   ├── pending_ack_policy.h    # Pending ACK management/tracking policy
│   ├── scope_key_hex.h     # Scope key hex encoding helpers
│   └── public_channel.h    # Public channel PSK constant
├── ui/
│   ├── theme.h            # Colors, pixel helpers (apply_pixel_*), runtime theme system
│   ├── theme.cpp          # Theme apply from NVS + refresh all screens
│   ├── responsive.h       # Display-size-agnostic layout helpers
│   ├── home_screen.cpp/h  # 4x3 icon grid, top/bottom bars
│   ├── chat_screen.cpp/h  # Channels, DM, message bubbles
│   ├── chat_history_store.cpp/h  # Persistent chat history storage (per-channel caches)
│   ├── chat_message_buffer.cpp/h  # Chat message buffer for UI rendering
│   ├── chat_store_migration.h  # Migration helpers for chat store format changes
│   ├── screens.h          # Screen entry points (sigurdos::ui::*_screen_show)
│   ├── screens.cpp        # Screen create/show/dispatch implementations
│   ├── screens_common.cpp/h  # make_screen_full(), show_screen(), PIN gate, shared helpers
│   ├── screen_lifetime.cpp/h  # Screen lifecycle management (create/delete tracking)
│   ├── channel_menu.cpp/h # Long-press channel menu (rename, delete, info)
│   ├── contact_paging.h   # Paginated contact list helpers
│   ├── contact_list_power.h  # Contact list power management
│   ├── onboarding_screen.cpp/h  # First-boot setup wizard
│   ├── navigation.cpp/h   # Screen routing with slide transitions + back-swipe
│   ├── notifications.cpp/h  # Notification subsystem (four-entry queue, banner)
│   ├── notification_model.h  # Notification data model
│   ├── list_window.h      # Virtual list window helper for LVGL
│   ├── lv_timer_owner.h   # LVGL timer ownership/cleanup wrapper
│   ├── message_detail.h   # Message detail view helpers
│   ├── repeater_transcript.h  # Repeater transcript data
│   ├── bluetooth_help.h   # Bluetooth help text definitions
│   ├── display_settings_helpers.h  # Display settings helper constants
│   ├── ui.cpp/h           # Splash→Home transition
│   └── screens/           # Individual screen implementations
│       ├── screen_advertise.cpp
│       ├── screen_bluetooth.cpp
│       ├── screen_channels.cpp
│       ├── screen_contacts.cpp
│       ├── screen_finder.cpp
│       ├── screen_map.cpp
│       ├── screen_node_stats.cpp
│       ├── screen_node_status.cpp
│       ├── screen_packets.cpp
│       ├── screen_radio_setup.cpp
│       ├── screen_regions.cpp
│       ├── screen_repeaters.cpp
│       ├── screen_settings.cpp
│       ├── screen_settings_display.cpp
│       ├── screen_settings_gps.cpp
│       ├── screen_settings_radio.cpp
│       ├── screen_settings_system.cpp
│       ├── screen_signal.cpp
│       ├── screen_telemetry.cpp
│       ├── screen_terminal.cpp
│       ├── screen_trace.cpp
│       └── screen_wifi_networks.cpp
├── app/
│   ├── map_renderer.cpp/h # Offline map (PNG tiles via lodepng, PSRAM cache)
│   ├── lodepng_alloc.cpp  # lodepng allocator → PSRAM with DRAM fallback
│   ├── qr_show.cpp/h      # QR code display for identity/channel sharing
│   └── tile_cache.cpp/h   # Map tile LRU disk cache
├── comms/
│   ├── companion_bridge.cpp/h     # BLE companion protocol bridge (phone app)
│   ├── observed_ble_interface.cpp/h  # BLE transport for companion protocol
│   └── ble_frame_queue.h          # BLE frame queue for outbound data
├── diagnostics/
│   ├── debug.cpp/h        # Debug dumps (SIGURDOS_DEBUG=1 build)
│   ├── debug_cfg.h        # Debug feature flags (fine-grained control)
│   ├── build_info.cpp/h   # Compile-time build metadata (version, date, env)
│   ├── log.h              # Lightweight serial logging macros (see docs/LOGGING.md)
│   ├── telemetry.cpp/h    # Telemetry dispatch — fan-out to collectors, crash ring, HB ring
│   ├── telemetry_collectors.cpp/h  # Per-subsystem telemetry collection
│   ├── telemetry_crash.cpp/h      # Crash ring buffer (pre-mortem data)
│   ├── telemetry_hb_ring.cpp/h    # Heartbeat ring buffer
│   ├── telemetry_input.cpp/h      # Input event telemetry (trackball, keyboard, touch)
│   └── telemetry_protocol.cpp/h   # Telemetry binary protocol (serial framing)
├── fonts/
│   ├── emoji_font_setup.cpp # Emoji font fallback for LVGL
│   ├── emoji_data.cpp/h     # Emoji codepoint→glyph mapping
│   ├── emoji_font.h         # Emoji font data declarations
│   ├── latin_ext_font.h     # Latin Extended font data
│   ├── keyboard_layout_font.h  # Keyboard layout indicator font data
│   └── emoji_images/        # Rendered emoji bitmaps
│       ├── emoji_picker_images.h
│       └── emoji_picker_index.h
├── validation/
│   ├── gps_validation.cpp          # GPS NMEA validation + coordinate tests
│   └── gps_validation_wifi.cpp/h   # GPS + WiFi co-location validation
├── test/
│   └── test_controller.cpp/h  # Remote test controller (serial command interface)
└── utils/
    └── utf8_util.h        # UTF-8 byte-length and truncation-safe helpers

lib/    (repo root — NOT under src/)
├── meshcore/              # Git submodule → MeshCore (MeshCore firmware library)
├── lodepng/               # PNG decode library (zlib license, PSRAM allocators)
├── base64/                # Base64 encode/decode library
└── qrcode/                # QR code generation library
```

---

## Boot Sequence

The boot order (PR #625) is documented in detail in [`docs/HARDWARE.md`](docs/HARDWARE.md#appendix-a--boot-sequence). Key changes:

1. **Display init** before SPIFFS — LovyanGFX + LVGL start early so the splash screen is visible during storage mounts
2. **Splash status label** — `boot_status()` shows live progress (e.g., "Mounting storage...", "Starting radio...")
3. **Deferred input init** — touch, keyboard, and trackball initialised after preferences are loaded, not during display init

```text
 1. Serial.begin(115200)
 2. TDeckBoard::begin()           (peripherals, I2C)
 3. Battery + buzzer init
 4. Display init                  ← moved before SPIFFS
 5. Splash screen (live status)
 6. SPIFFS mount (or LittleFS via storage abstraction)
 7. Load NodePrefs, apply theme
 8. Deferred input init           ← touch/keyboard/trackball
 9. SD card init
10. GPS init
11. Mesh init (identity, radio)
12. UI state restore (chats)
13. Map init
```

---

## Hardware (LilyGo T-Deck)

| Peripheral | Interface | Key Details |
|------------|-----------|-------------|
| **ESP32-S3** | MCU | 240 MHz, 16 MB flash, 8 MB PSRAM |
| **LoRa SX1262** | SPI (shared) | NSS=9, SCK=40, MISO=38, MOSI=41, DIO1=45, RST=17, BUSY=13 |
| **Display ST7789** | SPI (shared) | CS=12, DC=11, backlight=42, 320x240. Native orientation = 240x320 portrait + rotation(1) |
| **Keyboard** | I2C (0x55) | ESP32-C3 MCU, 100 kHz. Key mode returns ASCII. Backlight via 0x01/0x02 commands |
| **Trackball** | GPIO | UP=3, DOWN=15, LEFT=1, RIGHT=2, CLICK=0. 5-direction + center press |
| **Touch GT911** | I2C (0x5D) | SDA=18, SCL=8, INT=16. 400 kHz. Transforms for rotation(1): SWAP_XY=true, MIRROR_X=false, MIRROR_Y=true |
| **Battery ADC** | GPIO 4 | Voltage divider, ADC_MULTIPLIER = 2 × 3.3 × 1000 |
| **Peripheral Power** | GPIO 10 | HIGH = peripherals on |
| **GPS** | UART (Serial1) | RX=43, TX=44, baud 38400 |
| **Buzzer** | GPIO 46 | Active high |
| **SD Card** | SPI (shared bus) | CS=39, shares SCK(40)/MOSI(41)/MISO(38) with LoRa/display. VFS at `/sdcard` via SPI+FATFS. T-Deck v1.0 uses CS=21; if your board has no SD detect, try pin 21. |

**Shared SPI bus:** Display, LoRa, and microSD all share SCK(40)/MOSI(41)/MISO(38) with different CS lines (display=12, LoRa=9, SD=39). SPI is initialized separately per device from their respective drivers — no single `SPI.begin()` call covers all three.

---

## UI Conventions

### Pixel / Blocky Theme

Deep black `#0F0F0F` background, cyan `#00BFFF` accents, zero radius, 2px minimum borders. Theme colors are runtime variables (`sigurdos::theme::BG_PRIMARY`, etc.) stored in NVS — they survive reboots and can be changed by the user via Settings → Display → Theme.

Six built-in theme presets: Default Cyan, Midnight Blue, Forest Green, Sunset Orange, Royal Purple, Amber Glow.

**Theme helpers** in `src/ui/theme.h` and `src/ui/theme.cpp`:
- `apply_dark_bg(obj)` — sets full-screen black (BG_PRIMARY)
- `apply_pixel_card(obj)` — BG_TERTIARY, 0 radius, 2px border
- `apply_pixel_card_accent(obj)` — same as pixel_card but with accent border
- `apply_pixel_btn(obj)` — filled ACCENT button
- `apply_pixel_btn_outline(obj)` — transparent with ACCENT border
- `apply_pixel_input(obj)` — input field, BG_INPUT, 2px border
- `apply_pixel_badge(obj)` — small accent badge (30% opacity)
- `apply_topbar_icon_btn(btn)` — themed back button styling, state-aware
- `apply_focus_style(obj)` — KEY focus border treatment
- `apply_card_style(obj)` — legacy alias for `apply_pixel_card`
- `create_signal_dots(parent, rssi)` — iOS-style signal strength row (5 dots)
- `rssi_to_dots(rssi)` — RSSI → active dot count (1-5)
- `theme_apply(id)` — switch to a theme preset (0-5)

All screens MUST call `apply_dark_bg()` and use helpers from `theme.h`. Do not hardcode colors.

### Screen Structure

Each screen has:
1. **Top bar** — BG_SECONDARY, 22px high, ← back button, channel hashtag snapshot, 24h time, signal dots
2. **Content area** — between top bar and bottom bar
3. **Bottom bar** — BG_SECONDARY, 20px high, device name/signal/battery

The `make_screen_full(title)` helper in `screens_common.cpp` creates all of this. Use it.

### Navigation

Navigation lives in `ui/navigation.h` — the `Screen` enum and all routing functions are there.

- `navigate_to(Screen)` — switches screens with slide animation, tracks previous
- `go_back()` — returns to previous screen (nav stack, 8-entry circular buffer)
- `show_screen(scr)` — loads a screen created with `lv_obj_create(nullptr)`
- `can_go_back()` — returns true if there's a previous screen to return to
- `current_screen()` — returns the Screen enum value currently displayed
- `handle_back_swipe(event)` — universal two-swipe back gesture

### Icons

The home screen tiles use `LV_SYMBOL_*` (FontAwesome bundle built into LVGL v9):

| Tile | Symbol | Routes to | Notes |
|------|--------|-----------|-------|
| CHATS | `LV_SYMBOL_ENVELOPE` | Screen::Chat | Primary tile (bold) — opens chat screen with channel list + DMs |
| DMs | `LV_SYMBOL_FILE` | Screen::Chat | Opens chat screen focused on DMs |
| ROOMS | `LV_SYMBOL_DIRECTORY` | Screen::Contacts | Opens contacts filtered to room servers |
| CONTACTS | `LV_SYMBOL_CALL` | Screen::Contacts | All contacts, alphabetical |
| REPEATERS | `LV_SYMBOL_WIFI` | Screen::Repeaters | Network repeater/node list |
| ADVERTISE | `LV_SYMBOL_BELL` | Screen::Advertise | Broadcast presence |
| MAP | `LV_SYMBOL_GPS` | Screen::Map | Offline map viewer |
| TERMINAL | `LV_SYMBOL_KEYBOARD` | Screen::Terminal | Serial-style command terminal |
| PACKETS | `LV_SYMBOL_LIST` | Screen::Heard | Raw packet log (heard nodes) |
| SETTINGS | `LV_SYMBOL_SETTINGS` | Screen::Settings | Device configuration |
| SETUP | `LV_SYMBOL_HOME` | Screen::Onboarding | First-time setup wizard |
| SIGNAL | `LV_SYMBOL_BARS` | Screen::Signal | Live RSSI/SNR signal strength |

### All Screens

| # | Screen | Source | Status |
|---|--------|--------|--------|
| 0 | Splash (boot screen, not in nav enum) | `ui.cpp` | ✅ |
| 1 | Home (4×3 icon grid) | `home_screen.cpp` | ✅ |
| 2 | Chat (channels + DMs, message bubbles) | `chat_screen.cpp` | ✅ |
| 3 | Contacts (alphabetical, tap→DM) | `screens/screen_contacts.cpp` | ✅ |
| 4 | Channels (list + create #hashtag/PSK) | `screens/screen_channels.cpp` | ✅ |
| 5 | Network / Finder (nearby node discovery) | `screens/screen_finder.cpp` | ✅ |
| 6 | Heard / Packets (raw packet log, home tile "PACKETS") | `screens/screen_packets.cpp` | ✅ |
| 7 | Repeaters (network node list, home tile "REPEATERS") | `screens/screen_repeaters.cpp` | ✅ |
| 8 | Map (touch pan, auto-center) | `screens/screen_map.cpp` | ✅ |
| 9 | Advertise (broadcast presence) | `screens/screen_advertise.cpp` | ✅ |
| 10 | Settings (top-level) | `screens/screen_settings.cpp` | ✅ |
| 11 | Trace (path discovery per contact) | `screens/screen_trace.cpp` | ⚠️ see docs/KNOWN_ISSUES.md |
| 12 | Terminal (colored log + commands, 64-line cap) | `screens/screen_terminal.cpp` | ⚠️ see docs/KNOWN_ISSUES.md |
| 13 | Signal (live RSSI, SNR, radio params) | `screens/screen_signal.cpp` | ✅ |
| 14 | Radio Setup (freq, SF, BW, CR, power) | `screens/screen_radio_setup.cpp` | ✅ |
| 15 | Onboarding (wizard) | `onboarding_screen.cpp` | ✅ |
| 16 | Contact Detail (tap contact → info) | `screens/screen_contacts.cpp` | ✅ |
| 17 | Settings → Radio | `screens/screen_settings_radio.cpp` | ✅ |
| 18 | Settings → GPS | `screens/screen_settings_gps.cpp` | ✅ |
| 19 | Settings → Display | `screens/screen_settings_display.cpp` | ✅ |
| 20 | Settings → System | `screens/screen_settings_system.cpp` | ✅ |
| 21 | Node Stats (packet counters) | `screens/screen_node_stats.cpp` | ✅ |
| 22 | Telemetry (sensor data) | `screens/screen_telemetry.cpp` | ✅ |
| 23 | Node Status (repeater stats) | `screens/screen_node_status.cpp` | ✅ |
| 24 | WiFi Networks (scan + connect) | `screens/screen_wifi_networks.cpp` | ✅ |
| 25 | Bluetooth (companion BLE) | `screens/screen_bluetooth.cpp` | ✅ |
| 26 | Regions (flood scope) | `screens/screen_regions.cpp` | ✅ |

---

## Mesh Integration

MeshCore is a submodule at `lib/meshcore/`. The mesh subclass is `SigurdMeshV2` in `sigurd_mesh_v2.h` — it extends `BaseChatMesh` (not the old raw `::mesh::Mesh`). The UI never touches MeshCore or SigurdMeshV2 directly — all calls go through `sigurdos::mesh::*` in `mesh_wrapper.h`.

**Key mesh wrapper API (see `src/mesh/mesh_wrapper.h` for the complete 500+ line API):**

```cpp
// ── Init / Loop ──────────────────────────────
sigurdos::mesh::init(spiffs_ok)          // Boot — load identity, start radio
sigurdos::mesh::loop()                   // Call from main loop

// ── Messaging ────────────────────────────────
sigurdos::mesh::sendMessage(name, text)  // Direct message (returns ACK timestamp)
sigurdos::mesh::sendChannelMessage(ch, text)  // Group message
sigurdos::mesh::sendMessageWithScopeKey(name, text, key16)  // Scoped DM
sigurdos::mesh::sendChannelMessageWithScopeKey(ch, text, key16)  // Scoped group message
sigurdos::mesh::sendAnonMessage(pubkey_hex, text)  // DM by public key (no contact needed)
sigurdos::mesh::sendRoomMessage(contact, channel, text)  // Room server message
sigurdos::mesh::injectMessage(...)       // Test-only: simulate incoming msg
sigurdos::mesh::pollMessages(out, max)   // Poll incoming messages queue
sigurdos::mesh::pendingMessageCount()    // Pending message backlog
sigurdos::mesh::getQueueDropCount()      // Messages dropped due to full queue
sigurdos::mesh::getUnreadMessageCount()  // Unread message count
sigurdos::mesh::resetUnreadMessageCount()

// ── Channels ─────────────────────────────────
sigurdos::mesh::addChannel(name, psk_b64)     // PSK channel
sigurdos::mesh::addHashtagChannel(name)       // Hash-of-name channel
sigurdos::mesh::joinPublicChannel()           // Join "Public" channel
sigurdos::mesh::removeChannel(idx)            // Remove channel by index
sigurdos::mesh::exportChannels(out, max)      // Channel name list
sigurdos::mesh::saveChannels() / loadChannels()

// ── Contacts ─────────────────────────────────
sigurdos::mesh::addContactManual(name, pubkey_hex, type)  // Add by pubkey
sigurdos::mesh::exportContacts(out, max)      // Name list
sigurdos::mesh::exportContactsFull(out, max)  // Full ContactInfo list
sigurdos::mesh::getContactCount()             // Number of contacts
sigurdos::mesh::getContactByName(name, out)   // Lookup by name
sigurdos::mesh::removeContact(name)           // Remove contact
sigurdos::mesh::isContactFavourite(name) / setContactFavourite(name, fav)
sigurdos::mesh::importContactByUri(uri)       // Import from meshcore:// URI
sigurdos::mesh::addChannelByUri(uri)          // Add channel from meshcore:// URI
sigurdos::mesh::getContactPubkeyHex(name, out, sz)  // Pubkey for QR sharing
sigurdos::mesh::setOwnName(name) / getOwnName()

// ── Signal / Stats ───────────────────────────
sigurdos::mesh::getNoiseFloor()              // Current noise floor dBm
sigurdos::mesh::getLastRSSI() / getLastSNR() // Signal metrics
sigurdos::mesh::getTotalTxAirtimeMs() / getTotalRxAirtimeMs()
sigurdos::mesh::getNumSentFlood() / getNumSentDirect()
sigurdos::mesh::getNumRecvFlood() / getNumRecvDirect()
sigurdos::mesh::resetPacketStats()
sigurdos::mesh::getSignalHistoryCount/RSSI/SNR(idx)  // Sparkline data
sigurdos::mesh::getPacketLogCount/Generation/Entry(idx)  // Packet log

// ── Advertise ────────────────────────────────
sigurdos::mesh::sendAdvert()               // Broadcast presence
sigurdos::mesh::getLastAdvertTime/Success/UsedGps()

// ── Radio Config ─────────────────────────────
sigurdos::mesh::applyRadioParams(freq, bw, sf, cr, power, rx_gain)
sigurdos::mesh::revertRadioParams()

// ── Persistence ──────────────────────────────
sigurdos::mesh::saveState()                // Persist contacts + channels
sigurdos::mesh::saveContacts() / loadContacts()
sigurdos::mesh::saveContactsIfDue(now)     // Periodic auto-save
sigurdos::mesh::reloadContactsAfterIdentityChange()
sigurdos::mesh::shutdown()                 // Graceful shutdown
sigurdos::mesh::factoryReset()             // Wipe all persisted state

// ── Time ─────────────────────────────────────
sigurdos::mesh::getCurrentTime()
sigurdos::mesh::setSystemTime(epoch)
sigurdos::mesh::getCurrentLocalDateTime(y, m, d, h, min)
sigurdos::mesh::makeEpoch(y, month, d, h, min)

// ── Request/Response (Phase 4) ───────────────
sigurdos::mesh::requestStatus(name)        // Request repeater status blob
sigurdos::mesh::requestTelemetry(name)     // Request sensor telemetry
sigurdos::mesh::sendRequest(name, type)    // Generic request
sigurdos::mesh::sendRequestWithData(name, data, len)  // Request with payload
sigurdos::mesh::discoverPath(name)         // Flood path discovery
sigurdos::mesh::sendTrace(contact_idx, tag_out)  // Trace route
sigurdos::mesh::findContactIndex(name)     // Contact index for trace
sigurdos::mesh::hasTraceResult() / getTracePathLen() / getTracePath()
sigurdos::mesh::sendLogin(name, pwd)       // Login to repeater/room
sigurdos::mesh::sendCommand(name, text)    // Send admin command to repeater
sigurdos::mesh::sendPingNearby()           // Ping nearby nodes
sigurdos::mesh::sendRoomMsgFetchRequest(contact, channel)

// ── Regions ──────────────────────────────────
sigurdos::mesh::listRegions(out, max)
sigurdos::mesh::addRegion(name, parent)
sigurdos::mesh::removeRegion(name)
sigurdos::mesh::setActiveRegion(name)
sigurdos::mesh::getActiveRegion()
sigurdos::mesh::syncRegionsFromChannels()

// ── Companion BLE ────────────────────────────
sigurdos::mesh::companionBleAvailable/Enabled/Connected()
sigurdos::mesh::companionBleSetEnabled(bool)
sigurdos::mesh::companionBleLastSyncTime/Pin()

// ── Identity ─────────────────────────────────
sigurdos::mesh::exportIdentity(hex_out, sz)
sigurdos::mesh::importIdentity(hex_privkey)
sigurdos::mesh::signMessage(data, sig_out)

// ── Group Data ───────────────────────────────
sigurdos::mesh::sendGroupDataToChannel(idx, type, data, len)
sigurdos::mesh::getGroupDataRecvCount/Entry() / clearGroupDataRecv()
```

**Messages arrive** via `chat_screen_add_msg(channel, sender, text, is_self)`. The chat screen maintains per-channel message caches (8 messages each, 16 channels max) backed by `chat_history_store` for persistence.

**Channel protocol:**
- **PSK channels:** "Public" uses PSK `izOH6cXN6mrJ5e26oRXNcg==`. Any node with the matching PSK derives the same channel hash.
- **Hashtag channels:** No PSK needed. Channel hash = SHA256(SHA256(name)). Any node using the same hashtag name creates the same hash.
- Group messages embed `"<sender_name>: <text>"` for interop with MeshCore companion radio firmware.

---

## Testing

```bash
pio test -e native_test -v       # All tests (no hardware, 77 test modules)
pio test -e native_test -f test_touch -v     # One module
```

**Critical rules:**
- Tests use `test/test_<name>/` dirs with `main.cpp` entry points. Wrong naming = not discovered.
- Hardware is mocked in `test/mocks/` (Arduino.h, lvgl.h, RadioLib.h, LovyanGFX.hpp, Wire.h, MeshCore.h, esp_heap_caps.h, esp_partition.h, Stream.h, SPIFFS.h, mesh_helpers.h, mock_arduino.cpp, mock_esp_partition.cpp, mock_fonts.cpp, mock_mesh_wrapper.cpp, mock_prefs.cpp, mock_spiffs.cpp)
- `mock_prefs.cpp` provides byte-level NVS prefs stubs when HAL modules depend on prefs.
- `mock_spiffs.cpp` provides SPIFFS persistence stubs for storage-dependent tests.
- Always run tests before pushing. A PR with failing tests is rejected.

**New code = new tests.** Minimum:
- HAL changes → add mock + test (e.g., `test_trackball` for trackball HAL)
- New screen → navigation test + message display test
- API changes → update existing mock stubs

---

## Build & Tools

```bash
# Build firmware (release — no serial debug output)
pio run -e SigurdOS_TDeck

# Build with BLE validation extras
pio run -e SigurdOS_TDeck_ble_validation

# BLE agent build
pio run -e SigurdOS_TDeck_ble_agent

# Debug build (full boot sequence + periodic diagnostics over serial)
pio run -e SigurdOS_TDeck_debug

# Trackball debug build (raw GPIO state visible)
pio run -e SigurdOS_TDeck_trackball_debug

# MeshV2 build (SigurdMeshV2)
pio run -e SigurdOS_TDeck_meshv2

# Telemetry build (telemetry subsystem enabled)
pio run -e SigurdOS_TDeck_telemetry

# Telemetry diff (comparative) build
pio run -e SigurdOS_TDeck_telemetry_diff

# Remote test (no radio — serial-controlled input simulation)
pio run -e SigurdOS_TDeck_remote_test

# Remote test with radio enabled (various frequency variants)
pio run -e SigurdOS_TDeck_remote_test_radio
pio run -e SigurdOS_TDeck_remote_test_radio_testfreq
pio run -e SigurdOS_TDeck_remote_test_radio_roomtest
pio run -e SigurdOS_TDeck_remote_test_radio_meshv2

# GPS validation builds
pio run -e SigurdOS_TDeck_gps_validation
pio run -e SigurdOS_TDeck_gps_validation_wifi

# Companion USB mode
pio run -e SigurdOS_TDeck_companion_usb

# Run native tests (no hardware)
pio test -e native_test -v

# Run native tests with address sanitizer
pio test -e native_sanitize -v
```

Additional debug variants exist for focused subsystems: `_debug_ui`, `_debug_map`, `_debug_display`, `_debug_mesh`, `_debug_diag`, `_debug_869`.

### Serial Debugging

All `Serial.print`/`printf` output goes through **USB CDC** (`/dev/ttyACM0`). This is controlled by `ARDUINO_USB_CDC_ON_BOOT=1` in `platformio.ini` — do not change it back to 0 without ensuring GPS pins 43/44 don't conflict.

Local (USB cable connected directly):
```bash
pio device monitor -b 115200
```

Remote (if a hardware gateway is available for USB device access):
```bash
# Live tail — replace <host> and <port> as appropriate
ssh <gateway-host> "stty -F <serial-port> 115200 raw -echo && cat <serial-port>"

# Capture full boot (reset + read)
ssh <gateway-host> 'python3 -c "
import serial, time
s = serial.Serial(\"<serial-port>\", 115200, timeout=10)
s.setDTR(False); s.setRTS(False); time.sleep(0.1)
s.setDTR(True); time.sleep(0.1); s.setDTR(False)
time.sleep(2)
t0 = time.time()
while time.time() - t0 < 8:
    c = s.read(1)
    if c: print(c.decode(\"utf-8\", errors=\"replace\"), end=\"\", flush=True)
s.close()
"'
```

The debug build (`SigurdOS_TDeck_debug`) enables:
- Step-by-step boot logging (`[boot] step N: ...`)
- Periodic heap/PSRAM/battery stats (`[stat]`)
- Display flush tracking (`[flush] #N`)
- Trackball pin states (`[pins]`)
- On-demand debug dump via `sigurdos_debug_dump()`
- Map tile discovery logging

The release build (`SigurdOS_TDeck`) suppresses all of these via `NDEBUG` and the absence of `SIGURDOS_DEBUG=1`. Only critical errors and warnings print in release mode.

### Remote Test Controller

**⚠️ CRITICAL: Do NOT use this mode without explicit user consent.** This disables the LoRa radio and makes the device a serial-controlled input simulator. Only the user decides when to use this mode. Never switch to `SigurdOS_TDeck_remote_test` build env unless the user asks you to or explicitly approves it.

Enables automated and manual testing over serial (USB CDC). No LoRa radio is initialised — all mesh messages are simulated via injection. The radio hardware is never touched.

Build with:
```bash
pio run -e SigurdOS_TDeck_remote_test
```

Flash and connect (local):
```bash
pio run -e SigurdOS_TDeck_remote_test -t upload
pio device monitor -b 115200
```

Flash and connect (remote via hardware gateway):
```bash
scp .pio/build/SigurdOS_TDeck_remote_test/firmware-merged.bin <gateway>:/tmp/
ssh <gateway> "esptool --chip esp32s3 --port <serial-port> --baud 921600 write-flash 0x0 /tmp/firmware-merged.bin && rm /tmp/firmware-merged.bin"
ssh <gateway> "stty -F <serial-port> 115200 raw -echo && cat <serial-port>"
```

Once connected, the T-Deck shows a test controller banner. Type commands directly in the serial terminal:

| Command | Example | Description |
|---------|---------|-------------|
| `help` | `help` | Show command list |
| `nav chat` | `nav chat` | Navigate to screen (home/chat/contacts/channels/network/heard/map/settings/terminal/radio/trace/signal/advertise) |
| `back` | `back` | Go back in nav stack |
| `tb up` | `tb click` | Simulate trackball (up/down/left/right/click, or u/d/l/r/c) |
| `type hello` | `type Hello World` | Type text — queued and injected one char per loop cycle |
| `press enter` | `press backspace` | Press special key (enter/backspace/esc/tab) |
| `inject Alice Hello!` | `inject Bob channel=general hi` | Simulate incoming mesh message (no radio!) |
| `screen` | `screen` | Show current screen name |
| `status` | `status` | Show heap and PSRAM |
| `debug <1\|2\|3>` | `debug 1` | Set debug verbosity (1=quiet, 2=normal, 3=verbose) |

Safety guarantees:
- No LoRa radio initialised — `sigurdos::mesh::init()` is never called
- All `sendMessage`, `sendChannelMessage`, `sendAdvert` return false (g_mesh is null)
- Radio accessors (`getLastRSSI`, `getLastSNR`, `getNoiseFloor`) return dummy values
- No SPI transactions ever reach the SX1262 hardware

**⚠️ LIMITATION: Cannot test physical input hardware.** Remote test mode simulates trackball, keyboard, and touch programmatically — events are injected directly into the input queues. This means it **cannot** validate:
- GPIO debounce timing, edge detection, or signal quality
- Physical switch feel or actuation
- I2C bus timing or peripheral detection
- Trackball direction sensitivity or deadtime behavior
- Any issue where the root cause is in the physical layer (pin states, interrupts, pull resistors)

If the issue involves physical input hardware (trackball, keyboard, touch, buttons), remote test mode is not appropriate — it needs real hardware testing on the device.

### Debug Levels

The debug build system (`SIGURDOS_DEBUG=1`) supports three verbosity levels, controlled at build time via `-D SIGURDOS_DEBUG_LEVEL=N` or at runtime via the `debug <1|2|3>` serial command:

| Level | Name | Output | Default Env |
|-------|------|--------|-------------|
| 1 | Quiet | Only `[test]` responses from the test controller. No `[flush]`, `[stat]`, or `[pins]` output. | `SigurdOS_TDeck_remote_test` |
| 2 | Normal | All debug output: `[flush]` per frame, `[stat]` + `[pins]` every 5s. | `SigurdOS_TDeck_debug` |
| 3 | Verbose | Level 2 output plus on-demand heavy dumps (`dump_system`, object tree, etc.). | — |

When any `SIGURDOS_DEBUG` build is running, the display auto-off timer is disabled — the screen stays on so you can observe behavior without needing to wake it. This only applies to debug builds; release builds retain the 30-second auto-off.

---

## Versioning & Release

Dev-only branch model:
- `dev` — integration branch. All PRs merge here. Releases are tagged directly on `dev`.
- There is no `main` branch. See CONTRIBUTING.md.
- Tags: `beta-0.1.XX` (zero-padded for correct sort: `beta-0.1.09` not `beta-0.1.9`)

**Release flow (maintainer only):**
1. Update `SIGURDOS_VERSION` in `tdeck_pins.h` (current: `beta-0.1.44-RC6`)
2. `pio run -e SigurdOS_TDeck`
3. `cp .pio/build/SigurdOS_TDeck/firmware-merged.bin firmware/firmware-merged.bin`
4. Commit, tag, push, `gh release create`

---

## AI Agent Workflow

When working on this codebase, follow this sequence:

1. **Open an issue on the upstream repo** — check if an open issue on `hermes-gadget/SigurdOS-tdeck` already covers what you plan to do. If not, create one. No issue = no PR accepted.
2. **Read `CONTRIBUTING.md`** — follow every step. It applies to AI agents the same as human contributors.
3. **Load context** — read `AGENTS.md` (or `CLAUDE.md`, they are identical), `docs/KNOWN_ISSUES.md`, and any relevant source files
4. **Check the branch** — work is always on `dev`. PRs target `dev`, not `main`
5. **Run tests first** — `pio test -e native_test` before any changes to confirm baseline
6. **Make changes** — use the file tools (`read_file`, `patch`, `write_file`)
7. **Run tests again** — all tests must pass
8. **Build firmware** — `pio run -e SigurdOS_TDeck` must succeed
9. **Commit and push** — conventional commit messages (`feat:`, `fix:`, `docs:`, etc.)

### Bug Spotting

If you find a bug while working that is not directly related to your PR, do not ignore it. Add it to `docs/KNOWN_ISSUES.md` using the standard format below. This lets the project catch bugs faster — you found it, you document it, and a maintainer validates it during PR review.

**Standard entry format — insert a new section in `docs/KNOWN_ISSUES.md`:**

```
## Category Area (e.g. GPS, Terminal, Chat Screen)

### Short specific title — one line describing the issue
One or two paragraphs explaining what happens, under what conditions, and why. Include the source file and relevant line numbers if known.

**What's needed:** Concrete description of what a fix would look like — approach, trade-offs, and any pitfalls to avoid.
```

Entries are separated by `---`. Place new entries under the right category heading or create a new category heading if none fits.

**Examples from existing entries:**

```
## Terminal

### Terminal labels — fixed (64-line cap exists)
Each command used to create a new LVGL label with no pruning. This has been fixed with a capped 64-line terminal log buffer. The `screen_terminal.cpp` implementation now recycles labels within a fixed pool.
```

```
## GPS

### GPS NMEA checksum — now validated
The NMEA parser in `gps.cpp:288-318` validates the `*XX` checksum suffix before parsing. Sentences with missing or incorrect checksums are discarded.

**What's needed:** N/A — implemented in current code.
```

**Important:** Only add issues you have actually observed or can clearly demonstrate from reading the code. Do not add speculative bugs. The maintainer will verify your entry during PR review — if it does not hold up, the entry will be removed before merging.

### Code Audit Checklist

Before submitting a PR (or before merging someone else's), use this checklist to catch common failure modes in embedded C++/ESP32/LVGL code. If you are an AI agent contributing code, run through this before pushing.

#### Buffer Safety
- [ ] `strncpy` — does every call manually null-terminate? (`dest[n-1] = '\0'`)
- [ ] `snprintf` — is the buffer size correct? (including null terminator)
- [ ] `lv_textarea_set_max_length` — is the limit ≤ buffer size - 1?
- [ ] UTF-8 truncation — does any byte-level truncation risk splitting multi-byte characters mid-codepoint?
- [ ] Message payloads — is null-termination unconditional (not `if (len > 1) ...`)?
- [ ] Stack buffers — any large local arrays on stack that should be `static` or heap?

#### Logic & Edge Cases
- [ ] Hash/crypto comparisons — full `memcmp`, not single-byte prefix match
- [ ] Rate limiting — enforced at the API layer, not just the UI
- [ ] Overflow guards — `uint8_t` counters that wrap without resetting (e.g. `save_counter`)
- [ ] Contact/array eviction — does a full list drop new entries silently? (LRU or TTL needed)
- [ ] Navigation history — does a circular buffer overwrite without wrapping `pop`?
- [ ] GPIO edge detection — falling-edge only for all directions (not LEFT on both edges)
- [ ] Hardware init ordering — dependencies initialised before consumers (backlight before panel, LVGL tick before timer handler)

#### UI / Theme Compliance
- [ ] `apply_dark_bg()` called on every screen background
- [ ] Colors from `theme.h` constants, not hardcoded
- [ ] Zero radius throughout (no `border-radius`, no pill shapes)
- [ ] Bottom bar and top bar use `make_screen_full()` or equivalent
- [ ] No `\\n` literal where real newline was intended

#### Concurrency / Timing
- [ ] `lv_obj_del` in event handler — should be `lv_obj_del_async()`
- [ ] Auto-delete screens — `lv_scr_load_anim(..., true)` deletes all children; `LV_EVENT_DELETE` needed to null globals
- [ ] LVGL tick starvation — any blocking operations that delay `lv_timer_handler()`?
- [ ] `ESP.restart()` without flash write delay — SPIFFS/NVS write may not have completed

#### Testing
- [ ] Tests added or updated for every change
- [ ] `pio test -e native_test -v` passes (all tests)
- [ ] `pio run -e SigurdOS_TDeck` builds without error
- [ ] PlatformIO discovers test dirs correctly (`test/test_<name>/main.cpp`)
- [ ] PR body declares how hardware testing was done: "Remote test" (serial-controlled), "Physical hardware test", or both. If neither is stated, auto-decline.

#### Known Issue Detection
- [ ] Does this PR fix a documented issue in `docs/KNOWN_ISSUES.md`? If so, remove that section.
- [ ] Does the PR add new entries to `docs/KNOWN_ISSUES.md`? Verify each one is real — check the source code or reproduce the issue. Remove any that are speculative.
- [ ] Did testing reveal new issues? If so, add them to `docs/KNOWN_ISSUES.md`.
- [ ] Does the PR introduce a new dependency? Check GPL-3.0 compatibility.
- [ ] Is there any comment saying "this might break" or "temporary fix"? Investigate before merging.

### PR & Review Workflow

**For reviewers (maintainer only beyond step 5):**
1. List PRs: `gh pr list --repo hermes-gadget/SigurdOS-tdeck --state open`
2. Check diff: `gh pr diff N` or `git fetch origin pull/N/head:pr-N && git diff dev...pr-N`
3. Verify PR body declares testing method — "Remote test" (serial-controlled), "Physical hardware test", or both. If missing, decline with: "PR must state how hardware testing was done — remote test, physical hardware, or both."
4. Build: `pio run -e SigurdOS_TDeck`
5. Test: `pio test -e native_test -v` (all tests must pass)
6. Run the [Code Audit Checklist](#code-audit-checklist) — check every applicable item
7. If the PR came from an AI agent: read the full diff for logic errors beyond what the agent self-checked. Agents miss subtle race conditions and edge-case buffer overflows.
8. Merge: `gh pr merge N --squash --delete-branch --repo hermes-gadget/SigurdOS-tdeck`
9. If merge fails (conflicts): cherry-pick new commits only, or squash-merge locally
10. If PR branch has stale commits: cherry-pick new commits onto dev, close PR
11. Close the related issue with notes describing what was done

### Rejection triggers

| Trigger | Why |
|---------|-----|
| Unconditional `Serial.printf` without `#if defined(SIGURDOS_DEBUG)` guard | Leaks debug output to production builds |
| Test suite not passing | Any single failure rejects the PR |
| Hardcoded colors instead of theme constants | Breaks the pixel theme — use `theme.h` |
| Missing `apply_dark_bg()` on screen backgrounds | Background won't match the dark theme |
| `\\n` literal instead of real newline | Prints literal backslash-n, not a line break |
| Stack-local arrays used as LVGL event `user_data` | Dangling pointer on click — use `static` arrays |
| Byte-level truncation of UTF-8 text | Can split multi-byte emoji mid-codepoint, sending invalid UTF-8 over mesh |
| Unconditional null-termination missing on short message payloads | `strlen` reads past buffer into stack garbage |
| `memcmp`-required comparisons using single-byte prefix | ~11% collision rate on 8-channel hash — wrong decryption keys selected |
| `uint8_t` counters that increment forever without resetting | Save counter wraps after 255 iterations, stopping persistence for ~2 hours |
| Screens with unbounded widget accumulation | Thousands of orphan LVGL labels leak heap — cap at 64 |
| `ESP.restart()` without delay for pending flash writes | SPIFFS/NVS save may be lost on reboot |
| PR body missing hardware testing declaration | Cannot verify the change works on real hardware — must state "Remote test", "Physical hardware test", or both |
| New dependency without GPL-3.0 compatibility check | License incompatibility blocks the entire project |
| Commented-out code, dead code paths, or "temporary fix" markers | Unmaintainable — no such thing as permanent temporary code |

### For Contributors

| Rule | Detail |
|------|--------|
|| **Branch from `dev`** | There is no `main` branch — releases are tagged on `dev`. PRs target `dev`. |
| **Pull regularly** | Before starting work: `git checkout dev && git pull origin dev` |
| **Rebase your feature branch** | Before opening a PR: `git rebase dev` to avoid stale-commit noise |
| **Run the full test suite** | `pio test -e native_test` must pass all tests before pushing |
| **Read `docs/KNOWN_ISSUES.md`** | Before starting feature work to avoid duplicating effort |
| **Follow screen conventions** | New screens need `apply_dark_bg()`, `make_screen_full()`, and consistent pixel helpers |

---

## Gotchas & Pitfalls

| Gotcha | Details |
|--------|---------|
| Display init | ST7789 native = 240x320 portrait. Set panel 240x320, then `rotation(1)` for 320x240 landscape |
| Backlight | `_panel.setLight(&_light)` before `setPanel(&_panel)` — missing = black screen |
| LVGL tick | `lv_tick_set_cb()` after `lv_init()` — missing = refresh never fires |
| Touch coords | After rotation(1): SWAP_XY=true, MIRROR_X=false, MIRROR_Y=true |
| Keyboard | `Wire.read()` returns `int`, store in `int` not `char` — 0xFF vs -1 collision |
| strncpy | Does NOT null-terminate if source >= n. Always `dest[n-1] = '\0'` |
| Auto-delete | `lv_scr_load_anim(..., true)` deletes old screen + all children. Register `LV_EVENT_DELETE` to null globals |
| `lv_obj_del` in handler | Use `lv_obj_del_async()` — event loop may dereference freed object |
| Stack in user_data | `lv_obj_add_event_cb(btn, handler, LV_EVENT_CLICKED, (void*)&local_array[i])` — garbage on click. Use `static` arrays |
| Radio init | `P_LORA_*` prefix, `Module*` constructor, SX126X_DIO3_TCXO_VOLTAGE=1.8f and other defines from variant |
| PSRAM LVGL | `LV_MEM_CUSTOM` with `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` — default 48 KB won't render maps |
| Webflasher bootloop | `flash_mode: "keep"` in manifest — the bootloader header is DIO (0x02), esptool.js writing "qio" breaks it |
| constexpr on ESP32 | xtensa GCC rejects multi-statement constexpr. Use `inline` for any non-trivial function |
| SPIFFS failure | If SPIFFS fails, identity is ephemeral — don't try to save on every boot |
| Radio first boot | Init radio with compile-time defaults for receive. Gate TRANSMIT behind `configured == true` |
| Wire.endTransmission | Always check return value — NACK on keyboard init means keyboard MCU is dead |
| saveState | Call every ~5 min from UI loop. Crashes lose new contacts if not saved |
|| GPS NMEA | NMEA checksum validation implemented in `gps.cpp:288-318` — corrupted sentences are discarded |
|| Terminal labels | 64-line cap implemented in `screen_terminal.cpp` — labels recycle, not leak |
|| Emoji truncation | Byte-level msg truncation can split 4-byte emoji — mitigated by `utf8_util.h` truncation helpers and emoji integrity tests |
|| SPI bus sharing | Display, LoRa, and SD card all share SPI2_HOST with separate CS lines. No SPI3 involved — all devices use SPI2_HOST/FSPI |
| Debug shadow mode | `SIGURDOS_TRACKBALL_DEBUG_SHADOW` drops trackball events instead of logging+forwarding |
| Debug.h header guards | Declaration in `debug.h` is unconditional, but `debug.cpp` wraps all implementation in `#if defined(SIGURDOS_DEBUG)` — latent linker risk |
| SIGURDOS_DEBUG scoping | `SIGURDOS_DEBUG` no longer forces radio parameters. Use `SIGURDOS_DEBUG_FORCE_RADIO_PARAMS` separately to override radio config at boot. |
