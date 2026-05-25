# Known Issues

This document tracks known issues, bugs, and missing features in SlopOS. Contributions welcome — pick something from the list and open a PR.

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
The navigation system uses a circular buffer with MAX_HISTORY=8. `push_history` wraps around and overwrites the oldest entry when full. `pop_history` decrements `history_top` without wrapping — after wrapping occurs, the stack has no way to distinguish new entries from overwritten ones. The stack can only hold 8 items but there are 14+ screen types.

**What's needed:** Replace the circular buffer with a simple linear stack: drop the oldest entry when full instead of wrapping. Fix `pop_history` to handle wrap-around correctly.

---

## Chat Screen

### REPEATERS tile navigates to Packets screen instead of a nodes/repeaters view
The REPEATERS tile on the home screen 4x3 grid navigates to `Screen::Heard` — the same raw Packets log as the PACKETS tile (`home_screen.cpp:62-64`). Both tiles open identical screens. FINDER correctly navigates to `Screen::Network`. There is no dedicated screen for listing nearby nodes by signal strength.

**What's needed:** Either create a dedicated repeaters/nodes screen that lists known contacts sorted by RSSI, or redirect REPEATERS to `Screen::Network` (Finder) which already surfaces nearby nodes. The current double-mapping gives users two identical icon tiles with no functional difference.

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

## SPI / Display

### Display and SD card share the same SPI host
The display uses `SPI3_HOST` (`display.cpp:45`) and the SD card also uses `HSPI` (`sdcard.cpp:31`). On ESP32-S3, `HSPI` maps to `SPI3_HOST` — the same SPI peripheral. Both configure the same host through different driver instances (LovyanGFX internal vs Arduino SPIClass). The comment in `sdcard.cpp:29` says "LovyanGFX and RadioLib use SPI2 (FSPI)" which is incorrect — the display code clearly uses SPI3_HOST. While this works in practice because each transaction reconfigures the GPIO matrix, clock speed differences (40MHz display vs 4MHz SD) create a fragile architecture where one driver's transaction can interfere with the other's register state.

**What's needed:** Either (a) move SD card to `FSPI` (`SPI2_HOST`), which is the default Arduino SPI bus and not used by the display, or (b) move the display to `SPI2_HOST` and keep SD on `SPI3`. Update the comment in `sdcard.cpp` to reflect the actual bus assignment.
