# SlopOS T-Deck — Agent Guide (Synced from Codebase)

**This file is auto-synced from the codebase.** It reflects the actual codebase state as of the last automated sync. Changes made by PRs will eventually be overwritten by the next sync — do not edit manually. The locked reference files are `AGENTS.md` and `CLAUDE.md`.

---

## Quick Start

```bash
git clone --recurse-submodules git@github.com:hermes-gadget/SlopOS-tdeck.git
cd SlopOS-tdeck

# Run all native tests (no hardware, fast — do this repeatedly)
pio test -e native_test -v

# Run one test module
pio test -e native_test -f test_keyboard -v

# Build firmware
pio run -e SlopOS_TDeck

# List all test modules
pio test -e native_test --list-tests
```

MeshCore is at `lib/meshcore/` — a git submodule. `git submodule update --init` if you cloned without `--recurse-submodules`.

---

## Architecture

```
src/
├── main.cpp               # Boot: board→display→mesh→UI
├── lv_conf.h              # LVGL v9 config (16-bit, partial render)
├── hal/
│   ├── tdeck_pins.h       # Full T-Deck pinout + SPI/I2C aliases
│   ├── tdeck_board.h      # MainBoard impl (sleep, battery, power)
│   ├── display.cpp/h      # LovyanGFX ST7789 + LVGL + touch/keyboard callbacks
│   ├── battery.cpp/h      # ADC mV→%
│   ├── touch.cpp/h        # GT911 capacitive touch (I2C, 400 kHz)
│   ├── keyboard.cpp/h     # I2C keyboard (ESP32-C3 MCU at 0x55)
│   ├── trackball.cpp/h    # 5-direction trackball (debounce, event queue)
│   ├── prefs.cpp/h        # NodePrefs persisted in NVS (freq, SF, power, etc.)
│   ├── gps.cpp/h          # GPS NMEA parser
│   └── sdcard.cpp/h       # SD card init, status, path helpers
├── mesh/
│   ├── slop_mesh.h        # Mesh subclass — routing, channels, message handling
│   ├── slop_mesh_v2.h     # MeshV2 (BaseChatMesh) — Phase 0 migration, RSSI/SNR history, ACK tracking
│   └── mesh_wrapper.cpp/h # Public API for the UI layer
├── ui/
│   ├── theme.h            # Colors, pixel helpers (apply_pixel_*)
│   ├── responsive.h       # Display-size-agnostic layout helpers
│   ├── home_screen.cpp/h  # 4x3 icon grid, top/bottom bars, battery/signal/time
│   ├── chat_screen.cpp/h  # Channels, DM, message bubbles
│   ├── screens.cpp/h      # Heard, Contacts, Contact Detail, Map, Settings, Trace, Terminal, Signal, Channels, Finder, Advertise, Radio Setup, Custom RF, Telemetry, Node Status
│   ├── onboarding_screen.cpp/h  # First-boot setup wizard
│   ├── navigation.cpp/h   # Screen routing with slide transitions, universal back-swipe
│   └── ui.cpp/h           # Splash→Home transition, main loop updates
├── app/
│   ├── map_renderer.cpp/h # Offline map (PNG tiles via lodepng, PSRAM cache)
│   ├── tile_cache.cpp/h   # Tile cache — LRU eviction (4 entries, uint64_t monotonic clock)
│   └── lodepng_alloc.cpp  # lodepng allocator → PSRAM with DRAM fallback
├── diagnostics/
│   ├── debug_cfg.h        # Per-feature debug flag selection (runtime toggle)
│   └── debug.cpp/h        # Debug dumps (SLOPOS_DEBUG=1 build)
├── fonts/
│   ├── emoji_font_setup.cpp    # Emoji font fallback registration for LVGL (header: emoji_font.h)
│   ├── emoji_font.c/h          # Compiled emoji font bitmap data (16px, Noto Emoji derivative)
│   ├── emoji_data.cpp/h        # Discord-style emoji short name ↔ UTF-8 lookup (343 entries)
│   └── emoji_images/           # Emoji picker image assets (generated)
│       ├── emoji_picker_images.h
│       └── emoji_picker_index.h
├── test/                  # Remote test controller (SLOPOS_REMOTE_TEST=1)
│   └── test_controller.cpp/h  # Serial-driven test harness — inject events, nav, type, screen capture
├── utils/                 # Utility modules
│   └── utf8_util.h       # UTF-8 safe truncation (emoji-aware byte cutting)
└── lib/
    ├── meshcore/            # Git submodule → MeshCore (at repo root)
    └── lodepng/             # PNG decode library (zlib license, PSRAM allocators)
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
| **Buzzer** | GPIO 46 | Active low |
| **SD Card** | SPI (shared bus) | CS=39, shares SCK(40)/MOSI(41)/MISO(38) with LoRa/display. VFS at `/sdcard` via SPI+FATFS. T-Deck v1.0 uses CS=21; if your board has no SD detect, try pin 21. |

**Shared SPI bus:** Display, LoRa, and microSD all share SCK(40)/MOSI(41)/MISO(38) with different CS lines (display=12, LoRa=9, SD=39). SPI is initialized separately per device from their respective drivers — no single `SPI.begin()` call covers all three.

---

## UI Conventions

### Pixel / Blocky Theme

Deep black `#0F0F0F` background, cyan `#00BFFF` accents, zero radius, 2px minimum borders.

**Runtime theme system:** Theme colors are now runtime-variable `inline uint32_t` values in `theme.h` (not `constexpr`). Six presets are defined in `slopos::theme::THEMES[]`:
- 0: Default Cyan, 1: Midnight Blue, 2: Forest Green, 3: Sunset Orange, 4: Royal Purple, 5: Amber Glow

