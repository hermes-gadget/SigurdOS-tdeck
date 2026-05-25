# SlopOS T-Deck — Agent Onboarding

**You are an AI agent working on the SlopOS T-Deck firmware.** This file is your instruction manual. Read it before modifying code.

**Do not modify this file or `AGENTS.md` in any PR.** They are AI agent context. Only the repo owner changes them. Any PR that touches `CLAUDE.md` or `AGENTS.md` will be rejected without review.

**This file is a mirror of `AGENTS.md`.** Both files contain identical content. Claude Code reads `CLAUDE.md` by default; other agents should read `AGENTS.md`.

---

## Markdown Files in This Repo

These are the reference documents you should load before starting work. Which one to read depends on what you're doing:

| File | When to read it | Purpose |
|------|----------------|---------|
| **`README.md`** | First time in the repo | Project overview, quick start, license |
| **`AGENTS.md`** | Every session | Agent instructions, architecture, conventions, pitfalls |
| **`CLAUDE.md`** ← you are here | Claude Code sessions | Same content — mirror of AGENTS.md |
| **`CONTRIBUTING.md`** | Before ANY task or PR | **Mandatory.** Full contribution workflow, issue-first requirement, PR checklist. AI agents must follow every step. |
| **`KNOWN_ISSUES.md`** | Before feature work | What's broken or unfinished — don't duplicate effort |
| **`firmware/README.md`** | Releasing or CI work | Release artifact structure, web flasher manifest format |
| **`test/README.md`** | Writing new tests | Test framework, mock structure, naming conventions |

**Critical rules — follow both:**
1. **Check for an existing issue on the upstream repo.** Before writing any code, check if there's already an open GitHub issue on `hermes-gadget/SlopOS-tdeck` covering what you plan to do. If not, open one. No issue = no PR accepted.
2. **Read `KNOWN_ISSUES.md`** before starting any feature work. If someone already tried and documented a problem, you'll find it there.
3. **Follow `CONTRIBUTING.md`** — it's not optional. Every step applies to AI agents the same as human contributors.

---

## What Is This

Standalone T-Deck LVGL firmware that runs in the MeshCore mesh network. Think "Discord UI on a LoRa radio." Full mesh protocol compatibility — interoperates with any MeshCore node.

Three repos form the SlopOS ecosystem:

| Repo | What | Stack |
|------|------|-------|
| `hermes-gadget/SlopOS` | MeshCore fork (core library) | C++/PlatformIO, ESP32 |
| **`hermes-gadget/SlopOS-tdeck`** ← **you are here** | T-Deck LVGL firmware | C++/PlatformIO, LVGL v9, LovyanGFX |
| `hermes-gadget/SlopOS-client` | Flutter mobile app | Dart/Flutter, BLE/USB/TCP |

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

