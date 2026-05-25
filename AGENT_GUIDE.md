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
pio test -e native_test --list-tests 2>/dev/null || pio test -e native_test -h
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
│   └── mesh_wrapper.cpp/h # Public API for the UI layer
├── ui/
│   ├── theme.h            # Colors, pixel helpers (apply_pixel_*)
│   ├── responsive.h       # Display-size-agnostic layout helpers
│   ├── home_screen.cpp/h  # 4x3 icon grid, top/bottom bars, battery/signal/time
│   ├── chat_screen.cpp/h  # Channels, DM, message bubbles
│   ├── screens.cpp/h      # Heard, Contacts, Map, Settings, Trace, Terminal, Signal, Channels, Finder, Advertise, Radio Setup, Custom RF
│   ├── onboarding_screen.cpp/h  # First-boot setup wizard
│   ├── navigation.cpp/h   # Screen routing with slide transitions
│   └── ui.cpp/h           # Splash→Home transition, main loop updates
├── app/
│   ├── map_renderer.cpp/h # Offline map (PNG tiles via lodepng, PSRAM cache)
│   └── lodepng_alloc.cpp  # lodepng allocator → PSRAM with DRAM fallback
├── diagnostics/
│   └── debug.cpp/h        # Debug dumps (SLOPOS_DEBUG=1 build)
├── fonts/
│   └── emoji_font_setup.cpp/h  # Emoji font fallback for LVGL
├── utils/                 # (empty — placeholder for utility modules)
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

