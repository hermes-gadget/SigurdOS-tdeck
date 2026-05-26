# Known Issues

This document tracks known issues, bugs, and missing features in SlopOS. Contributions welcome — pick something from the list and open a PR.

---

## Boot / Board Support

### Duplicate TDeckBoard instances — radio driver board never initialised (`mesh_wrapper.cpp:33`)
**FIXED in PR #133:** Added `board.begin()` call at the start of `mesh::init()`. The mesh-layer board instance now has `begin()` called before the radio driver uses it, so `getStartupReason()` correctly detects DIO1 wake-from-deep-sleep and the buffered LoRa packet is received instead of dropped.

### `new Module(...)` at static init time with no null-check (`mesh_wrapper.cpp:35`)
**FIXED in PR #134:** Deferred RadioLib Module, CustomSX1262, and radio driver allocation from static init time (before PSRAM available) into `mesh::init()`. All three allocations now have null-checks with serial diagnostics on OOM failure.

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
**FIXED in PR #110:** Two-part fix: (1) reordered main loop so `slopos_display_loop()` (which calls `lv_timer_handler()`) runs before `slopos::mesh::loop()`. (2) Added periodic `lv_timer_handler()` calls inside `mesh_wrapper.cpp::loop()` with a 20ms guard (~50 Hz), ensuring UI stays responsive even during sustained mesh activity.

### `lv_obj_del()` called synchronously inside LV_EVENT_CLICKED handler (`screens.cpp:1527`)
The Trace result screen deletes `trace_result_label` with `lv_obj_del()` directly inside an event callback. LVGL's event dispatch loop may still hold references to the object after the callback returns. Other handlers in the same file correctly use `lv_obj_del_async()`, making this an isolated omission. This can cause use-after-free crashes in the LVGL event system.

**What's needed:** Replace with `lv_obj_del_async(trace_result_label)` and immediately set `trace_result_label = nullptr` after scheduling the async delete.

### Static local variables in map screen lambda persist across re-entries (`screens.cpp:690-691`)
The map screen touch handler uses `static int drag_start_x/y` and `static uint32_t map_last_render_ms`. These values survive navigation away from and back to the map screen. On re-entry, `map_last_render_ms` retains its old value, which can cause the first map re-render to be skipped if the elapsed-time guard (`now - map_last_render_ms >= 200`) evaluates as not-yet-expired.

**What's needed:** Reset all three statics to sentinel values (0 for coordinates, 0 for timestamp) in an `LV_EVENT_SCREEN_LOADED` handler, or convert them to non-static function-local variables that are re-initialised on every entry.

---

## Mesh Networking

### Channel hash lookup only checks first byte
**FIXED:** Replaced single-byte comparison with full `memcmp` of the entire 32-byte channel hash. No collision window remaining.

### No contact expiry / eviction
**FIXED in PR #109:** Replaced the silent `return` in `onAdvertRecv` with LRU eviction. When the contact list is full (`SLOP_MAX_CONTACTS=64`) and a new contact arrives, the contact with the oldest `last_seen` timestamp is evicted and the new contact takes its slot. Added 9 unit tests.

### Advert rate limiting at mesh layer only
**FIXED:** `sendAdvert()` now has an internal static timestamp guard that rejects calls within the cooldown window, regardless of caller.

### Missing null-termination on short payloads
**FIXED in PR #97:** The `onPeerDataRecv` handler now unconditionally null-terminates payloads. The `else if (len > 0)` guard ensures `data[len - 1]` is always in-bounds.

### `sendTrace()` indentation anomaly suggesting merge artifact (`slop_mesh.h:265-271`)
Lines 265–271 inside `sendTrace()` are at column 0 (8-space indent would match the rest of the function). The code compiles and is logically correct, but the anomalous indentation suggests a botched merge conflict resolution. A future edit that relies on alignment for structure comprehension risks introducing a logic error.

**What's needed:** Fix the indentation of lines 265–271 to match the method body (use 8-space indent).

---

## Map Screen

### LRU cache clock uint32_t wrap
**FIXED:** Changed `cache_clock` from `uint32_t` to `uint64_t`, eliminating the ~50-day wrap-around that broke cache eviction ordering.

---

## Touch / Input

### I2C bus speed race — touch runs at 100kHz instead of 400kHz
**FIXED:** `Wire.setClock(400000)` is now restored at the top of each touch poll cycle, ensuring touch reads always run at full speed regardless of the keyboard scan having previously set 100kHz.

### Trackball LEFT fires on both edges
**FIXED in PR #111:** Removed the LEFT-specific exception in `scan_direction` — LEFT now fires on falling edge only, matching UP/DOWN/RIGHT.