# Check test count (varies as tests are added)
pio test -e native_test --list
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
│   ├── home_screen.cpp/h  # 4x3 icon grid, top/bottom bars
│   ├── chat_screen.cpp/h  # Channels, DM, message bubbles
│   ├── screens.cpp/h      # Heard, Contacts, Map, Settings, etc.
│   ├── onboarding_screen.cpp/h  # First-boot setup wizard
│   ├── navigation.cpp/h   # Screen routing with slide transitions
│   └── ui.cpp/h           # Splash→Home transition
├── app/
│   ├── map_renderer.cpp/h # Offline map (PNG tiles via lodepng, PSRAM cache)
│   └── lodepng_alloc.cpp  # lodepng allocator → PSRAM with DRAM fallback
├── diagnostics/
│   └── debug.cpp/h        # Debug dumps (SLOPOS_DEBUG=1 build)
├── fonts/
│   └── emoji_font_setup.cpp/h  # Emoji font fallback for LVGL
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
- `show_screen(scr)` — loads a screen created with `lv_obj_create(nullptr)`
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
| 8 | Advertise (broadcast presence) | `screens.cpp` | ✅ |
| 9 | Settings (radio, keyboard BL, date/time) | `screens.cpp` | ✅ |
| 10 | Trace (path discovery per contact) | `screens.cpp` | ⚠️ see KNOWN_ISSUES.md |
| 11 | Terminal (colored log + commands) | `screens.cpp` | ⚠️ see KNOWN_ISSUES.md |
| 12 | Signal (live RSSI, SNR, radio params) | `screens.cpp` | ✅ |
| 13 | Radio Setup (freq, SF, BW, CR, power) | `screens.cpp` | ✅ |
| 14 | Onboarding (wizard) | `onboarding_screen.cpp` | ✅ |

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
```

**Messages arrive** via `chat_screen_add_msg(channel, sender, text, is_self)`. The chat screen maintains per-channel message caches (8 messages each, 16 channels max).

**Channel protocol:**
- **PSK channels:** "Public" uses PSK `izOH6cXN6mrJ5e26oRXNcg==`. Any node with the matching PSK derives the same channel hash.
- **Hashtag channels:** No PSK needed. Channel hash = SHA256(SHA256(name)). Any node using the same hashtag name creates the same hash.
- Group messages embed `"<sender_name>: <text>"` for interop with MeshCore companion radio firmware.

---

## Testing

```bash
pio test -e native_test -v       # All tests (no hardware, ~200+)
pio test -e native_test -f test_touch -v     # One module
```

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
| `debug <1\|2\|3>` | `debug 1` | Set debug verbosity (1=quiet, 2=normal, 3=verbose) |

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

### Debug Levels

The debug build system (`SLOPOS_DEBUG=1`) supports three verbosity levels, controlled at build time via `-D SLOPOS_DEBUG_LEVEL=N` or at runtime via the `debug <1|2|3>` serial command:

| Level | Name | Output | Default Env |
|-------|------|--------|-------------|
| 1 | Quiet | Only `[test]` responses from the test controller. No `[flush]`, `[stat]`, or `[pins]` output. | `SlopOS_TDeck_remote_test` |
| 2 | Normal | All debug output: `[flush]` per frame, `[stat]` + `[pins]` every 5s. | `SlopOS_TDeck_debug` |
| 3 | Verbose | Level 2 output plus on-demand heavy dumps (`dump_system`, object tree, etc.). | — |

When any `SLOPOS_DEBUG` build is running, the display auto-off timer is disabled — the screen stays on so you can observe behavior without needing to wake it. This only applies to debug builds; release builds retain the 30-second auto-off.

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

## AI Agent Workflow

When working on this codebase, follow this sequence:

1. **Open an issue on the upstream repo** — check if an open issue on `hermes-gadget/SlopOS-tdeck` already covers what you plan to do. If not, create one. No issue = no PR accepted.
2. **Read `CONTRIBUTING.md`** — follow every step. It applies to AI agents the same as human contributors.
3. **Load context** — read `AGENTS.md` (or `CLAUDE.md`, they are identical), `KNOWN_ISSUES.md`, and any relevant source files
4. **Check the branch** — work is always on `dev`. PRs target `dev`, not `main`
5. **Run tests first** — `pio test -e native_test` before any changes to confirm baseline
6. **Make changes** — use the file tools (`read_file`, `patch`, `write_file`)
7. **Run tests again** — all tests must pass
8. **Build firmware** — `pio run -e SlopOS_TDeck` must succeed
9. **Commit and push** — conventional commit messages (`feat:`, `fix:`, `docs:`, etc.)

### Bug Spotting

If you find a bug while working that is not directly related to your PR, do not ignore it. Add it to `KNOWN_ISSUES.md` using the standard format below. This lets the project catch bugs faster — you found it, you document it, and a maintainer validates it during PR review.

**Standard entry format — insert a new section in `KNOWN_ISSUES.md`:**

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

### Undocumented commands
The built-in serial/diagnostics terminal exposes several internal commands but there is no documentation on what is available or what each command does. Users have to read the source code to discover features.

**What's needed:** A `help` command that lists all available commands with a one-line description.
```