Theme helpers in `src/ui/theme.h`:
- `apply_dark_bg(obj)` — sets full-screen black
- `apply_pixel_card(obj)` — `BG_TERTIARY`, 0 radius, 2px border
- `apply_pixel_btn(obj)` — filled `ACCENT` button
- `apply_pixel_btn_outline(obj)` — transparent with `ACCENT` border
- `apply_pixel_input(obj)` — input field, `BG_INPUT`, 2px border
- `apply_topbar_icon_btn(btn)` — themed back button styling, state-aware

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
| 1 | Home (4x3 grid) | `home_screen.cpp` | ✅ |
| 2 | Chat (channels + DM) | `chat_screen.cpp` | ✅ |
| 3 | Contacts (alphabetical, tap→DM) | `screens.cpp` | ✅ |
| 4 | Channels (list + create #hashtag/PSK) | `screens.cpp` | ✅ |
| 5 | Finder (nearby nodes) | `screens.cpp` | ✅ |
| 6 | Packets (raw packet log, 50 entries) | `screens.cpp` | ✅ |
| 7 | Map (touch pan, auto-center) | `screens.cpp` | ✅ |
| 8 | Advertise (broadcast presence, status timer) | `screens.cpp` | ✅ |
| 9 | Settings (radio, keyboard BL, date/time) | `screens.cpp` | ✅ |
| 10 | Trace (path discovery per contact) | `screens.cpp` | ⚠️ see docs/KNOWN_ISSUES.md |
| 11 | Terminal (colored log + commands) | `screens.cpp` | ⚠️ see docs/KNOWN_ISSUES.md |
| 12 | Signal (live RSSI, SNR, noise floor, radio params) | `screens.cpp` | ✅ |
| 13 | Radio Setup (freq presets, SF/BW/CR/Pwr controls, save & reboot) | `screens.cpp` | ✅ |
| 14 | Onboarding (wizard) | `onboarding_screen.cpp` | ✅ |
| — | Custom RF (sub-screen of Radio Setup — Freq, SF, BW, CR, Pwr text inputs with Apply) | `screens.cpp` | ✅ |

---

## Mesh Integration

MeshCore is a submodule at `lib/meshcore/`. The UI never touches MeshCore directly — all calls go through `slopos::mesh::*` in `mesh_wrapper.h`.

**Key mesh wrapper API:**

```cpp
slopos::mesh::init(spiffs_ok)          // Boot — load identity, start radio
slopos::mesh::loop()                   // Call from main loop
slopos::mesh::sendMessage(name, text)  // Direct message
slopos::mesh::sendChannelMessage(ch, text)  // Group message
slopos::mesh::addChannel(name, psk_b64)     // PSK channel
slopos::mesh::addHashtagChannel(name)       // Hash-of-name channel
slopos::mesh::exportContacts(out, max)      // Name list
slopos::mesh::exportChannels(out, max)      // Channel name list
slopos::mesh::getNoiseFloor()              // Current noise floor dBm
slopos::mesh::getLastRSSI()                // Last received message RSSI
slopos::mesh::getLastSNR()                 // Last received message SNR
slopos::mesh::getContactCount()            // Number of known contacts
slopos::mesh::getChannelCount()            // Number of joined channels
slopos::mesh::sendAdvert()                 // Broadcast advert
```

**Messages arrive** via `chat_screen_add_msg(channel, sender, text, is_self)`. The chat screen maintains per-channel message caches (8 messages each, 16 channels max).

**Channel protocol:**
- **PSK channels:** "Public" uses PSK `izOH6cXN6mrJ5e26oRXNcg==`. Any node with the matching PSK derives the same channel hash.
- **Hashtag channels:** No PSK needed. Channel hash = SHA256(SHA256(name)). Any node using the same hashtag name creates the same hash.
- Group messages embed `"<sender_name>: <text>"` for interop with MeshCore companion radio firmware.

---

## Testing

**Current test count: 172** (171 passed, 1 skipped for native_test).

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

# Run native tests (no hardware)
pio test -e native_test -v
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
| `nav chat` | `nav chat` | Navigate to screen (home/chat/contacts/channels/network/heard/map/settings/terminal/radio/trace/signal/advertise) |
| `back` | `back` | Go back in nav stack |
| `tb up` | `tb click` | Simulate trackball (up/down/left/right/click, or u/d/l/r/c) |
| `type hello` | `type Hello World` | Type text — queued and injected one char per loop cycle |
| `press enter` | `press backspace` | Press special key (enter/backspace/esc/tab) |
| `inject Alice Hello!` | `inject Bob channel=general hi` | Simulate incoming mesh message (no radio!) |
| `screen` | `screen` | Show current screen name |
| `status` | `status` | Show heap and PSRAM |

Safety guarantees:
- No LoRa radio initialised — `slopos::mesh::init()` is never called
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

---

## Versioning & Release

Main + dev branch model:
- `dev` — integration branch. All PRs merge here.
- `main` — stable releases only.
- Tags: `beta-0.1.XX` (zero-padded for correct sort: `beta-0.1.09` not `beta-0.1.9`)

**Release flow (maintainer only):**
1. Update `SLOPOS_VERSION` in `tdeck_pins.h`
2. `pio run -e SlopOS_TDeck`
3. `cp .pio/build/SlopOS_TDeck/firmware-merged.bin firmware/firmware-merged.bin`
4. Commit, tag, push, `gh release create`

---

## Radio Setup

The Radio Setup screen (`screens.cpp:1804`) provides a two-column layout:
- **Left column:** Frequency presets (868.000 EU, 869.525 UK, 869.618 UK, 915.000 US, 433.500 EU)
- **Right column:** "Custom RF..." button → opens Custom RF sub-screen, SF −/+ controls (7-12), BW −/+ controls (steps through 500/250/125/62.5/41.7/31.25/20.8/15.6/10.4/7.8 kHz), TX power −/+ controls (2-22 dBm)
- **Bottom:** Save & Reboot button (writes prefs, saves channels and messages, then `ESP.restart()`)

Radio params are stored in module-level `static` vars (`s_rf_freq`, `s_rf_sf`, `s_rf_bw`, `s_rf_cr`, `s_rf_pwr`) in `screens.cpp:1666-1670`, shared between `radio_setup_screen_show` and `custom_rf_screen_show`. These defaults to 869.618 MHz / SF8 / 62.5 kHz / CR 4/5 / 22 dBm.

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
| Terminal `term_add_line` unbounded | Each line creates a new `lv_label_create(log)` — no model-level cap. The docs/KNOWN_ISSUES.md entry about unbounded label accumulation has NOT been fixed. |
| Radio Setup Save & Reboot no delay | Calls `ESP.restart()` immediately after `prefs_set()`, `saveChannels()`, and `chat_save_messages()` — no delay for flash writes to complete (see docs/KNOWN_ISSUES.md) |
| SF label via `user_data` | The SF −/+ buttons use `(void*)sf_lbl` as event user_data, but `sf_lbl` is a stack-local variable in `radio_setup_screen_show`. While `sf_lbl` is alive for the screen's lifetime, the pointer is only valid as long as the widget exists — fragile if screen is recreated |
| BW discrete stepping | BW uses a hardcoded float array and cycles through values via a loop with `x + 0.01f` tolerance — values near boundaries may not snap correctly due to float comparison |
| `src/utils/` empty directory | Exists but contains no files — may be a leftover or placeholder, not currently used |

---

## Known Issues Reference

All known issues are documented in `docs/KNOWN_ISSUES.md`. Key categories:
- **Chat Screen:** Channel selection preview clipping, text input max_length off-by-one, emoji truncation on multi-byte codepoints, three home tiles mapping to same screen
- **Emoji Support:** Incomplete glyph coverage (PR #25)
- **Trackball:** LEFT fires on both edges, universal back-swipe not implemented
- **Signal Bars:** No visual RSSI indicator
- **Finder:** Zero-hop ping not implemented
- **Terminal:** Undocumented commands, unbounded label accumulation
- **Launcher Compatibility:** SlopOS doesn't work under bmorcelli/Launcher
- **UI Performance:** LVGL tick starvation during TX, save_counter overflow
- **Mesh Networking:** Channel hash only checks first byte, no contact expiry, advert rate limiting only at UI layer, missing null-termination on short payloads
- **Map Screen:** Large allocations not using PSRAM, LRU cache clock uint32_t wrap
- **Touch/Input:** I2C bus speed race (100kHz instead of 400kHz)
- **Screen Navigation:** Navigation history stack broken (circular buffer UB)
- **Onboarding:** ESP.restart() without flash write completion
- **GPS:** No NMEA checksum validation
- **SPI/Display:** Display and SD card share same SPI host
- **Diagnostics:** Debug shadow mode drops events, header guard mismatch

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
