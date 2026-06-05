# SigurdOS T-Deck — Agent Guide (Synced from Codebase)

**This file is auto-synced from the codebase.** It reflects the actual codebase state as of the last automated sync. Changes made by PRs will eventually be overwritten by the next sync — do not edit manually. The locked reference files are `AGENTS.md` and `CLAUDE.md`.

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
│   ├── buzzer.cpp/h       # Buzzer notifications (short/double beep, active-low GPIO 46)
│   ├── wifi_ota.cpp/h     # WiFi AP OTA firmware update + site survey (APInfo struct, scan)
│   ├── github_ota.cpp/h   # GitHub-release OTA download (WiFi STA, async, status/progress)
│   └── sdcard.cpp/h       # SD card init, status, path helpers
├── mesh/
│   ├── sigurd_mesh_v2.h         # MeshV2 (BaseChatMesh) — routing, channels, message handling, RSSI/SNR history, ACK tracking. V1 class removed (#290).
│   ├── mesh_wrapper.cpp/h       # Public API for the UI layer
│   ├── channel_validation.cpp/h # Channel name validation (charset, length, prefix rules)
│   ├── regions.cpp/h            # Region frequency preset management (SPIFFS persistence, key derivation)
│   ├── message_store.cpp/h      # Persistent message store (SPIFFS binary ring buffer, 32 conversations, 160-char text, timestamped)
│   └── companion_adapter.inc    # Companion BLE bridge adapter — included into mesh_wrapper.cpp, PSRAM-allocated CompanionBridge (~3 KB)
├── comms/
│   └── companion_bridge.cpp/h   # Companion BLE bridge protocol (SerialInterface transport, command enum, channel/contact/self-info types)
├── ui/
│   ├── theme.h            # Colors, pixel helpers (apply_pixel_*)
│   ├── responsive.h       # Display-size-agnostic layout helpers
│   ├── home_screen.cpp/h  # 4x3 icon grid, top/bottom bars, battery/signal/time
│   ├── chat_screen.cpp/h  # Channels, DM, message bubbles
│   ├── screens.cpp/h      # Heard, Contacts, Contact Detail, Map, Settings, Trace, Terminal, Signal, Channels, Finder, Advertise, Radio Setup, Custom RF, Telemetry, Node Status, Node Stats, Regions
│   ├── onboarding_screen.cpp/h  # First-boot setup wizard
│   ├── navigation.cpp/h   # Screen routing with slide transitions, universal back-swipe
│   └── ui.cpp/h           # Splash→Home transition, main loop updates
├── app/
│   ├── map_renderer.cpp/h # Offline map (PNG tiles via lodepng, PSRAM cache)
│   ├── tile_cache.cpp/h   # Tile cache — LRU eviction (4 entries, uint64_t monotonic clock)
│   ├── lodepng_alloc.cpp  # lodepng allocator → PSRAM with DRAM fallback
│   └── qr_show.cpp/h      # QR code display (ricmoo QRCode library, MIT)
├── diagnostics/
│   ├── debug_cfg.h            # Per-feature debug flag selection (runtime toggle)
│   ├── debug.cpp/h            # Debug dumps (SIGURDOS_DEBUG=1 build)
│   ├── telemetry.h            # Telemetry aggregation header
│   ├── telemetry.cpp          # Telemetry core implementation
│   ├── telemetry_collectors.cpp/h  # Telemetry data collectors (heap, PSRAM, RSSI)
│   ├── telemetry_crash.cpp/h       # Crash telemetry (reboot reason, stack watermark)
│   ├── telemetry_hb_ring.cpp/h     # Heartbeat ring buffer
│   ├── telemetry_input.cpp/h       # Input event telemetry (trackball, keyboard, touch)
│   └── telemetry_protocol.cpp/h    # Telemetry wire protocol (binary framing)
├── fonts/
│   ├── emoji_font_setup.cpp    # Emoji font fallback registration for LVGL
│   ├── emoji_font.h            # Emoji font wrapping declarations
│   ├── emoji_font.c            # Compiled emoji font bitmap data (16px, Noto Emoji derivative, 356KB)
│   ├── emoji_data.cpp/h        # Discord-style emoji short name ↔ UTF-8 lookup (343 entries)
│   └── emoji_images/           # Emoji picker image assets (generated)
│       ├── emoji_picker_images.h
│       └── emoji_picker_index.h
├── test/                  # Remote test controller (SIGURDOS_REMOTE_TEST=1)
│   └── test_controller.cpp/h  # Serial-driven test harness — inject events, nav, type, screen capture
├── utils/                 # Utility modules
│   └── utf8_util.h       # UTF-8 safe truncation (emoji-aware byte cutting)
```
`lib/` (at repo root, not under `src/`):
- `meshcore/` — Git submodule → MeshCore
- `lodepng/` — PNG decode library (zlib license, PSRAM allocators)
- `base64/` — Base64 encode/decode header

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

**Runtime theme system:** Theme colors are now runtime-variable `inline uint32_t` values in `theme.h` (not `constexpr`). Six presets are defined in `sigurdos::theme::THEMES[]`:
- 0: Default Cyan, 1: Midnight Blue, 2: Forest Green, 3: Sunset Orange, 4: Royal Purple, 5: Amber Glow

Call `sigurdos::theme::theme_apply(id)` to switch themes. Theme ID is persisted in NVS via `NodePrefs.theme_id`. Changing theme applies to new screen creations immediately; existing screens may need to be re-entered to pick up new colors.

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
| CHATS | `LV_SYMBOL_ENVELOPE` | Chat (channels, with unread badge) |
| DMs | `LV_SYMBOL_FILE` | Chat (DMs only filter) |
| ROOMS | `LV_SYMBOL_DIRECTORY` | Contacts (room servers only filter) |
| CONTACTS | `LV_SYMBOL_CALL` | Contacts |
| REPEATERS | `LV_SYMBOL_WIFI` | Repeaters (relay nodes) |
| ADVERTISE | `LV_SYMBOL_BELL` | Advertise |
| MAP | `LV_SYMBOL_GPS` | Map |
| TERMINAL | `LV_SYMBOL_KEYBOARD` | Terminal |
| PACKETS | `LV_SYMBOL_LIST` | Heard (packet log) |
| SETTINGS | `LV_SYMBOL_SETTINGS` | Settings |
| SETUP | `LV_SYMBOL_HOME` | Onboarding |
| SIGNAL | `LV_SYMBOL_BARS` | Signal |
| FINDER | `LV_SYMBOL_EYE_OPEN` | Network (not on home grid) |
| TRACE | `LV_SYMBOL_SHUFFLE` | Trace (not on home grid) |

### All Screens

| # | Screen | Source | Status |
|---|--------|--------|--------|
| 0 | Splash | `ui.cpp` | ✅ |
| 1 | Home (4x3 grid, 12 tiles: CHATS/DMs/ROOMS/CONTACTS/REPEATERS/ADVERTISE/MAP/TERMINAL/PACKETS/SETTINGS/SETUP/SIGNAL. Unread badge on CHATS. DMs filters chat→DM; ROOMS filters contacts→rooms.) | `home_screen.cpp` | ✅ |
| 2 | Chat (channels + DM, message search bar, ACK delivery ticks, filterable: all/channels/DMs, 149-byte input cap with counter) | `chat_screen.cpp` | ✅ |
| 3 | Contacts (alphabetical, tap→DM, filtered to CHAT/ROOM types, RSSI+SNR per contact) | `screens.cpp` | ✅ |
| 4 | Channels (list + create #hashtag/PSK, delete with confirmation dialog) | `screens.cpp` | ✅ |
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
| — | Repeater Detail (full relay info: type, RSSI, SNR, last seen, login/logout, admin command, favourite toggle) | `screens.cpp` | ✅ |
| 16 | Contact Detail (full contact info: type, RSSI, SNR, last seen, path, location, RSSI sparkline, DM + Trace buttons, Request Status + Telemetry buttons) | `screens.cpp` | ✅ |
| 17 | Telemetry (request & display CayenneLPP sensor data — voltage, temperature from remote nodes) | `screens.cpp` | ✅ |
| 18 | Node Status (request & display remote node stats — battery, uptime, airtime, packet counters) | `screens.cpp` | ✅ |
| 19 | WiFiNetworks (WiFi AP OTA firmware update + site survey, scan & display nearby access points, GitHub release OTA download) | `screens.cpp` | ✅ |
| 20 | Node Stats (local node packet counters: sent/received flood+direct, TX/RX airtime, duty cycle budget, ACK counter) | `screens.cpp` | ✅ |
| 21 | Regions (regional frequency preset management — active region selection, import/export, scope gating for flood scoping) | `screens.cpp` | ✅ |
| 22 | Bluetooth (companion BLE bridge status — enable/disable toggle, PIN display, app connection state, last sync time, compile-time gated by `SIGURDOS_COMPANION_BLE`) | `screens.cpp` | ✅ |
| — | Custom RF (sub-screen of Radio Setup — Freq, SF, BW, CR, Pwr text inputs with Apply) | `screens.cpp` | ✅ |
| — | SettingsRadio (Radio/Mesh sub-screen — radio params, bandwidth/SF/tuning, duty cycle, RX gain) | `screens.cpp` | ✅ |
| — | SettingsGPS (GPS/Location sub-screen — GPS enable toggle, read interval, location sharing) | `screens.cpp` | ✅ |
| — | SettingsDisplay (Display/UI sub-screen — keyboard BL, display brightness, auto-off timeout, theme selector) | `screens.cpp` | ✅ |
| — | SettingsSystem (System sub-screen — node name, SD card, date/time, chat history cap, auto-add contacts, shut down) | `screens.cpp` | ✅ |

---

## Mesh Integration

MeshCore is a submodule at `lib/meshcore/`. The UI never touches MeshCore directly — all calls go through `sigurdos::mesh::*` in `mesh_wrapper.h`.

**Key mesh wrapper API:**

```cpp
sigurdos::mesh::init(spiffs_ok)              // Boot — load identity, start radio
sigurdos::mesh::loop()                       // Call from main loop
sigurdos::mesh::sendMessage(name, text)      // Direct message — returns uint32_t epoch timestamp for ACK tracking (0 on failure)
sigurdos::mesh::sendChannelMessage(ch, text) // Group message
sigurdos::mesh::addChannel(name, psk_b64)    // PSK channel
sigurdos::mesh::addHashtagChannel(name)      // Hash-of-name channel
sigurdos::mesh::joinPublicChannel()          // Join "Public" with default PSK
sigurdos::mesh::exportContacts(out, max)     // Name list
sigurdos::mesh::exportContactsFull(out, max) // Name + RSSI + last_seen
sigurdos::mesh::exportChannels(out, max)     // Channel name list
sigurdos::mesh::pollMessages(out, max)       // Non-blocking message fetch
sigurdos::mesh::pendingMessageCount()        // Messages waiting in queue
sigurdos::mesh::getQueueDropCount()           // Queue overflow drops since boot
sigurdos::mesh::getUnreadMessageCount()       // Unread messages across all channels
sigurdos::mesh::resetUnreadMessageCount()     // Reset unread counter
sigurdos::mesh::setOwnName(name)             // Set this node's display name
sigurdos::mesh::getOwnName()                 // Get this node's display name
sigurdos::mesh::getNoiseFloor()              // Current noise floor dBm
sigurdos::mesh::getLastRSSI()                // Last received message RSSI
sigurdos::mesh::getLastSNR()                 // Last received message SNR
sigurdos::mesh::getTotalTxAirtimeMs()         // Cumulative TX airtime in ms
sigurdos::mesh::getTotalRxAirtimeMs()         // Cumulative RX airtime in ms
sigurdos::mesh::getNumSentFlood()             // Flood messages sent
sigurdos::mesh::getNumSentDirect()            // Direct messages sent
sigurdos::mesh::getNumRecvFlood()             // Flood messages received
sigurdos::mesh::getNumRecvDirect()            // Direct messages received
sigurdos::mesh::resetPacketStats()            // Reset all packet counters
sigurdos::mesh::getContactCount()            // Number of known contacts
sigurdos::mesh::findContactIndexByName(name) // Find contact index by exact name match
sigurdos::mesh::getChannelCount()            // Number of joined channels
sigurdos::mesh::sendAdvert()                 // Broadcast advert
sigurdos::mesh::getLastAdvertTime()          // Timestamp of last advert
sigurdos::mesh::getLastAdvertSuccess()       // Whether last advert succeeded
sigurdos::mesh::getLastAdvertUsedGps()       // Whether GPS data was included
sigurdos::mesh::saveState()                  // Save contacts to NVS
sigurdos::mesh::saveChannels()               // Save channels to NVS
sigurdos::mesh::loadChannels()               // Restore channels from NVS
sigurdos::mesh::saveContacts()               // Save contacts to NVS (Phase 2.6+)
sigurdos::mesh::loadContacts()               // Restore contacts from NVS
sigurdos::mesh::isContactFavourite(name)     // Check if contact is favourited
sigurdos::mesh::setContactFavourite(n, fav)  // Set favourite flag
sigurdos::mesh::shutdown()                   // Graceful radio + power off
sigurdos::mesh::factoryReset()               // Factory reset — wipe all NVS + SPIFFS
sigurdos::mesh::injectMessage(sender, ch, text)  // Simulate incoming (test only)
sigurdos::mesh::getCurrentTime()             // RTC epoch seconds
sigurdos::mesh::setSystemTime(epoch)         // Set RTC from UI
sigurdos::mesh::getCurrentLocalDateTime()    // Breakdown: y/m/d/h/m
sigurdos::mesh::makeEpoch(y, m, d, h, mn)    // Create epoch from components
sigurdos::mesh::getPacketLogCount()          // Entries in packet log
sigurdos::mesh::getPacketLogEntry(i, out)    // Read one packet log entry
sigurdos::mesh::pushPacketLog(src, rssi, snr, type) // Add packet log entry
sigurdos::mesh::sendTrace(idx, &tag)         // Send trace route request
sigurdos::mesh::findContactIndex(name)       // Find contact index by exact name
sigurdos::mesh::hasTraceResult()             // Trace reply received?
sigurdos::mesh::getTracePathLen()            // SNR/hop count in trace
sigurdos::mesh::getTracePath(snrs, hashes)   // Get trace path data
sigurdos::mesh::clearTraceResult()           // Reset trace state
sigurdos::mesh::contactHasPath(idx)          // Does contact have a known path?
sigurdos::mesh::sendPingNearby()              // Zero-hop ping for node discovery
sigurdos::mesh::pingIsActive()                // Ping in progress?
sigurdos::mesh::pingOnCooldown()              // Ping on cooldown?
sigurdos::mesh::pingCooldownRemaining()       // Milliseconds until next ping allowed
sigurdos::mesh::activePingRemaining()         // Milliseconds until current ping times out
sigurdos::mesh::getPingResultCount()          // Number of ping responses received
sigurdos::mesh::getPingResult(i)             // Read one ping response (name, RSSI)
sigurdos::mesh::applyRadioParams(freq, bw, sf, cr, pwr, rx_gain) // Live radio param update (no NVS)
sigurdos::mesh::revertRadioParams()          // Revert radio params to stored prefs
sigurdos::mesh::getRemainingTxBudget()       // Duty cycle: remaining TX airtime in ms
sigurdos::mesh::setDutyCycle(percent)        // Set duty cycle limit (0 = disabled)
sigurdos::mesh::removeChannel(idx)           // Remove a joined channel by index
sigurdos::mesh::getSignalHistoryCount()      // Number of RSSI/SNR history samples for sparkline
sigurdos::mesh::getSignalHistoryRSSI(idx)    // RSSI by logical index (0 = oldest) for sparkline
sigurdos::mesh::getSignalHistorySNR(idx)     // SNR by logical index (0 = oldest) for sparkline
sigurdos::mesh::registerAckedMessage(name, ts) // Register DM for ACK delivery tracking
sigurdos::mesh::isMessageAcked(name, ts)     // Check if a DM was acknowledged
sigurdos::mesh::removeContact(name)           // Remove a contact by name
sigurdos::mesh::resetPathTo(name)             // Reset route path to a contact
sigurdos::mesh::getAckCounter()               // Incremented each registerAckedMessage call
// ── Generic request/response (Phase 4.1) ──
sigurdos::mesh::sendRequest(dest, type)       // Send typed request to a node
sigurdos::mesh::sendRequestWithData(d, dat, len) // Request with payload
sigurdos::mesh::getResponseCount()            // Responses waiting
sigurdos::mesh::getResponse(idx, &tag, &data, &len, &name) // Read response
sigurdos::mesh::clearResponses()              // Clear response buffer
// ── Status request (Phase 4.2) ──
sigurdos::mesh::requestStatus(name)           // Request remote node status (battery, uptime, etc.)
sigurdos::mesh::hasStatusResponse()           // Status reply received?
sigurdos::mesh::getStatusResult(&st)          // Read NodeStatus struct
// ── Telemetry (Phase 4.3) ──
sigurdos::mesh::requestTelemetry(name)        // Request CayenneLPP sensor data
sigurdos::mesh::hasTelemetryResponse()        // Telemetry reply received?
sigurdos::mesh::getTelemetryResult(&tr)       // Read TelemetryResult struct
// ── Path discovery (Phase 4.4) ──
sigurdos::mesh::discoverPath(name)            // Flood-force route discovery, returns tag
sigurdos::mesh::hasPathTo(name)               // Path known for this contact?
sigurdos::mesh::getContactPathLen(name)       // Hop count to contact
// ── Repeater/room login (Phase 4.5) ──
sigurdos::mesh::sendLogin(name, password)     // Login to repeater/room
sigurdos::mesh::sendLogout(name)              // Logout
sigurdos::mesh::sendCommand(name, text)       // Admin command to repeater
sigurdos::mesh::isLoggedIn(name)              // Currently logged in?
sigurdos::mesh::getLoginStatus(name)          // LOGIN_STATUS_NONE/PENDING/OK/FAILED
sigurdos::mesh::getLoginPermission(name)      // Permission level
sigurdos::mesh::forceLoginState(n, s, p)      // Override login state (always available)
// ── Command response ring buffer (for admin terminal) ──
sigurdos::mesh::pushCmdResponse(n, t)         // Push response to ring buffer (MAX_CMD_RESPONSES=16)
sigurdos::mesh::pollCmdResponse(n_out, ns, t_out, ts) // Poll next response
sigurdos::mesh::clearCmdResponses()           // Clear all responses
// ── Room message fetch (Phase 4.6) ──
sigurdos::mesh::sendRoomMsgFetchRequest(contact, channel) // Fetch room history
sigurdos::mesh::getRoomMsgFetchCount()         // Entries in fetch result
sigurdos::mesh::getRoomMsgFetchEntry(idx, &s, &t, &c, &ts) // Read fetch entry
sigurdos::mesh::clearRoomMsgFetch()            // Clear fetch buffer
// ── Anonymous messages (Phase 4.7) ──
sigurdos::mesh::sendAnonMessage(pubkey_hex, text) // DM by public key (no contact needed)
// ── Group data datagrams (Phase 4.8) ──
sigurdos::mesh::sendGroupDataToChannel(ch_idx, type, data, len) // Send typed data to group
sigurdos::mesh::getGroupDataRecvCount()         // Received group datagrams
sigurdos::mesh::getGroupDataRecvEntry(idx, &type, &data, &len, &ch, &ts) // Read datagram
sigurdos::mesh::clearGroupDataRecv()            // Clear datagram buffer
// ── Test helpers (SIGURDOS_REMOTE_TEST only) ──
sigurdos::mesh::addTestRepeater(name)          // Inject fake repeater contact
sigurdos::mesh::addTestRoomServer(name)        // Inject fake room server contact
// ── Companion BLE bridge (Phase 5) ──
sigurdos::mesh::companionBleAvailable()        // BLE compiled in? (SIGURDOS_COMPANION_BLE)
sigurdos::mesh::companionBleEnabled()          // BLE advertising enabled?
sigurdos::mesh::companionBleSetEnabled(on)     // Enable/disable BLE advertising
sigurdos::mesh::companionBleConnected()        // App currently connected?
sigurdos::mesh::companionBlePin()              // BLE pairing PIN (6 digits)
sigurdos::mesh::companionBleLastSyncTime()     // Last sync epoch (0 = never)
sigurdos::mesh::companionBleLoop()             // Call from main loop
sigurdos::mesh::companionBleShutdown()         // Graceful BLE shutdown
sigurdos::mesh::messageStoreBegin()            // Init persistent message store (SPIFFS)
sigurdos::mesh::messageStoreAppend(msg)        // Write message to persistent store
sigurdos::mesh::messageStoreLoadRecent(conv, out, max) // Load recent messages for a conversation
```

**Messages arrive** via `chat_screen_add_msg(channel, sender, text, is_self)`. The chat screen maintains per-channel message caches (8 messages each, 16 channels max).

**Channel protocol:**
- **PSK channels:** "Public" uses PSK `izOH6cXN6mrJ5e26oRXNcg==`. Any node with the matching PSK derives the same channel hash.
- **Hashtag channels:** No PSK needed. Channel hash = SHA256(SHA256(name)). Any node using the same hashtag name creates the same hash.
- Group messages embed `"<sender_name>: <text>"` for interop with MeshCore companion radio firmware.

---

## Testing

**Current test count: 586** for native_test.

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
| `test_layout` | Layout regression tests for Contact Detail and Signal screens (no-overlap assertions for fixed 320×240 T-Deck) |
| `test_mesh_wrapper` | Mesh wrapper API stubs |
| `test_trackball` | Trackball debounce, event queue |
| `test_theme` | Theme color constants and helpers |
| `test_mesh_messaging` | Message send/receive through mesh |
| `test_prefs` | NVS preferences read/write, NodePrefs struct serialization |
| `test_pins` | Pin definitions and aliases |
| `test_sdcard` | SD card VFS path helpers |
| `test_build` | Compilation checks for all modules |
| `test_navigation` | Screen navigation and history |
| `test_map_renderer` | Map renderer PSRAM allocators |
| `test_emoji` | Emoji font data, glyph coverage, fallback registration, Discord-style autocomplete |
| `test_chat_truncation` | UTF-8 safe message truncation (emoji-aware byte cutting) |
| `test_home_screen` | Home screen 4x3 grid layout, icon grid, top/bottom bar rendering |
| `test_terminal` | Terminal screen capped line output, command dispatch |
| `test_channel_validation` | Channel name validation rules (character set, length limits) |
| `test_regions` | Region persistence (SPIFFS load/save), key derivation |
| `test_debug` | Debug infrastructure, feature toggle macros, runtime feat selection |
| `test_buzzer` | Buzzer GPIO control and beep patterns |
| `test_telemetry_hb_ring` | Heartbeat ring buffer operations |
| `test_telemetry_packet_log` | Packet log ring buffer |
| `test_telemetry_collectors` | Telemetry data collectors (heap, PSRAM, RSSI) |
| `test_telemetry_input` | Input event telemetry (trackball, keyboard, touch) |
| `test_telemetry_crash` | Crash telemetry (reboot reason, stack watermark) |
| `test_telemetry_protocol` | Telemetry wire protocol (binary framing) |
| `test_navigation_contract` | Navigation screen enum completeness and dispatch |
| `test_github_ota_contract` | GitHub-release OTA public API contract |
| `test_mesh_contract` | Mesh wrapper public API contract completeness |
| `test_ui_contract` | UI screen public API contract |
| `test_hal_contract` | HAL public API contract |
| `test_input_contract` | Input event API contract |
| `test_prefs_defaults` | NodePrefs default values |
| `test_emoji_integrity` | Emoji font data integrity and short name ↔ codepoint mapping |
| `test_emoji_fallback` | Emoji font fallback wiring |
| `test_chat_config` | Chat configuration validation |
| `test_companion_protocol` | Companion BLE wire protocol encoding/decoding |
| `test_message_store` | Persistent message store (SPIFFS, binary ring buffer) |
| `test_responsive` | Responsive layout helpers |
| `test_ui_timing` | LVGL timing contract |
| `test_controller` | Remote test controller command dispatch |
| `test_map` | Map bounding-box helpers |
| `test_lodepng_alloc` | lodepng PSRAM allocator fallback |
| `test_wifi_scan` | WiFi AP OTA scan and network discovery |
| `test_qr_show` | QR code display rendering |
| `test_onboarding` | Onboarding wizard flow |
| `test_tdeck_board` | T-Deck board power/sleep management |

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
pio run -e SigurdOS_TDeck

# Debug build (full boot sequence + periodic diagnostics over serial)
pio run -e SigurdOS_TDeck_debug

# Trackball debug build (raw GPIO state visible)
pio run -e SigurdOS_TDeck_trackball_debug

# Per-feature debug builds — enable only one subsystem's debug output
pio run -e SigurdOS_TDeck_debug_display  # Display flush/invalidate/auto-off
pio run -e SigurdOS_TDeck_debug_mesh     # Mesh message rx/tx, radio init
pio run -e SigurdOS_TDeck_debug_ui       # UI boot steps, screen transitions
pio run -e SigurdOS_TDeck_debug_map      # Map tile loading, rendering
pio run -e SigurdOS_TDeck_debug_diag     # Periodic stats & system dumps

# Debug build at prescribed frequency (e.g. 869.525 MHz / SF10 / 250 kHz)
pio run -e SigurdOS_TDeck_debug_869

# Run native tests (no hardware)
pio test -e native_test -v

# Remote test — test controller + SIMULATED mesh (no LoRa radio)
pio run -e SigurdOS_TDeck_remote_test

# Remote test WITH RADIO — test controller + full LoRa mesh
pio run -e SigurdOS_TDeck_remote_test_radio

# MeshV2 build (BaseChatMesh subclass — Phase 0 migration, compile-time `-D SIGURDOS_MESH_V2=1`)
pio run -e SigurdOS_TDeck_meshv2

# Remote test with MeshV2
pio run -e SigurdOS_TDeck_remote_test_radio_meshv2

# Remote test with radio + prescribed frequency (e.g. 869.525 MHz / SF10 / 250 kHz)
pio run -e SigurdOS_TDeck_remote_test_radio_testfreq

# BLE companion bridge build (companion BLE enabled, full LoRa mesh)
pio run -e SigurdOS_TDeck_ble
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

The debug build (`SigurdOS_TDeck_debug`) enables:
- Step-by-step boot logging (`[boot] step N: ...`)
- Periodic heap/PSRAM/battery stats (`[stat]`)
- Display flush tracking (`[flush] #N`)
- Trackball pin states (`[pins]`)
- On-demand debug dump via `sigurdos_debug_dump()`
- Map tile discovery logging