Call `slopos::theme::theme_apply(id)` to switch themes. Theme ID is persisted in NVS via `NodePrefs.theme_id`. Changing theme applies to new screen creations immediately; existing screens may need to be re-entered to pick up new colors.

Theme helpers in `src/ui/theme.h`:
- `apply_dark_bg(obj)` — sets full-screen background (`BG_PRIMARY`)
- `apply_pixel_card(obj)` — `BG_TERTIARY`, 0 radius, 2px border
- `apply_pixel_btn(obj)` — filled `ACCENT` button
- `apply_pixel_btn_outline(obj)` — transparent with `ACCENT` border
- `apply_pixel_input(obj)` — input field, `BG_INPUT`, 2px border
- `apply_topbar_icon_btn(btn)` — themed back button styling, state-aware
- `apply_pixel_card_accent(obj)` — card with accent border
- `apply_pixel_badge(obj)` — accent-colored small badge (transparent fill)
- `apply_focus_style(obj)` — shared keyboard/trackball focus border treatment
- `create_signal_dots(parent, rssi)` — iOS-style signal strength dots (5 dots, ACCENT fill or TEXT_MUTED border)
- `rssi_to_dots(rssi)` — convert RSSI dBm to 1-5 active dots

All screens MUST call `apply_dark_bg()` and use helpers from `theme.h`. Do not hardcode colors.

### Screen Structure

Each screen has:
1. **Top bar** — `BG_SECONDARY`, 22px high, ← back button, channel hashtag snapshot, 24h time
2. **Content area** — between top bar and bottom bar
3. **Bottom bar** — `BG_SECONDARY`, 20px high, device name/signal/battery

The `make_screen_full(title)` helper in `screens.cpp` creates all of this. Use it.

### Navigation

- `navigate_to(Screen)` — switches screens with slide animation, tracks previous
- `go_back()` — returns to previous screen (nav stack, 8-entry circular buffer)
- `show_screen(scr)` — loads a screen created with `lv_obj_create(nullptr)` (used by sub-screens like Custom RF)
- `can_go_back()` — returns true if there's a previous screen to return to
- `current_screen()` — returns the currently active `Screen` enum value
- `handle_back_swipe(event)` — universal two-swipe commit back gesture. First Left event consumed (neutralise), second Left triggers `go_back()`. Non-Left events reset the counter. Call from trackball dispatch for screens without their own Left handler.

### Icons

Use `LV_SYMBOL_*` (FontAwesome bundle built into LVGL v9):

| Icon | Symbol | Use on screen |
|------|--------|-------------|
| CHATS | `LV_SYMBOL_ENVELOPE` | Chat |
| CONTACTS | `LV_SYMBOL_CALL` | Contacts |
| REPEATERS | `LV_SYMBOL_WIFI` | Heard (network) |
| FINDER | `LV_SYMBOL_EYE_OPEN` | Network |
| PACKETS | `LV_SYMBOL_LIST` | Heard (packet log) |
| MAP | `LV_SYMBOL_GPS` | Map |
| ADVERTISE | `LV_SYMBOL_AUDIO` | Advertise |
| SETTINGS | `LV_SYMBOL_SETTINGS` | Settings |
| TRACE | `LV_SYMBOL_SHUFFLE` | Trace |
| TERMINAL | `LV_SYMBOL_KEYBOARD` | Terminal |
| SETUP | `LV_SYMBOL_SETTINGS` | Onboarding |
| SIGNAL | `LV_SYMBOL_BARS` | Signal |

### All Screens