### `slopos_keyboard_consume_event()` clears its event flag as a side effect (`keyboard.cpp:191`)
`slopos_keyboard_consume_event()` returns true and simultaneously clears the internal `has_new_event` flag. A second call before `slopos_keyboard_consume_key()` returns false even though the key value has not been read. The `has_*` naming convention universally implies a non-mutating predicate, so this violates caller expectations without documentation.

**What's needed:** Either rename to `slopos_keyboard_consume_event()` to make the side effect explicit, or split into `slopos_keyboard_has_event()` (non-destructive) + `slopos_keyboard_clear_event()` (explicit clear).

### GT911 INT-pin-HIGH release check may drop buffered touch events on rapid taps (`touch.cpp`)
The touch polling loop treats an INT pin HIGH as an immediate release and stops reading. The GT911 controller buffers multiple touch scan intervals internally; if the user taps twice rapidly and the INT pin de-asserts between scans, the second tap is never read from the FIFO. This manifests as intermittent missed taps on fast-response buttons (Send in chat, number keys in settings).

**What's needed:** After reading a touch point, re-read the GT911 point count register before reporting release. Only report `LV_INDEV_STATE_RELEASED` when the point count confirms zero active touches, rather than relying solely on the INT pin level.

---

## Screen Navigation

### Navigation history stack is broken
**FIXED:** Replaced the circular buffer with a simple linear stack. When full, the oldest entry is dropped instead of wrapping. `pop_history` no longer wraps — pure linear push/pop semantics.

---

## Chat Screen

### REPEATERS tile navigates to Packets screen instead of a nodes/repeaters view
**FIXED:** REPEATERS now navigates to `Screen::Network` (the Finder screen). Both REPEATERS and FINDER open the same network view.

### Contact name retrieved by hardcoded child index instead of user data (`screens.cpp:551`)
When the user taps a contact row to open a DM, the contact name label is extracted via `lv_obj_get_child(target, 1)` — a hardcoded positional index. If any future change adds or reorders children in the contact row (e.g. an online indicator icon, signal strength badge), this silently retrieves the wrong widget, resulting in a DM opened with a garbage name or a crash.

**What's needed:** At row-creation time, store the contact name string as `user_data` on the row object via `lv_obj_set_user_data(row, name_ptr)`. In the event handler, retrieve it with `lv_obj_get_user_data(target)`. This eliminates all positional coupling.

---

## Onboarding

### ESP.restart() without flash write completion
**FIXED:** A `delay(100)` was added between the SPIFFS write and the `ESP.restart()` call, ensuring writes complete before reboot.

---

## GPS

### No NMEA checksum validation
**FIXED in PR #101:** NMEA checksum validation is implemented in `gps.cpp` with full XOR checksum verification. Corrupted sentences are discarded before parsing.

## Terminal

### Unbounded label accumulation
**FIXED:** Terminal output is now capped at `MAX_TERM_LINES = 64`. The oldest label is deleted when the cap is reached, preventing unbounded heap consumption.

---

## SPI / Display

### Display and SD card share the same SPI host
**FIXED in PR #108 + follow-up:** Moved the display from SPI3_HOST to SPI2_HOST, so all three bus-sharing devices (display, LoRa, SD) use the same SPI2_HOST with different CS lines. No cross-host pin contention.

---

## Code Quality / Maintenance

### Screen dispatch switch duplicated in `navigate_to()` and `go_back()` (`navigation.cpp`)
The 14+-case switch that maps `Screen` enum values to their `*_show()` functions appears identically in both `navigate_to()` and `go_back()`. Any new screen type requires updating both copies.

**What's needed:** Extract the switch into a `static void dispatch_screen(Screen s)` helper function and call it from both `navigate_to()` and `go_back()`.

### `debug.h` declares symbols only implemented under `#if defined(SLOPOS_DEBUG)` (`debug.h`)
**FIXED:** `debug.h` already has `#else` inline stubs for non-debug builds (added in commit `afdedd3`). The `slopos::debug` namespace provides empty inline implementations that compile to zero instructions when `SLOPOS_DEBUG` is not defined.

### `makeEpoch()` uses non-thread-safe `setenv`/`tzset` for UTC conversion (`mesh_wrapper.cpp`)
`makeEpoch()` temporarily sets the process TZ to `"UTC0"` via `setenv()` + `tzset()`, calls `mktime()`, then restores the original TZ. `setenv` and `tzset` modify global state and are not thread-safe. Any concurrent code path reading localtime (GPS NMEA timestamp parsing, UI clock display, NTP sync) during this window silently receives UTC instead of the device's configured timezone.

**What's needed:** Replace with `timegm()` (available as a GNU extension in ESP32 newlib). `timegm()` interprets `struct tm` as UTC without touching TZ and is reentrant.