**Per-feature debug environments** (`SigurdOS_TDeck_debug_display`, `_mesh`, `_ui`, `_map`, `_diag`) enable only one subsystem's debug output at a time. They share `debug_cfg.h` infrastructure which supports runtime feature toggle (`debug feat 0/1` in remote test mode) and a `feat_set_all_mask()` aggregator.

The master `SIGURDOS_DEBUG=1` flag enables all features simultaneously (backward compatible). Individual `-DSIGURDOS_DEBUG_DISPLAY=1` etc. flags enable just that one feature, controlled at compile time by `#if SIGURDOS_DEBUG_DISPLAY` guards throughout the codebase.

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
| `nav chat` | `nav chat` | Navigate to screen (home/chat/contacts/channels/network/heard/map/settings/terminal/radio/trace/signal/advertise/onboarding/contactdetail/telemetry/nodestatus/nodestats/s-radio/s-gps/s-display/s-system) |
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
| `factoryreset` / `wipe` | `factoryreset` | Factory reset — clear all NVS + SPIFFS (requires confirmation) |
| `acmd <name> <text>` | `acmd Repeater1 status` | Send admin command to repeater |
| `login <name> [password]` | `login Repeater1 secret123` | Login to repeater/room server |
| `loginstat <name>` | `loginstat Repeater1` | Show login status for a repeater |

