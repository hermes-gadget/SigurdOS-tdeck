# Known Issues

This document tracks known issues, bugs, and missing features in SlopOS. Contributions welcome — pick something from the list and open a PR.

---

## Chat Screen

### Channel selection — message preview clipping
When scrolling through channels in the channel selector, message previews (the last message in each channel) don't properly truncate. Long messages overflow the preview area and clip visually, overlapping adjacent UI elements.

**What's needed:** Proper string truncation in the channel list — clamp preview text to fit the available width, appending "..." when truncated. The `build_channel_string` function in `home_screen.cpp` was recently hardened (PR #29) — a similar approach should be applied to the preview text in the channel selector.


## Trackball Navigation

### Universal back-swipe not implemented
The trackball left-swipe (back/navigate to previous screen) currently only works on the Chat screen (PR #21). Every other screen requires reaching for the back button in the top-left corner, which is awkward during one-handed use.

**What's needed:** Make the trackball left-swipe a universal back gesture on every screen. Implementation pattern from `chat_screen.cpp` should be extracted into `navigation.cpp/h` so all screens can register a back-swipe handler.

However, this needs to handle screens that have their own left/right navigation (e.g. scrolling through contacts alphabetically, paging through channel lists, or horizontal content). On those screens, a single left-swipe would conflict with the screen's own navigation. Two approaches:
- **Two-swipe commit:** The first left-swipe deselects/neutralises the current screen's navigation state (e.g. exits the scroll context). The second left-swipe then triggers the back gesture. This gives the user a deliberate two-step flow — "navigate past content, then go back."
- **Timing-based:** A quick left swipe scrolls the content; a longer hold-and-swipe-left triggers the back gesture. Differentiable by swipe speed/duration.
- **Edge zone:** Only trigger back-swipe when the trackball is swiped left from a neutral/resting state (no active list selection). If the user is actively scrolling a channel list, the left swipe scrolls the list instead.

The two-swipe commit approach is the most intuitive and least prone to accidental backs. It also gives clear visual feedback — the first swipe can clear any selection highlights or scroll to the top of the list, making it obvious that the next left swipe will go back.

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

---

## UI Performance

### LVGL tick starvation during LoRa TX
The main loop calls `slopos::mesh::loop()` (which may do blocking LoRa TX/RX taking 100-500ms) before `lv_timer_handler()`. During that period, LVGL is not serviced, causing visible UI stuttering — animations freeze, button feedback lags, and scrolling jerks.

**What's needed:** Defer long mesh operations to a separate task or state machine, interleave `lv_timer_handler()` calls during mesh loop iterations, or use FreeRTOS task priorities to keep LVGL responsive.

### Status bar save_counter overflow
`ui.cpp` uses a `uint8_t save_counter` that increments without resetting after reaching 10. It continues incrementing to 255, causing the save counter to trip continuously for ~245 iterations (every 30s), then pauses for ~2 hours until uint8_t wraps around to 0. During the pause, settings are not persisted.

**What's needed:** Reset `save_counter = 0` after triggering the save, not just `>= 10`.

---

## Mesh Networking

### Channel hash lookup only checks first byte
The `searchChannelsByHash` function in `slop_mesh.h` compares only the first byte of the 32-byte channel hash to find a matching channel. With 8 channels and uniformly random hashes, there is an ~11% collision probability. When a collision occurs, an encrypted group message is decrypted with the wrong channel key, producing garbage text and displaying the wrong channel name.

**What's needed:** Replace the single-byte `hash[0] == _channels[i].channel.hash[0]` comparison with a full `memcmp` of the entire hash array.

### No contact expiry / eviction
The contact list has a hard cap of 64 entries (`SLOP_MAX_CONTACTS`). Once full, new contacts are silently dropped. There is no TTL-based eviction, LRU replacement, or purge of stale entries. Contacts not seen for hours or days still occupy a slot, preventing discovery of new nodes.

**What's needed:** Implement periodic eviction — purge contacts with `last_seen` older than a configurable threshold (e.g. 30 minutes) when the list is full.

### Advert rate limiting at mesh layer only
The 10-second advert cooldown is only enforced in the UI (button disabled state). The `sendAdvert()` function in `mesh_wrapper.cpp` has no rate-limiting of its own — it can be called programmatically (e.g. from the Terminal's `advert` command) without protection, potentially flooding the mesh.

**What's needed:** Add a timestamp check in `sendAdvert()` that rejects calls within `ADVERT_COOLDOWN_SECONDS`.

### Missing null-termination on short payloads
In `slop_mesh.h`, the payload text null-termination is conditional: `if (len > 1) data[len - 1] = '\0'`. For `len == 1`, no null byte is written, so the C string read from `data` may run past the buffer, causing undefined behavior or leaking stack data.

**What's needed:** Always null-terminate: `data[len - 1] = '\0';` unconditionally.

---

## Map Screen

### Large allocations not using PSRAM
The map renderer allocates large buffers (131KB tile cache, 153KB canvas) via `lv_malloc()`, which draws from DRAM heap (~320KB total). Two consecutive map renders can consume 284KB of DRAM, leaving only ~36KB for LVGL and other operations, causing allocation failures and crashes.

**What's needed:** Use `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` for large map buffers, keeping only small allocations (4KB JPEG input buffer) on DRAM.

### LRU cache clock uint32_t wrap
The tile LRU cache uses a `uint32_t cache_clock` that increments monotonically. After ~4 billion increments (or ~50 days of continuous use at 1kHz), it wraps to 0, breaking cache eviction comparisons — newly cached tiles have lower `last_used` values than old ones, causing premature eviction of recently used tiles.

**What's needed:** Use `uint64_t` for `cache_clock`, or implement a wrapping-aware comparison.

---

## Touch / Input

### I2C bus speed race — touch runs at 100kHz instead of 400kHz
The I2C bus is shared between the GT911 touch controller (400kHz capable) and the keyboard MCU (100kHz). The init sequence sets 400kHz for touch, then overwrites it to 100kHz during keyboard init. After that, ALL subsequent I2C operations (including touch reads) run at 100kHz. Touch is functional but reads at 1/4 speed, increasing touch latency by ~3-4x.

**What's needed:** Restore `Wire.setClock(400000)` after keyboard init completes, or use 100kHz for both (GT911 works at any speed up to 400kHz).

### Trackball LEFT fires on both edges
All trackball directions fire on falling edge only (one event per physical detent). LEFT fires on BOTH rising and falling edges, producing 2 events per detent. The 80ms deadtime (vs 150ms for others) doesn't prevent the double-fire — it only limits the minimum inter-event gap. In UI navigation, moving LEFT advances 2 items while other directions advance 1.

**What's needed:** Change LEFT to fire on falling edge only (same as other directions), or implement edge-agnostic debounce that fires exactly once per detent.

---

## Screen Navigation

### Navigation history stack is broken
The navigation system uses a circular buffer with MAX_HISTORY=8. `push_history` wraps around and overwrites the oldest entry when full. `pop_history` decrements `history_top` without wrapping — after wrapping occurs, it reads `history[-1]` (out-of-bounds, undefined behavior). The stack can only hold 8 items but there are 14+ screen types.

**What's needed:** Replace the circular buffer with a simple linear stack: drop the oldest entry when full instead of wrapping. Fix `pop_history` to never read below index 0.

---

## Chat Screen

### Text input max_length exceeds buffer
`lv_textarea_set_max_length(input_field, 160)` allows 160 characters, but `ChannelMessage::text` is `char text[160]` — 159 chars + 1 null terminator. The max_length should be 159 to prevent a 1-byte overflow of the null terminator.

**What's needed:** Change max_length to 159.

### Home screen three tiles map to same screen
The home screen 4x3 grid has three tiles (REPEATERS, FINDER, HEARD) that all navigate to `Screen::Heard`. This appears to be unintentional — they should navigate to separate screens or at least be documented.

**What's needed:** Verify the intended behavior and either fix the screen targets or document the duplicate mapping.

---

## Onboarding

### ESP.restart() without flash write completion
The onboarding screen's Done button calls `ESP.restart()` immediately after `chat_save_messages()`. If the SPIFFS write hasn't completed (due to write caching), the save data is lost after reboot.

**What's needed:** Add a small delay (`delay(100)`) between the save and the restart, or set a flag for the main loop to handle the restart.

---

## GPS

### No NMEA checksum validation
Raw GPS NMEA sentences from the L76K module include a `*XX` checksum suffix that is never validated (`gps.cpp`). Corrupted sentences from noisy GPS reception are parsed as valid data, potentially giving incorrect coordinates, altitude, or fix status.

**What's needed:** Implement NMEA checksum validation — extract the checksum from after the `*` in the sentence, compute XOR of all bytes between `$` and `*`, and compare. Discard sentences that don't match.

---

## Terminal

### Unbounded label accumulation
Each command in the Terminal screen creates a new LVGL label widget (`screens.cpp:1221-1227`). There is no upper bound or pruning — after hundreds of commands, thousands of label widgets accumulate in the LVGL object tree, consuming heap. Labels are only freed when the user navigates away.

**What's needed:** Cap the number of visible terminal lines (e.g. 64), deleting the oldest label when the cap is reached. A `lv_obj_clean()` on the log container before adding the new line would also work but is more disruptive to the scroll state.

---

## Chat Screen

### Emoji truncation on multi-byte codepoints
The send path truncates message text by byte count (`chat_screen.cpp:933`), not codepoint boundary. If a 4-byte emoji (e.g. 🚀 = `\xF0\x9F\x9A\x80`) starts at byte 147 of 149, only 2 of the 4 bytes are copied, producing invalid UTF-8 which is then transmitted over the mesh.

**What's needed:** Replace byte-level truncation with codepoint-aware truncation — walk backward from the limit to ensure the last character boundary is valid, or reduce the max byte count to account for multi-byte trailing characters.

---

## Mesh Networking

### Missing null-termination on short non-text payloads
In `slop_mesh.h:93-95`, the payload text null-termination is conditional: `if (len > 1) data[len - 1] = '\0'`. For non-text payloads with `len == 1`, no null byte is written, so downstream uses of `strlen()`, `strncpy()`, or string formatting read past the buffer into uninitialized stack data.

**What's needed:** Always null-terminate: `data[len - 1] = '\0';` unconditionally (see also existing entry about the same issue in `slop_mesh.h`).

---

## SPI / Display

### Display and SD card share the same SPI host
The display uses `SPI3_HOST` (`display.cpp:45`) and the SD card also uses `HSPI` (`sdcard.cpp:31`). On ESP32-S3, `HSPI` maps to `SPI3_HOST` — the same SPI peripheral. Both configure the same host through different driver instances (LovyanGFX internal vs Arduino SPIClass). The comment in `sdcard.cpp:29` says "LovyanGFX and RadioLib use SPI2 (FSPI)" which is incorrect — the display code clearly uses SPI3_HOST. While this works in practice because each transaction reconfigures the GPIO matrix, clock speed differences (40MHz display vs 4MHz SD) create a fragile architecture where one driver's transaction can interfere with the other's register state.

**What's needed:** Either (a) move SD card to `FSPI` (`SPI2_HOST`), which is the default Arduino SPI bus and not used by the display, or (b) move the display to `SPI2_HOST` and keep SD on `SPI3`. Update the comment in `sdcard.cpp` to reflect the actual bus assignment.

---

## Diagnostics

### Debug mode issues
When `SLOPOS_TRACKBALL_DEBUG_SHADOW` is defined (`trackball.cpp:91-93`), the debug print fires but the event is short-circuited — all trackball input is silently dropped during shadow debugging.

The `debug.h` header declares functions unconditionally, but `debug.cpp` wraps all implementation in `#if defined(SLOPOS_DEBUG)`. Any non-debug code that calls a debug function will get a linker error. Current call sites are properly guarded, but this is a latent risk for future code.

**What's needed:** Fix the shadow debug mode so it logs events but still queues them. Wrap debug.h declarations in the same `#if defined(SLOPOS_DEBUG)` guard, or provide empty inline stubs.