| # | Screen | Source | Status |
|---|--------|--------|--------|
| 0 | Splash | `ui.cpp` | ✅ |
| 1 | Home (4x3 grid, 12 tiles, unread badge on CHATS) | `home_screen.cpp` | ✅ |
| 2 | Chat (channels + DM, message search bar, ACK delivery ticks) | `chat_screen.cpp` | ✅ |
| 3 | Contacts (alphabetical, tap→DM, filtered to CHAT/ROOM types, RSSI+SNR per contact) | `screens.cpp` | ✅ |
| 4 | Channels (list + create #hashtag/PSK) | `screens.cpp` | ✅ |
| 5 | Finder / Network (Ping Nearby, nearby nodes list) | `screens.cpp` | ✅ |
| 6 | Packets / Heard (packet log, 50 entries, 5-column TIME/SOURCE/RSSI/SNR/TYPE headers) | `screens.cpp` | ✅ |
| 7 | Map (touch pan, auto-center, PSRAM tile cache) | `screens.cpp` | ✅ |
| 8 | Advertise (broadcast presence, status timer, send button) | `screens.cpp` | ✅ |
| 9 | Settings (menu → 4 sub-screens: Radio/Mesh, GPS/Location, Display/UI, System) | `screens.cpp` | ✅ |
| 10 | Trace (path discovery per contact) | `screens.cpp` | ✅ |
| 11 | Terminal (colored log + commands, 64 line cap) | `screens.cpp` | ✅ |
| 12 | Signal (live RSSI, SNR, noise floor, radio params from prefs, TX/RX airtime, packet statistics, RSSI sparkline chart) | `screens.cpp` | ✅ |
| 13 | Radio Setup (freq presets, SF/BW/CR/Pwr controls, save & reboot) | `screens.cpp` | ✅ |
| 14 | Onboarding (wizard) | `screens.cpp` | ✅ |
| 15 | Repeaters (infrastructure relay nodes only, filtered from contacts, RSSI+SNR per relay) | `screens.cpp` | ✅ |
| 16 | Contact Detail (full contact info: type, RSSI, SNR, last seen, path, location, RSSI sparkline, DM + Trace buttons, Request Status + Telemetry buttons) | `screens.cpp` | ✅ |
| 17 | Telemetry (request & display CayenneLPP sensor data — voltage, temperature from remote nodes) | `screens.cpp` | ✅ |
| 18 | Node Status (request & display remote node stats — battery, uptime, airtime, packet counters) | `screens.cpp` | ✅ |
| — | Custom RF (sub-screen of Radio Setup — Freq, SF, BW, CR, Pwr text inputs with Apply) | `screens.cpp` | ✅ |
| — | SettingsRadio (Radio/Mesh sub-screen — radio params, bandwidth/SF/tuning, duty cycle, RX gain) | `screens.cpp` | ✅ |
| — | SettingsGPS (GPS/Location sub-screen — GPS enable toggle, read interval, location sharing) | `screens.cpp` | ✅ |
| — | SettingsDisplay (Display/UI sub-screen — keyboard BL, display brightness, auto-off timeout, theme selector) | `screens.cpp` | ✅ |
| — | SettingsSystem (System sub-screen — node name, SD card, date/time, chat history cap, auto-add contacts, shut down) | `screens.cpp` | ✅ |

---

## Mesh Integration

MeshCore is a submodule at `lib/meshcore/`. The UI never touches MeshCore directly — all calls go through `slopos::mesh::*` in `mesh_wrapper.h`.

**Key mesh wrapper API:**

```cpp
slopos::mesh::init(spiffs_ok)              // Boot — load identity, start radio
slopos::mesh::loop()                       // Call from main loop
slopos::mesh::sendMessage(name, text)      // Direct message
slopos::mesh::sendChannelMessage(ch, text) // Group message
slopos::mesh::addChannel(name, psk_b64)    // PSK channel
slopos::mesh::addHashtagChannel(name)      // Hash-of-name channel
slopos::mesh::joinPublicChannel()          // Join "Public" with default PSK
slopos::mesh::exportContacts(out, max)     // Name list
slopos::mesh::exportContactsFull(out, max) // Name + RSSI + last_seen
slopos::mesh::exportChannels(out, max)     // Channel name list
slopos::mesh::pollMessages(out, max)       // Non-blocking message fetch
slopos::mesh::pendingMessageCount()        // Messages waiting in queue
slopos::mesh::getQueueDropCount()           // Queue overflow drops since boot
slopos::mesh::getUnreadMessageCount()       // Unread messages across all channels
slopos::mesh::resetUnreadMessageCount()     // Reset unread counter
slopos::mesh::setOwnName(name)             // Set this node's display name
slopos::mesh::getOwnName()                 // Get this node's display name
slopos::mesh::getNoiseFloor()              // Current noise floor dBm
slopos::mesh::getLastRSSI()                // Last received message RSSI
slopos::mesh::getLastSNR()                 // Last received message SNR
slopos::mesh::getTotalTxAirtimeMs()         // Cumulative TX airtime in ms
slopos::mesh::getTotalRxAirtimeMs()         // Cumulative RX airtime in ms
slopos::mesh::getNumSentFlood()             // Flood messages sent
slopos::mesh::getNumSentDirect()            // Direct messages sent
slopos::mesh::getNumRecvFlood()             // Flood messages received
slopos::mesh::getNumRecvDirect()            // Direct messages received
slopos::mesh::resetPacketStats()            // Reset all packet counters
slopos::mesh::getContactCount()            // Number of known contacts
slopos::mesh::findContactIndexByName(name) // Find contact index by exact name match
slopos::mesh::getChannelCount()            // Number of joined channels
slopos::mesh::sendAdvert()                 // Broadcast advert
slopos::mesh::getLastAdvertTime()          // Timestamp of last advert
slopos::mesh::getLastAdvertSuccess()       // Whether last advert succeeded
slopos::mesh::getLastAdvertUsedGps()       // Whether GPS data was included
slopos::mesh::saveState()                  // Save contacts to NVS
slopos::mesh::saveChannels()               // Save channels to NVS
slopos::mesh::loadChannels()               // Restore channels from NVS
slopos::mesh::saveContacts()               // Save contacts to NVS (Phase 2.6+)
slopos::mesh::loadContacts()               // Restore contacts from NVS
slopos::mesh::isContactFavourite(name)     // Check if contact is favourited
slopos::mesh::setContactFavourite(n, fav)  // Set favourite flag
slopos::mesh::shutdown()                   // Graceful radio + power off
slopos::mesh::injectMessage(sender, ch, text)  // Simulate incoming (test only)
slopos::mesh::getCurrentTime()             // RTC epoch seconds
slopos::mesh::setSystemTime(epoch)         // Set RTC from UI
slopos::mesh::getCurrentLocalDateTime()    // Breakdown: y/m/d/h/m
slopos::mesh::makeEpoch(y, m, d, h, mn)    // Create epoch from components
slopos::mesh::getPacketLogCount()          // Entries in packet log
slopos::mesh::getPacketLogEntry(i, out)    // Read one packet log entry
slopos::mesh::pushPacketLog(src, rssi, snr, type) // Add packet log entry
slopos::mesh::sendTrace(idx, &tag)         // Send trace route request
slopos::mesh::findContactIndex(name)       // Find contact index by exact name
slopos::mesh::hasTraceResult()             // Trace reply received?
slopos::mesh::getTracePathLen()            // SNR/hop count in trace
slopos::mesh::getTracePath(snrs, hashes)   // Get trace path data
slopos::mesh::clearTraceResult()           // Reset trace state
slopos::mesh::contactHasPath(idx)          // Does contact have a known path?
slopos::mesh::sendPingNearby()              // Zero-hop ping for node discovery
slopos::mesh::pingIsActive()                // Ping in progress?
slopos::mesh::pingOnCooldown()              // Ping on cooldown?
slopos::mesh::pingCooldownRemaining()       // Milliseconds until next ping allowed
slopos::mesh::activePingRemaining()         // Milliseconds until current ping times out
slopos::mesh::getPingResultCount()          // Number of ping responses received
slopos::mesh::getPingResult(i)             // Read one ping response (name, RSSI)
slopos::mesh::applyRadioParams(freq, bw, sf, cr, pwr, rx_gain) // Live radio param update (no NVS)
slopos::mesh::revertRadioParams()          // Revert radio params to stored prefs
slopos::mesh::getRemainingTxBudget()       // Duty cycle: remaining TX airtime in ms
slopos::mesh::setDutyCycle(percent)        // Set duty cycle limit (0 = disabled)
slopos::mesh::removeChannel(idx)           // Remove a joined channel by index
slopos::mesh::getSignalHistoryCount()      // Number of RSSI/SNR history samples for sparkline
slopos::mesh::getSignalHistoryRSSI(idx)    // RSSI by logical index (0 = oldest) for sparkline
slopos::mesh::getSignalHistorySNR(idx)     // SNR by logical index (0 = oldest) for sparkline
slopos::mesh::registerAckedMessage(name, ts) // Register DM for ACK delivery tracking
slopos::mesh::isMessageAcked(name, ts)     // Check if a DM was acknowledged
slopos::mesh::removeContact(name)           // Remove a contact by name
slopos::mesh::resetPathTo(name)             // Reset route path to a contact
slopos::mesh::getAckCounter()               // Incremented each registerAckedMessage call
// ── Generic request/response (Phase 4.1) ──
slopos::mesh::sendRequest(dest, type)       // Send typed request to a node
slopos::mesh::sendRequestWithData(d, dat, len) // Request with payload
slopos::mesh::getResponseCount()            // Responses waiting
slopos::mesh::getResponse(idx, &tag, &data, &len, &name) // Read response
slopos::mesh::clearResponses()              // Clear response buffer
// ── Status request (Phase 4.2) ──
slopos::mesh::requestStatus(name)           // Request remote node status (battery, uptime, etc.)
slopos::mesh::hasStatusResponse()           // Status reply received?
slopos::mesh::getStatusResult(&st)          // Read NodeStatus struct
// ── Telemetry (Phase 4.3) ──
slopos::mesh::requestTelemetry(name)        // Request CayenneLPP sensor data
slopos::mesh::hasTelemetryResponse()        // Telemetry reply received?
slopos::mesh::getTelemetryResult(&tr)       // Read TelemetryResult struct
// ── Path discovery (Phase 4.4) ──
slopos::mesh::discoverPath(name)            // Flood-force route discovery, returns tag
slopos::mesh::hasPathTo(name)               // Path known for this contact?
slopos::mesh::getContactPathLen(name)       // Hop count to contact
// ── Repeater/room login (Phase 4.5) ──
slopos::mesh::sendLogin(name, password)     // Login to repeater/room
slopos::mesh::sendLogout(name)              // Logout
slopos::mesh::sendCommand(name, text)       // Admin command to repeater
slopos::mesh::isLoggedIn(name)              // Currently logged in?
slopos::mesh::getLoginStatus(name)          // LOGIN_STATUS_NONE/PENDING/OK/FAILED
slopos::mesh::getLoginPermission(name)      // Permission level
slopos::mesh::forceLoginState(n, s, p)      // Override login state (test)
// ── Room message fetch (Phase 4.6) ──
slopos::mesh::sendRoomMsgFetchRequest(contact, channel) // Fetch room history
slopos::mesh::getRoomMsgFetchCount()         // Entries in fetch result
slopos::mesh::getRoomMsgFetchEntry(idx, &s, &t, &c, &ts) // Read fetch entry
slopos::mesh::clearRoomMsgFetch()            // Clear fetch buffer
// ── Anonymous messages (Phase 4.7) ──
slopos::mesh::sendAnonMessage(pubkey_hex, text) // DM by public key (no contact needed)
// ── Group data datagrams (Phase 4.8) ──
slopos::mesh::sendGroupDataToChannel(ch_idx, type, data, len) // Send typed data to group
slopos::mesh::getGroupDataRecvCount()         // Received group datagrams
slopos::mesh::getGroupDataRecvEntry(idx, &type, &data, &len, &ch, &ts) // Read datagram
slopos::mesh::clearGroupDataRecv()            // Clear datagram buffer
// ── Test helpers (SLOPOS_REMOTE_TEST only) ──
slopos::mesh::addTestRepeater(name)          // Inject fake repeater contact
slopos::mesh::addTestRoomServer(name)        // Inject fake room server contact
```

**Messages arrive** via `chat_screen_add_msg(channel, sender, text, is_self)`. The chat screen maintains per-channel message caches (8 messages each, 16 channels max).

**Channel protocol:**
- **PSK channels:** "Public" uses PSK `izOH6cXN6mrJ5e26oRXNcg==`. Any node with the matching PSK derives the same channel hash.
- **Hashtag channels:** No PSK needed. Channel hash = SHA256(SHA256(name)). Any node using the same hashtag name creates the same hash.
- Group messages embed `"<sender_name>: <text>"` for interop with MeshCore companion radio firmware.

---

## Testing

**Current test count: 319** (318 passed, 1 skipped for native_test).

```bash
pio test -e native_test -v       # All tests (no hardware)
pio test -e native_test -f test_touch -v     # One module
```

Test modules:
| Module | What it tests |
|--------|--------------|
| `test_battery` | Battery ADC → percentage conversion |
| `test_gps` | GPS NMEA sentence parsing |
| `test_touch` | GT911 touch coordinate transforms |
| `test_keyboard` | Keyboard I2C protocol, key mode |
| `test_mesh_wrapper` | Mesh wrapper API stubs |
| `test_trackball` | Trackball debounce, event queue |
| `test_theme` | Theme color constants and helpers |
| `test_mesh_messaging` | Message send/receive through mesh |
| `test_pins` | Pin definitions and aliases |
| `test_sdcard` | SD card VFS path helpers |
| `test_build` | Compilation checks for all modules |
| `test_navigation` | Screen navigation and history |
| `test_map` | Map renderer PSRAM allocators |
| `test_emoji` | Emoji font data, glyph coverage, fallback registration, Discord-style autocomplete |
| `test_chat_truncation` | UTF-8 safe message truncation (emoji-aware byte cutting) |
| `test_home_screen` | Home screen 4x3 grid layout, icon grid, top/bottom bar rendering |
| `test_terminal` | Terminal screen capped line output, command dispatch |

**Critical rules:**
- Tests use `test/test_<name>/` dirs with `main.cpp` entry points. Wrong naming = not discovered.
- Hardware is mocked in `test/mocks/` (Arduino.h, lvgl.h, RadioLib.h, LovyanGFX.hpp, Wire.h, etc.)
- `mock_prefs.cpp` provides byte-level NVS prefs stubs when HAL modules depend on prefs.
- Always run tests before pushing. A PR with failing tests is rejected.

**New code = new tests.** Minimum:
- HAL changes → add mock + test (e.g., `test_trackball` for trackball HAL)
- New screen → navigation test + message display test
- API changes → update existing mock stubs

---

## Build & Tools

```bash
# Build firmware (release — no serial debug output)
pio run -e SlopOS_TDeck

# Debug build (full boot sequence + periodic diagnostics over serial)
pio run -e SlopOS_TDeck_debug

# Trackball debug build (raw GPIO state visible)
pio run -e SlopOS_TDeck_trackball_debug

# Per-feature debug builds — enable only one subsystem's debug output
pio run -e SlopOS_TDeck_debug_display  # Display flush/invalidate/auto-off
pio run -e SlopOS_TDeck_debug_mesh     # Mesh message rx/tx, radio init
pio run -e SlopOS_TDeck_debug_ui       # UI boot steps, screen transitions
pio run -e SlopOS_TDeck_debug_map      # Map tile loading, rendering
pio run -e SlopOS_TDeck_debug_diag     # Periodic stats & system dumps

# Debug build at prescribed frequency (e.g. 869.525 MHz / SF10 / 250 kHz)
pio run -e SlopOS_TDeck_debug_869

# Run native tests (no hardware)
pio test -e native_test -v

# Remote test — test controller + SIMULATED mesh (no LoRa radio)
pio run -e SlopOS_TDeck_remote_test

# Remote test WITH RADIO — test controller + full LoRa mesh
pio run -e SlopOS_TDeck_remote_test_radio

# MeshV2 build (BaseChatMesh subclass — Phase 0 migration, compile-time `-D SLOPOS_MESH_V2=1`)
pio run -e SlopOS_TDeck_meshv2

# Remote test with MeshV2
pio run -e SlopOS_TDeck_remote_test_radio_meshv2
```

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

The debug build (`SlopOS_TDeck_debug`) enables:
- Step-by-step boot logging (`[boot] step N: ...`)
- Periodic heap/PSRAM/battery stats (`[stat]`)
- Display flush tracking (`[flush] #N`)
- Trackball pin states (`[pins]`)
- On-demand debug dump via `slopos_debug_dump()`
- Map tile discovery logging

**Per-feature debug environments** (`SlopOS_TDeck_debug_display`, `_mesh`, `_ui`, `_map`, `_diag`) enable only one subsystem's debug output at a time. They share `debug_cfg.h` infrastructure which supports runtime feature toggle (`debug feat 0/1` in remote test mode) and a `feat_set_all_mask()` aggregator.

The master `SLOPOS_DEBUG=1` flag enables all features simultaneously (backward compatible). Individual `-DSLOPOS_DEBUG_DISPLAY=1` etc. flags enable just that one feature, controlled at compile time by `#if SLOPOS_DEBUG_DISPLAY` guards throughout the codebase.

The release build (`SlopOS_TDeck`) suppresses all of these via `NDEBUG` and the absence of `SLOPOS_DEBUG=1`. Only critical errors and warnings print in release mode.

### Remote Test Controller

**⚠️ CRITICAL: Do NOT use this mode without explicit user consent.** This disables the LoRa radio and makes the device a serial-controlled input simulator. Only the user decides when to use this mode. Never switch to `SlopOS_TDeck_remote_test` build env unless the user asks you to or explicitly approves it.

Enables automated and manual testing over serial (USB CDC). No LoRa radio is initialised — all mesh messages are simulated via injection. The radio hardware is never touched.

Build with:
```bash
pio run -e SlopOS_TDeck_remote_test
```

Flash and connect (local):
```bash
pio run -e SlopOS_TDeck_remote_test -t upload
pio device monitor -b 115200
```

Flash and connect (remote via hardware gateway):
```bash
scp .pio/build/SlopOS_TDeck_remote_test/firmware-merged.bin <gateway>:/tmp/
ssh <gateway> "esptool --chip esp32s3 --port <serial-port> --baud 921600 write-flash 0x0 /tmp/firmware-merged.bin && rm /tmp/firmware-merged.bin"
ssh <gateway> "stty -F <serial-port> 115200 raw -echo && cat <serial-port>"
```

Once connected, the T-Deck shows a test controller banner. Type commands directly in the serial terminal:

| Command | Example | Description |
|---------|---------|-------------|
| `help` | `help` | Show command list |
| `nav chat` | `nav chat` | Navigate to screen (home/chat/contacts/channels/network/heard/map/settings/terminal/radio/trace/signal/advertise/onboarding/contactdetail/telemetry/nodestatus/s-radio/s-gps/s-display/s-system) |
| `back` | `back` | Go back in nav stack |
| `tb up` | `tb click` | Simulate trackball (up/down/left/right/click, or u/d/l/r/c) |
| `type hello` | `type Hello World` | Type text — queued and injected one char per loop cycle |
| `press enter` | `press backspace` | Press special key (enter/backspace/esc/tab) |
| `inject Alice Hello!` | `inject Bob channel=general hi` | Simulate incoming mesh message (no radio!) |
| `screen` | `screen` | Show current screen name |
| `status` | `status` | Show heap and PSRAM |
| `setrf <freq> <sf> <bw> <cr> <pwr>` | `setrf 869.525 10 250 5 22` | Set radio params in NVS |
| `reboot` | `reboot` | Reboot the device |
| `advert` | `advert` | Send self advert |
| `sendmessage <name> <text>` | `sendmessage Bob Hello` | Send a direct message via mesh |

Safety guarantees for `SlopOS_TDeck_remote_test`:
- No LoRa radio initialised — `slopos::mesh::init()` is never called
- All `sendMessage`, `sendChannelMessage`, `sendAdvert` return false (g_mesh is null)
- Radio accessors (`getLastRSSI`, `getLastSNR`, `getNoiseFloor`) return dummy values
- No SPI transactions ever reach the SX1262 hardware

**⚠️ The `SlopOS_TDeck_remote_test_radio` env has NO such guarantees** — it initialises the full LoRa mesh (`SLOPOS_REMOTE_TEST_RADIO=1`) and actually transmits. Use only for bidirectional radio verification (T-Deck ↔ Heltec V3).

**⚠️ LIMITATION: Cannot test physical input hardware.** Remote test mode simulates trackball, keyboard, and touch programmatically — events are injected directly into the input queues. This means it **cannot** validate:
- GPIO debounce timing, edge detection, or signal quality
- Physical switch feel or actuation
- I2C bus timing or peripheral detection
- Trackball direction sensitivity or deadtime behavior
- Any issue where the root cause is in the physical layer (pin states, interrupts, pull resistors)

If the issue involves physical input hardware (trackball, keyboard, touch, buttons), remote test mode is not appropriate — it needs real hardware testing on the device.

---

## Versioning & Release

Main + dev branch model:
- `dev` — integration branch. All PRs merge here.
- `main` — stable releases only.
- Tags: `beta-0.1.XX` (zero-padded for correct sort: `beta-0.1.09` not `beta-0.1.9`). Current: `beta-0.1.37`

**Release flow (maintainer only):**
1. Update `SLOPOS_VERSION` in `tdeck_pins.h`
2. `pio run -e SlopOS_TDeck`
3. `cp .pio/build/SlopOS_TDeck/firmware-merged.bin firmware/firmware-merged.bin`
4. Commit, tag, push, `gh release create`

---

## Radio Setup

The Radio Setup screen (`screens.cpp:4935`) provides a two-column layout:
- **Left column:** Frequency presets (868.000 EU, 869.525 UK, 869.618 UK, 915.000 US, 433.500 EU)
- **Right column:** "Custom RF..." button → opens Custom RF sub-screen, SF −/+ controls (7-12), BW −/+ controls (steps through 500/250/125/62.5/41.7/31.25/20.8/15.6/10.4/7.8 kHz), TX power −/+ controls (2-22 dBm), **RX Gain toggle** (BOOST/NORMAL), **Duty cycle** setting (percentage limit)
- **Bottom:** Save & Reboot button (writes prefs + rx_boosted_gain + duty_cycle, saves channels and messages, then `ESP.restart()`)

Radio params are stored in module-level `static` vars (`s_rf_freq`, `s_rf_sf`, `s_rf_bw`, `s_rf_cr`, `s_rf_pwr`, `s_rx_gain`, `s_duty_cycle`) in `screens.cpp:4782-4788`, shared between `radio_setup_screen_show` and `custom_rf_screen_show`. These defaults to 869.618 MHz / SF8 / 62.5 kHz / CR 4/5 / 22 dBm / RX gain normal / duty cycle disabled.

### Custom RF Sub-Screen

Accessed via the "Custom RF..." button on the Radio Setup screen. Not a top-level navigable screen — uses `show_screen()` directly, not `navigate_to()`. Provides five text input fields for Freq, SF, BW, CR, Pwr with validation rules:
- Freq: 400.0 – 930.0 MHz
- SF: 7 – 12
- BW: 7.8 – 500.0 kHz
- CR: 4/5 – 4/8 (5-8)
- TX Pwr: 2 – 22 dBm

On "Apply", validated values are written to the shared state and `go_back()` is called. Unsaved changes are lost if the user navigates away — only "Save & Reboot" persists to NVS.

---

## New Gotchas & Pitfalls (Discovered During Codebase Audit)

| Gotcha | Details |
|--------|---------|
| Custom RF fragile textarea finding | The Apply button callback walks `lv_obj_get_child(scr, i)` looking for textarea types to extract data — this depends on widget creation order and breaks if any non-textarea child is added between them |
| Custom RF error label lookup | Uses `lv_obj_get_child(scr, lv_obj_get_child_cnt(scr) - 2)` to find the error label — fragile, assumes exact child ordering relative to the Apply button |
| Radio Setup static state | Shared `s_rf_*` statics persist across screen navigate/show cycles — if the screen is shown twice, the second view preserves the first session's unsaved edits |
| Unsaved changes on go_back() | Navigating away from Radio Setup or Custom RF without pressing "Save & Reboot" discards all edits — state resets to defaults on next `radio_setup_screen_show()` |
| SF label via `user_data` | The SF −/+ buttons use `(void*)sf_lbl` as event user_data, but `sf_lbl` is a stack-local variable in `radio_setup_screen_show`. While `sf_lbl` is alive for the screen's lifetime, the pointer is only valid as long as the widget exists — fragile if screen is recreated |
| BW discrete stepping | BW uses a hardcoded float array and cycles through values via a loop with `x + 0.01f` tolerance — values near boundaries may not snap correctly due to float comparison |
| `src/utils/` no longer empty | Now contains `utf8_util.h` — the empty placeholder was replaced by a real utility module for UTF-8 safe truncation |
| `SLOPOS_RUNTIME_FEAT()` macro scope | The `debug_cfg.h` `SLOPOS_RUNTIME_FEAT()` macro only works under full `SLOPOS_DEBUG=1` build. In per-feature builds (e.g. `SLOPOS_DEBUG_MESH=1` alone), the macro expands to nothing — runtime `debug feat 0/1` toggle has no effect. Only compile-time `#if SLOPOS_DEBUG_MESH` guards control output. |
| Emoji font pointer init ordering | `emoji_font.h` declares mutable globals `emoji_wrapped_montserrat_*` that are initialized to raw Montserrat before `emoji_font_setup()` runs. Any code accessing these font pointers during boot (before the setup function runs) gets un-wrapped fonts without emoji fallback glyphs. |
|| Navigation back-swipe state reset | `back_swipe_commit` is reset to 0 at the start of both `navigate_to()` and `go_back()`. If a screen transition is triggered mid-back-swipe (e.g. by a screen's constructor calling navigate internally), the two-swipe sequence is broken — the user must start over from zero Left events. |
||| Contacts screen strdup user_data | Contacts and Repeaters screens store `strdup(c.name)` as `lv_obj_set_user_data()` for click handlers. The heap copy is freed on `LV_EVENT_DELETE`. If a row is added or manually deleted without firing the delete event, the pointer leaks. |
||| Contact Detail screen strdup user_data | Contact Detail screen uses `strdup(contact_name)` for DM button user_data, plus two more `strdup(c.name)` in the Contacts screen for the detail icon and row data — all freed on LV_EVENT_DELETE. Three concurrent strdups per contact row in the Contacts screen now. |
||| Channel delete button hidden at n=1 | The Channels screen delete button is hidden when `n <= 1` — prevents deleting the last remaining channel. But `removeChannel()` is always callable from the API, so a test or code path that calls it directly could leave zero channels. |
||| Custom RF child-scan walk order | Apply button walks `lv_obj_get_child_cnt(scr)` looking for the first 5 textareas. The sort order depends on widget creation sequence — if any non-textarea child is inserted before the textareas, the count resets and the wrong textarea is picked up. Now guarded by a `found != 5` check, but the error path still uses the fragile `lv_obj_get_child_cnt(scr) - 2` label lookup. |
||| New: RX Gain and Duty Cycle saved on reboot | `s_rx_gain` and `s_duty_cycle` are saved in NVS only on "Save & Reboot". Navigating away from Radio Setup without pressing save discards these settings silently, same as freq/SF/etc. |
|||| New: `SlopOS_TDeck_remote_test_radio` env | Full LoRa mesh + test controller. The `SLOPOS_REMOTE_TEST_RADIO=1` flag enables both the test controller serial commands AND the radio mesh. ⚠️ This env actually transmits — unlike `_remote_test` which never initializes the radio. |
|||| New: SlopMeshV2 compile-time selection | `-D SLOPOS_MESH_V2=1` selects `SlopMeshV2` (BaseChatMesh subclass) instead of the original `SlopMesh`. Both classes coexist but only one mesh instance runs per build. The `SlopOS_TDeck_meshv2` env sets this flag — use the correct env when testing meshv2 features. |
|||| New: ACK tracking state is in-memory only | `registerAckedMessage`/`isMessageAcked` delivery state resets on every boot. No NVS persistence — ACK data is lost after power cycle. |
||||| New: Custom vars SPIFFS no file locking | Custom variables at `/custom_vars.txt` (key=value lines) are read/written from terminal commands with no file locking. Concurrent `getvar`/`setvar`/`delvar` can intermix reads and writes, corrupting the file. Single-user terminal usage is safe but avoid concurrent access. |
|||| New: Settings sub-screens are full navigable screens | SettingsRadio, SettingsGPS, SettingsDisplay, SettingsSystem are all in the `Screen` enum and use `navigate_to()` with slide animation — they have nav stack entries and can be gone back from. Each calls `prefs_get()` at entry to get fresh state. State changes in one sub-screen are not visible in another until the user exits and re-enters. |
|||| New: Runtime theme system (6 presets, NVS persisted) | `theme.h` now has `slopos::theme::THEMES[6]` with presets (Default Cyan, Midnight Blue, Forest Green, Sunset Orange, Royal Purple, Amber Glow). `theme_apply(id)` writes to mutable inline vars (`BG_PRIMARY`, `ACCENT`, etc.). Theme ID stored in NVS via `p.theme_id`. Not all screens re-read theme vars on `theme_apply()` — some may need to be re-entered to pick up new colors. |
|||| New: Repeater login state is in-memory | `sendLogin()`/`sendLogout()` login state resets on every boot. Password can be saved to NVS via `NodePrefs.repeater_password` for auto-login on restart. |
|||| New: Room message fetch is polling-based | `sendRoomMsgFetchRequest()` triggers a fetch; results are read via `getRoomMsgFetchCount()`/`getRoomMsgFetchEntry()`. Only one fetch result can be stored at a time — calling fetch again overwrites previous results. |
|||| New: NodeStatus/Telemetry screens need mesh response | Both screens use `show_screen()` (not `navigate_to()`) and rely on `hasStatusResponse()`/`hasTelemetryResponse()` being set before entry. If no response arrives, they show a "waiting" message. `clearResponses()` is called after reading, so re-entering without a new request shows the waiting state. |
|||| New: Group data datagrams are one-shot poll | `getGroupDataRecvCount()` / `getGroupDataRecvEntry()` / `clearGroupDataRecv()` follow the same polling pattern as room message fetch. No persistent storage — data is lost on each `clearGroupDataRecv()`. |

---

## Known Issues Reference

All known issues are documented in `docs/KNOWN_ISSUES.md`. Most previously tracked issues have been **FIXED** (see git log for PR references). The remaining actionable item:

- **Launcher Compatibility:** SlopOS doesn't work under bmorcelli/Launcher — would need a compatibility layer for peripheral re-init after handoff from the launcher

**Recently fixed (see `docs/KNOWN_ISSUES.md` for PR details):**
- GPS NMEA checksum validation, Navigation history stack, Channel hash full compare, Contact expiry/eviction with LRU, Advert rate limiting at mesh layer, Null-termination on short payloads, LVGL tick starvation during TX, `lv_obj_del` in event handlers, Map screen static persistence, I2C bus speed race (400kHz touch), Trackball LEFT double-fire, `keyboard_consume_event` side effects, GT911 INT-pin-HIGH buffered event drop, TDeckBoard duplicate instances, Module static-init allocation ordering, Terminal unbounded labels, REPEATERS/PACKETS screen separation, GPS NMEA checksum, makeEpoch thread-safety, debug.h non-debug stubs, onboard restart flash write delay, screen dispatch code deduplication, SPI host pin contention, sendTrace indentation
- **Previously synced:** Radio reception fix (SPI host moved to SPI2_HOST, channel hash full compare, auto-join Public), graceful shutdown from Settings, unread message badges on home screen, display brightness control, auto-backlight timeout, flood max hops setting, contact SNR display, TX/RX delay tuning in Settings, TX/RX airtime and packet statistics on Signal screen, GPS clock sync on first valid fix, Contact Detail screen (#180), Signal screen two-column layout (#183), iOS-style signal dots (#187), `setrf`/`reboot`/`advert` serial test commands (#189), channel deletion (#168/#169), NAV serial command (#35a7638), map canvas PSRAM allocation fix (#4a464f6), `SlopOS_TDeck_remote_test_radio` env (#195), ROADMAP.md (#197), beta-0.1.36 release, SlopMeshV2 migration (Phase 0, #223/#224), per-contact RSSI/SNR history with sparkline chart (#236), message search in chat (#234), message delivery status (ACK ticks) in chat bubbles (#232), custom variables key-value store via terminal commands (Phase 2.6), auto-add contact type config (Phase 2.3), GPS enable/read-interval controls (Phase 2.5), `sendmessage` test controller command, `SlopOS_TDeck_meshv2` and `SlopOS_TDeck_remote_test_radio_meshv2` build envs
- **Since last sync:** Settings organized into category sub-menus (Phase 2.7, #246), runtime theme system with 6 presets + NVS persistence (#244), PendingAck bugfix + 4 unpersisted NodePrefs fields (#249), beta-0.1.37 release, generic binary-request framework (Phase 4.1, #251), status request with UI — battery/uptime/airtime from remote node (Phase 4.2, #253), telemetry request with CayenneLPP parsing (Phase 4.3), path discovery with flood-force routing (Phase 4.4), repeater/room login with admin commands (Phase 4.5, #259), room server message fetch (Phase 4.6, #263), anonymous message send/receive (Phase 4.7, #260), group data datagrams (Phase 4.8, #265), dedicated repeater detail screen with login flow (#257/#258), repeater login with password save and compact UI, NAV serial entries for settings submenus and new screens

---

## Code Audit Checklist

### Buffer Safety
- [ ] `strncpy` — does every call manually null-terminate? (`dest[n-1] = '\0'`)
- [ ] `snprintf` — is the buffer size correct? (including null terminator)
- [ ] `lv_textarea_set_max_length` — is the limit ≤ buffer size - 1?
- [ ] UTF-8 truncation — does any byte-level truncation risk splitting multi-byte characters mid-codepoint?
- [ ] Message payloads — is null-termination unconditional (not `if (len > 1) ...`)?
- [ ] Stack buffers — any large local arrays on stack that should be `static` or heap?

### Logic & Edge Cases
- [ ] Hash/crypto comparisons — full `memcmp`, not single-byte prefix match
- [ ] Rate limiting — enforced at the API layer, not just the UI
- [ ] Overflow guards — `uint8_t` counters that wrap without resetting (e.g. `save_counter`)
- [ ] Contact/array eviction — does a full list drop new entries silently? (LRU or TTL needed)
- [ ] Navigation history — does a circular buffer overwrite without wrapping `pop`?
- [ ] GPIO edge detection — falling-edge only for all directions (not LEFT on both edges)
- [ ] Hardware init ordering — dependencies initialised before consumers (backlight before panel, LVGL tick before timer handler)

### UI / Theme Compliance
- [ ] `apply_dark_bg()` called on every screen background
- [ ] Colors from `theme.h` constants, not hardcoded
- [ ] Zero radius throughout (no `border-radius`, no pill shapes)
- [ ] Bottom bar and top bar use `make_screen_full()` or equivalent
- [ ] No `\\n` literal where real newline was intended

### Concurrency / Timing
- [ ] `lv_obj_del` in event handler — should be `lv_obj_del_async()`
- [ ] Auto-delete screens — `lv_scr_load_anim(..., true)` deletes all children; `LV_EVENT_DELETE` needed to null globals
- [ ] LVGL tick starvation — any blocking operations that delay `lv_timer_handler()`?
- [ ] `ESP.restart()` without flash write delay — SPIFFS/NVS write may not have completed

### Custom RF Specific
- [ ] Textarea child-finding (by widget type scan) — will break if widget order changes
- [ ] Error label positioned by `lv_obj_get_child_cnt(scr) - 2` — fragile ordering dependency
- [ ] Shared `s_rf_*` static state persists across screens — check for stale values on re-entry
- [ ] Unsaved changes discarded on `go_back()` without confirmation