Safety guarantees for `SigurdOS_TDeck_remote_test`:
- No LoRa radio initialised — `sigurdos::mesh::init()` is never called
- All `sendMessage`, `sendChannelMessage`, `sendAdvert` return false (g_mesh is null)
- Radio accessors (`getLastRSSI`, `getLastSNR`, `getNoiseFloor`) return dummy values
- No SPI transactions ever reach the SX1262 hardware

**⚠️ The `SigurdOS_TDeck_remote_test_radio` env has NO such guarantees** — it initialises the full LoRa mesh (`SIGURDOS_REMOTE_TEST_RADIO=1`) and actually transmits. Use only for bidirectional radio verification (T-Deck ↔ Heltec V3).

**⚠️ LIMITATION: Cannot test physical input hardware.** Remote test mode simulates trackball, keyboard, and touch programmatically — events are injected directly into the input queues. This means it **cannot** validate:
- GPIO debounce timing, edge detection, or signal quality
- Physical switch feel or actuation
- I2C bus timing or peripheral detection
- Trackball direction sensitivity or deadtime behavior
- Any issue where the root cause is in the physical layer (pin states, interrupts, pull resistors)

If the issue involves physical input hardware (trackball, keyboard, touch, buttons), remote test mode is not appropriate — it needs real hardware testing on the device.