```
## GPS

### No NMEA checksum validation
Raw GPS NMEA sentences from the L76K module include a `*XX` checksum suffix that is never validated (`gps.cpp`). Corrupted sentences from noisy GPS reception are parsed as valid data, potentially giving incorrect coordinates, altitude, or fix status.

**What's needed:** Implement NMEA checksum validation — extract the checksum from after the `*` in the sentence, compute XOR of all bytes between `$` and `*`, and compare. Discard sentences that dont match.
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
- [ ] No `\n` literal where real newline was intended

#### Concurrency / Timing
- [ ] `lv_obj_del` in event handler — should be `lv_obj_del_async()`
- [ ] Auto-delete screens — `lv_scr_load_anim(..., true)` deletes all children; `LV_EVENT_DELETE` needed to null globals
- [ ] LVGL tick starvation — any blocking operations that delay `lv_timer_handler()`?
- [ ] `ESP.restart()` without flash write delay — SPIFFS/NVS write may not have completed

#### Testing
- [ ] Tests added or updated for every change
- [ ] `pio test -e native_test -v` passes (all tests)
- [ ] `pio run -e SlopOS_TDeck` builds without error
- [ ] PlatformIO discovers test dirs correctly (`test/test_<name>/main.cpp`)
- [ ] PR body declares how hardware testing was done: "Remote test" (serial-controlled), "Physical hardware test", or both. If neither is stated, auto-decline.

#### Known Issue Detection
- [ ] Does this PR fix a documented issue in `KNOWN_ISSUES.md`? If so, remove that section.
- [ ] Does the PR add new entries to `KNOWN_ISSUES.md`? Verify each one is real — check the source code or reproduce the issue. Remove any that are speculative.
- [ ] Did testing reveal new issues? If so, add them to `KNOWN_ISSUES.md`.
- [ ] Does the PR introduce a new dependency? Check GPL-3.0 compatibility.
- [ ] Is there any comment saying "this might break" or "temporary fix"? Investigate before merging.

### PR & Review Workflow

**For reviewers (maintainer only beyond step 5):**
1. List PRs: `gh pr list --repo hermes-gadget/SlopOS-tdeck --state open`
2. Check diff: `gh pr diff N` or `git fetch origin pull/N/head:pr-N && git diff dev...pr-N`
3. Verify PR body declares testing method — "Remote test" (serial-controlled), "Physical hardware test", or both. If missing, decline with: "PR must state how hardware testing was done — remote test, physical hardware, or both."
4. Build: `pio run -e SlopOS_TDeck`
5. Test: `pio test -e native_test -v` (all tests must pass)
6. Run the [Code Audit Checklist](#code-audit-checklist) — check every applicable item
7. If the PR came from an AI agent: read the full diff for logic errors beyond what the agent self-checked. Agents miss subtle race conditions and edge-case buffer overflows.
8. Merge: `gh pr merge N --squash --delete-branch --repo hermes-gadget/SlopOS-tdeck`
9. If merge fails (conflicts): cherry-pick new commits only, or squash-merge locally
10. If PR branch has stale commits: cherry-pick new commits onto dev, close PR
11. Close the related issue with notes describing what was done

### Rejection triggers

| Trigger | Why |
|---------|-----|
| Unconditional `Serial.printf` without `#if defined(SLOPOS_DEBUG)` guard | Leaks debug output to production builds |
| Test suite not passing | Any single failure rejects the PR |
| Hardcoded colors instead of theme constants | Breaks the pixel theme — use `theme.h` |
| Missing `apply_dark_bg()` on screen backgrounds | Background won't match the dark theme |
| `\n` literal instead of real newline | Prints literal backslash-n, not a line break |
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
| **Branch from `dev`** | `main` is for stable releases only. PRs target `dev`. |
| **Pull regularly** | Before starting work: `git checkout dev && git pull origin dev` |
| **Rebase your feature branch** | Before opening a PR: `git rebase dev` to avoid stale-commit noise |
| **Run the full test suite** | `pio test -e native_test` must pass all tests before pushing |
| **Read `KNOWN_ISSUES.md`** | Before starting feature work to avoid duplicating effort |
| **Follow screen conventions** | New screens need `apply_dark_bg()`, `make_screen_full()`, and consistent pixel helpers |

---

## Gotchas & Pitfalls

| Gotcha | Details |
|--------|---------|
| Display init | ST7789 native = 240x320 portrait. Set panel 240x320, then `rotation(1)` for 320x240 landscape |
| Backlight | `_panel.setLight(&_light)` before `setPanel(&_panel)` — missing = black screen |
| LVGL tick | `lv_tick_set_cb()` after `lv_init()` — missing = refresh never fires |
|| Touch coords | After rotation(1): SWAP_XY=true, MIRROR_X=false, MIRROR_Y=true |
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
| GPS NMEA | No checksum validation in `gps.cpp` — corrupted sentences parse as valid coordinates |
| Terminal labels | Each command creates a new LVGL label with no pruning — cap at 64 lines |
| Emoji truncation | Byte-level msg truncation can split 4-byte emoji, sending invalid UTF-8 over mesh |
| SPI bus sharing | Display (SPI3_HOST, 40MHz) and SD card (HSPI=SPI3_HOST, 4MHz) share same SPI peripheral. LovyanGFX comment says "SPI2" but code uses SPI3 — fragile |
| Debug shadow mode | `SLOPOS_TRACKBALL_DEBUG_SHADOW` drops trackball events instead of logging+forwarding |
| Debug.h header guards | Declaration in `debug.h` is unconditional, but `debug.cpp` wraps all implementation in `#if defined(SLOPOS_DEBUG)` — latent linker risk |
