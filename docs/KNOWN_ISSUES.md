# Known Issues

This document tracks currently open known issues, bugs, and missing features in SigurdOS T-Deck firmware. All historically tracked issues that have been resolved are maintained in the Git history — check merged PRs and commit logs for the full record.

---

## Launcher Compatibility

### Supported — bmorcelli/Launcher (v2.7.2+)

SigurdOS can now be installed as a Launcher app. See [`firmware/README.md`](../firmware/README.md) for the full installation guide and caveats.

**What's implemented (Phase 1/4):**
- ✅ Launcher install via SD, WebUI, or direct GitHub URL — use `SigurdOS-tdeck-launcher.bin`
- ✅ Runtime Launcher detection (probes for test-subtype app partition)
- ✅ Self-OTA gated with on-screen explanation when under Launcher
- ✅ Boot-time diagnostics when app-only install loses persistence
- ✅ Self-OTA disabled to prevent flash corruption of co-installed apps
- ✅ SPIFFS partition created for persistence (when using merged image)

**Phase 2a — Detection validated on hardware (2026-06-10):**
- ✅ Launcher detection tested via custom `test`-subtype partition
- ✅ `sigurdos_is_under_launcher()` returns `true` when Launcher partition exists
- ✅ Boot env diagnostic confirms `"bmorcelli/Launcher"` vs `"standalone"`
- ✅ Launcher installed on T-Deck (awaiting physical button press to proceed to handoff test)

**Phase 3 / C6 — Keyboard warm-handoff hardening ✅ merged with #573 follow-up:**
- ✅ Retry loop: keyboard init now retries 3× with 100ms delay instead of single-NACK-abort
- ✅ Mode reset: sends `CMD_MODE_KEY` (0x04) before each probe to reset C3 to known state
- ✅ 2 new native tests covering transient-NACK recovery and exhaustion
- ✅ 748/748 native tests pass, release build clean

**Remaining gaps (Phase 2b/5/6):**
- 🔜 Phase 2b: Actual Launcher boot handoff (T4/T9) — requires physical SD card or WebUI interaction on T-Deck
- ⏳ Phase 5: Full regression matrix (T1–T14) — standalone rows (T1–T3) pass, Launcher rows (T4–T13) need physical hardware
- ❌ Not yet listed in LauncherHub catalog (requires maintainer coordination)

---

## How to Help

Pick any item from the list above and open a PR against the `dev` branch. See [`CONTRIBUTING.md`](./CONTRIBUTING.md) for the full contribution workflow.

---

## Potential Bugs (from 2026-06-21 audit)

These code defects could cause crashes or corruption but aren't yet user-visible. See [`AUDIT-2026-06-21.md`](./AUDIT-2026-06-21.md) for the full report.

### Latent div-by-zero in battery percentage lookup

**File:Line:** `src/hal/battery.h:49-53`  
**Severity:** MEDIUM  
**Status:** FIXED (guard added)  
If two consecutive entries in `mv_table` have the same value, `range_mv` becomes 0 causing division-by-zero. A guard was added: `if (range_mv == 0) return pct_table[i];`.

### I2C speed compromise at 200kHz

**File:Line:** `src/hal/touch.cpp:112`, `src/hal/keyboard.cpp:361`  
**Severity:** MEDIUM  
**Status:** UNFIXED  
Both touch (GT911) and keyboard MCU (ESP32-C3) support 400kHz I2C. Currently capped at 200kHz as a "compromise." Consider trying 400kHz first with fallback, and move `setClock()` to a single shared init point (currently called in 3 places).

### Garbage GPS data can stall Serial1.readBytesUntil

**File:Line:** `src/hal/gps.cpp:211`  
**Severity:** MEDIUM  
**Status:** UNFIXED  
`Serial1.readBytesUntil('\n', ...)` blocks until newline or buffer fill. If GPS sends garbage without newlines, the poll task stalls for the full buffer wait. Consider `Serial1.setTimeout()` or character-by-character read.

### Undocumented firmware binaries

**File:Line:** `firmware/sigurdos-tdeck.bin`, `firmware/sigurdos-tdeck-merged.bin`  
**Severity:** LOW  
**Status:** UNFIXED  
These files exist on disk but are not documented in `firmware/README.md`. The stale `firmware/firmware-merged.bin` (pre-rename) has been removed.

### Auto-off dialog static pointer fragility

**File:Line:** `src/ui/screens/screen_settings_display.cpp:285-362`  
**Severity:** LOW  
**Status:** UNFIXED (correct but fragile)  
The `static lv_obj_t* selected` pointer in `auto_off_dialog()` is correctly nulled by `LV_EVENT_DELETE`, but a per-dialog context struct would be more robust against future UI changes.