---

## Versioning & Release

Single `dev` branch model:
- `dev` — integration branch. All PRs merge here. There is no `main` branch; releases are tagged directly on `dev`.
- Tags: `beta-0.1.XX` (zero-padded for correct sort: `beta-0.1.09` not `beta-0.1.9`). Current: `beta-0.1.39`

**Release flow (maintainer only):**
1. Update `SIGURDOS_VERSION` in `tdeck_pins.h`
2. `pio run -e SigurdOS_TDeck`
3. `cp .pio/build/SigurdOS_TDeck/firmware-merged.bin firmware/firmware-merged.bin`
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
| `SIGURDOS_RUNTIME_FEAT()` macro scope | The `debug_cfg.h` `SIGURDOS_RUNTIME_FEAT()` macro only works under full `SIGURDOS_DEBUG=1` build. In per-feature builds (e.g. `SIGURDOS_DEBUG_MESH=1` alone), the macro expands to nothing — runtime `debug feat 0/1` toggle has no effect. Only compile-time `#if SIGURDOS_DEBUG_MESH` guards control output. |
| Emoji font pointer init ordering | `emoji_font.h` declares mutable globals `emoji_wrapped_montserrat_*` that are initialized to raw Montserrat before `emoji_font_setup()` runs. Any code accessing these font pointers during boot (before the setup function runs) gets un-wrapped fonts without emoji fallback glyphs. |
| Navigation back-swipe state reset | `back_swipe_commit` is reset to 0 at the start of both `navigate_to()` and `go_back()`. If a screen transition is triggered mid-back-swipe (e.g. by a screen's constructor calling navigate internally), the two-swipe sequence is broken — the user must start over from zero Left events. |
| Contacts screen strdup user_data | Contacts and Repeaters screens store `strdup(c.name)` as `lv_obj_set_user_data()` for click handlers. The heap copy is freed on `LV_EVENT_DELETE`. If a row is added or manually deleted without firing the delete event, the pointer leaks. |
| Contact Detail screen strdup user_data | Contact Detail screen uses `strdup(contact_name)` for DM button user_data, plus two more `strdup(c.name)` in the Contacts screen for the detail icon and row data — all freed on LV_EVENT_DELETE. Three concurrent strdups per contact row in the Contacts screen now. |
| Channel delete button hidden at n=1 | The Channels screen delete button is hidden when `n <= 1` — prevents deleting the last remaining channel. But `removeChannel()` is always callable from the API, so a test or code path that calls it directly could leave zero channels. |
| Custom RF child-scan walk order | Apply button walks `lv_obj_get_child_cnt(scr)` looking for the first 5 textareas. The sort order depends on widget creation sequence — if any non-textarea child is inserted before the textareas, the count resets and the wrong textarea is picked up. Now guarded by a `found != 5` check, but the error path still uses the fragile `lv_obj_get_child_cnt(scr) - 2` label lookup. |
| New: RX Gain and Duty Cycle saved on reboot | `s_rx_gain` and `s_duty_cycle` are saved in NVS only on "Save & Reboot". Navigating away from Radio Setup without pressing save discards these settings silently, same as freq/SF/etc. |
| New: `SigurdOS_TDeck_remote_test_radio` env | Full LoRa mesh + test controller. The `SIGURDOS_REMOTE_TEST_RADIO=1` flag enables both the test controller serial commands AND the radio mesh. ⚠️ This env actually transmits — unlike `_remote_test` which never initializes the radio. |
| New: SigurdMeshV2 compile-time selection | `-D SIGURDOS_MESH_V2=1` selects `SigurdMeshV2` (BaseChatMesh subclass). The V1 `SigurdMesh` class has been removed (#290) — only V2 remains. The `SigurdOS_TDeck_meshv2` env sets this flag — but since V1 is gone, `-D SIGURDOS_MESH_V2=1` is essentially always active and the env distinction may be removed. |
| New: ACK tracking state is in-memory only | `registerAckedMessage`/`isMessageAcked` delivery state resets on every boot. No NVS persistence — ACK data is lost after power cycle. |
| New: Custom vars SPIFFS no file locking | Custom variables at `/custom_vars.txt` (key=value lines) are read/written from terminal commands with no file locking. Concurrent `getvar`/`setvar`/`delvar` can intermix reads and writes, corrupting the file. Single-user terminal usage is safe but avoid concurrent access. |
| New: Settings sub-screens are full navigable screens | SettingsRadio, SettingsGPS, SettingsDisplay, SettingsSystem are all in the `Screen` enum and use `navigate_to()` with slide animation — they have nav stack entries and can be gone back from. Each calls `prefs_get()` at entry to get fresh state. State changes in one sub-screen are not visible in another until the user exits and re-enters. |
| New: Runtime theme system (6 presets, NVS persisted) | `theme.h` now has `sigurdos::theme::THEMES[6]` with presets (Default Cyan, Midnight Blue, Forest Green, Sunset Orange, Royal Purple, Amber Glow). `theme_apply(id)` writes to mutable inline vars (`BG_PRIMARY`, `ACCENT`, etc.). Theme ID stored in NVS via `p.theme_id`. Not all screens re-read theme vars on `theme_apply()` — some may need to be re-entered to pick up new colors. |
| New: Repeater login state is in-memory | `sendLogin()`/`sendLogout()` login state resets on every boot. Password can be saved to NVS via `NodePrefs.repeater_password` for auto-login on restart. |
| New: Room message fetch is polling-based | `sendRoomMsgFetchRequest()` triggers a fetch; results are read via `getRoomMsgFetchCount()`/`getRoomMsgFetchEntry()`. Only one fetch result can be stored at a time — calling fetch again overwrites previous results. |
| New: NodeStatus/Telemetry screens need mesh response | Both screens use `show_screen()` (not `navigate_to()`) and rely on `hasStatusResponse()`/`hasTelemetryResponse()` being set before entry. If no response arrives, they show a "waiting" message. `clearResponses()` is called after reading, so re-entering without a new request shows the waiting state. |
| New: Group data datagrams are one-shot poll | `getGroupDataRecvCount()` / `getGroupDataRecvEntry()` / `clearGroupDataRecv()` follow the same polling pattern as room message fetch. No persistent storage — data is lost on each `clearGroupDataRecv()`. |
| New: TODO in test_controller — chat_screen_add_msg_with_ts() | `test_controller.cpp` has a `// TODO: add chat_screen_add_msg_with_ts() to accept an explicit timestamp.` — the `inject` command cannot set custom timestamps on simulated incoming messages. All injected messages use the current system time. |
| New: sendMessage returns uint32_t timestamp | `sendMessage()` now returns the epoch-second timestamp used for ACK tracking (instead of `bool`). The UI must store this returned value and pass it to `isMessageAcked()`. Returns 0 on failure. |
| New: Home screen icon grid restructured | 12-tile grid now has: CHATS, DMs, ROOMS, CONTACTS, REPEATERS, ADVERTISE, MAP, TERMINAL, PACKETS, SETTINGS, SETUP, SIGNAL. FINDER and TRACE removed from home grid (still navigable via nav commands). DMs filters chat to DMs only; ROOMS filters contacts to ADV_TYPE_ROOM only. |
| New: factoryReset() wipes everything | `factoryReset()` clears all NVS keys and SPIFFS via `nvs_flash_erase()` + SPIFFS format — no confirmation prompt at the mesh layer. UI must gate behind user confirmation. Accessible from Settings System screen and `factoryreset`/`wipe` test controller commands. |
| New: Command response ring buffer | `pushCmdResponse(name, text)` stores responses from admin commands in a 16-entry circular buffer. No persistent storage — data is lost on `clearCmdResponses()` or buffer wrap. Single-consumer polling via `pollCmdResponse()`. |
| New: forceLoginState always available | Previously guarded behind `#if SIGURDOS_REMOTE_TEST`, `forceLoginState()` is now unconditionally available — can be called from any code path, not just tests. |
| New: Namespace rebrand slopos → sigurdos | All code now uses `namespace sigurdos` instead of `namespace slopos`. Legacy mesh class names were removed during the V2/BaseChatMesh migration; only `SigurdMeshV2` via `sigurd_mesh_v2.h` remains. Build env names unchanged (`SigurdOS_TDeck`). |
| New: Chat input capped at 149 bytes | Chat textarea is limited to 149 bytes with a live byte counter display (#288, #291). Messages over 149 bytes are rejected before send. |
| New: Channel deletion has confirmation dialog | The Channels screen now shows a confirmation dialog before deleting a channel (#287, #289). The `removeChannel()` API call is still immediate — the dialog only gates the UI path. |
| New: V1 mesh class removed | `sigurd_mesh.h` (the original `SigurdMesh` V1 class) was deleted (#286, #290). Only `SigurdMeshV2` via `sigurd_mesh_v2.h` remains. Any code path still referencing the V1 class will fail to compile. |
| New: Companion bridge PSRAM allocation | `companion_adapter.inc` allocates the `CompanionBridge` (~3 KB) via `heap_caps_malloc(SPIRAM)` with internal-DRAM fallback. The internal DRAM .bss is nearly full — manually allocating in PSRAM avoids linker placement issues. Must use placement new and explicit destructor call on shutdown. |
| New: message_store SPIFFS binary format | `message_store.cpp` uses a raw binary ring buffer in SPIFFS (`/msg_store.bin`) with magic/version header (`MESSAGE_STORE_VERSION=1`). No cross-version compatibility — changing the `StoredMessage` struct layout silently corrupts the store. Must call `messageStoreClear()` on version mismatch. |
| New: companion_adapter.inc inclusion model | `companion_adapter.inc` is `#include`-d from `mesh_wrapper.cpp` (not compiled standalone). It accesses `mesh_wrapper`'s private statics (`rtc_clock`, `msg_buf`, `msg_count`, etc.) without widening the public API. Cannot be tested in isolation — needs the full `mesh_wrapper.cpp` translation unit. |
| New: BLE compile-time guard | `SIGURDOS_COMPANION_BLE` controls the entire BLE subsystem at compile time. When not defined, `companionBleAvailable()` returns false, the Bluetooth screen shows "Not built", and all BLE API calls are no-ops. The `SigurdOS_TDeck_ble` build env sets this flag. |
| New: Bluetooth screen uses show_screen() | The Bluetooth screen uses `show_screen()` (like Custom RF), not `navigate_to()`. It does not enter the nav stack — `go_back()` returns to whatever screen was active before entry. No persistent state between visits. |

---

## Known Issues Reference

All known issues are documented in `docs/KNOWN_ISSUES.md`. Most previously tracked issues have been **FIXED** (see git log for PR references). The remaining actionable item:

- **Launcher Compatibility:** SigurdOS doesn't work under bmorcelli/Launcher — would need a compatibility layer for peripheral re-init after handoff from the launcher

**Recently fixed (see `docs/KNOWN_ISSUES.md` for PR details):**
- GPS NMEA checksum validation, Navigation history stack, Channel hash full compare, Contact expiry/eviction with LRU, Advert rate limiting at mesh layer, Null-termination on short payloads, LVGL tick starvation during TX, `lv_obj_del` in event handlers, Map screen static persistence, I2C bus speed race (400kHz touch), Trackball LEFT double-fire, `keyboard_consume_event` side effects, GT911 INT-pin-HIGH buffered event drop, TDeckBoard duplicate instances, Module static-init allocation ordering, Terminal unbounded labels, REPEATERS/PACKETS screen separation, GPS NMEA checksum, makeEpoch thread-safety, debug.h non-debug stubs, onboard restart flash write delay, screen dispatch code deduplication, SPI host pin contention, sendTrace indentation
- **Previously synced:** Radio reception fix (SPI host moved to SPI2_HOST, channel hash full compare, auto-join Public), graceful shutdown from Settings, unread message badges on home screen, display brightness control, auto-backlight timeout, flood max hops setting, contact SNR display, TX/RX delay tuning in Settings, TX/RX airtime and packet statistics on Signal screen, GPS clock sync on first valid fix, Contact Detail screen (#180), Signal screen two-column layout (#183), iOS-style signal dots (#187), `setrf`/`reboot`/`advert` serial test commands (#189), channel deletion (#168/#169), NAV serial command (#35a7638), map canvas PSRAM allocation fix (#4a464f6), `SigurdOS_TDeck_remote_test_radio` env (#195), ROADMAP.md (#197), beta-0.1.36 release, V2/BaseChatMesh migration (Phase 0, #223/#224), per-contact RSSI/SNR history with sparkline chart (#236), message search in chat (#234), message delivery status (ACK ticks) in chat bubbles (#232), custom variables key-value store via terminal commands (Phase 2.6), auto-add contact type config (Phase 2.3), GPS enable/read-interval controls (Phase 2.5), `sendmessage` test controller command, `SigurdOS_TDeck_meshv2` and `SigurdOS_TDeck_remote_test_radio_meshv2` build envs
- **Since last sync:** Settings organized into category sub-menus (Phase 2.7, #246), runtime theme system with 6 presets + NVS persistence (#244), PendingAck bugfix + 4 unpersisted NodePrefs fields (#249), beta-0.1.37 release, generic binary-request framework (Phase 4.1, #251), status request with UI — battery/uptime/airtime from remote node (Phase 4.2, #253), telemetry request with CayenneLPP parsing (Phase 4.3), path discovery with flood-force routing (Phase 4.4), repeater/room login with admin commands (Phase 4.5, #259), room server message fetch (Phase 4.6, #263), anonymous message send/receive (Phase 4.7, #260), group data datagrams (Phase 4.8, #265), dedicated repeater detail screen with login flow (#257/#258), repeater login with password save and compact UI, NAV serial entries for settings submenus and new screens, `SigurdOS_TDeck_remote_test_radio_testfreq` build env, `test_prefs` test module, regions screen with flood-scope management (#330), channel name validation module, QR code display screen, telemetry diagnostics subsystem, `test_channel_validation` and `test_regions` test modules
- **Currently synced:** Home screen DMs/ROOMS filter icons (#269), admin command terminal with live response polling, fav-toggle fix for login view, repeater login UI improvements (#266), RX boost preference test (#267), Phase 4 complete (4.1-4.8), Claude Code review fixes (#273), factory reset from Settings System + `factoryreset`/`wipe` test commands (#274), beta-0.1.38 release, remaining SlopOS ref cleanup, namespace rebrand (`slopos` → `sigurdos`, file renames), orphaned strdup fix in contacts_screen_show, LoginEntry::in_use default fix, meshcore submodule remote sync, V1 mesh class removal (#290), chat 149-byte cap (#291), channel delete confirmation dialog (#289), NodeStats screen (#288), Multi-ACK toggle (#296), node type selector (#298), buzzer notifications (#300), Reboot button (#314), storage usage display (#315), hop count / advert-path display (#316/#324), message signing (#317), telemetry answer-side (#318), client-repeat (#319), identity backup (#320), contact map markers (#321), URI import (#322), device PIN (#326), QR generation (#328), WiFi AP OTA (#329), Node Discovery Protocol (#331), and GitHub-release OTA download from Settings System.
- **Since last sync:** beta-0.1.39 release, companion BLE bridge (`comms/companion_bridge.cpp/h` — SerialInterface transport, command protocol, offline queue), persistent message store (`mesh/message_store.cpp/h` — SPIFFS binary ring buffer, 32 conversations, 160-char text), Bluetooth screen (#22 — enable/disable toggle, PIN display, connection state, last sync), companion adapter (`mesh/companion_adapter.inc` — PSRAM-allocated bridge, included into mesh_wrapper.cpp), `SigurdOS_TDeck_ble` build env, screen enum completeness audit, mesh public API contract tests, HAL public API contract tests, navigation screen inventory contract, input event API contract, prefs defaults contract, emoji integrity checks, companion protocol wire tests, responsive layout tests, UI timing tests, and 28 additional test modules bringing total to 586 test cases.

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
