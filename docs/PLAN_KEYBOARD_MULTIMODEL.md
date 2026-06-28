# Plan: Multi-Model Keyboard & Input Integration

**Status:** draft · **Date:** 2026-06-28 · **Author:** agent (Ben review)

## Goal

Make SigurdOS-tdeck keyboard/input work correctly across ALL T-Deck variants (T-Deck, T-Deck Plus, etc.) by integrating the best architectural choices from wadamesh while preserving SigurdOS's modular HAL, existing test coverage, and feature set.

---

## Problem Statement

SigurdOS-tdeck currently operates the T-Deck's ESP32-C3 keyboard in **raw matrix mode** — it reads 5-byte column bitmasks and decodes key presses from a hardcoded `RAW_KEYS[5][7]` table. This table is specific to ONE T-Deck model's physical key matrix. On a T-Deck Plus or other variant with a different matrix layout, keys produce wrong characters.

Additionally, SigurdOS has no concept of keyboard layouts — it's US QWERTY with a small extended-Latin accent table bolted on via Alt/Mic modifiers.

Wadamesh avoids both problems by:
1. Using the C3 keyboard's **ASCII key mode** — the C3 resolves the matrix itself, emitting pre-decoded characters independent of the physical layout
2. Having a 12-language phonetic mapping layer that transparently remaps keys for non-Latin scripts

---

## What We Keep from SigurdOS (Non-Negotiable)

These are the strengths that must survive the integration:

| Item | Why we keep it |
|------|---------------|
| **Modular HAL architecture** (`hal/keyboard.cpp`, `hal/touch.cpp`, `hal/trackball.cpp` as separate modules) | Wadamesh has everything in one 16,300-line `UITask.cpp`. SigurdOS's separation is cleaner and testable |
| **Native test suite** (`test/test_keyboard/`, `test/test_touch/`, `test/test_input_contract/`) | Wadamesh has no visible tests. SigurdOS's test culture is a competitive advantage |
| **Raw matrix as fallback** | The Mic key and unlabeled Alt key are only accessible in raw mode. Keep it as a secondary path |
| **Warm-handoff retry** (3 attempts × 100ms for Launcher reboot) | Wadamesh lacks this; SigurdOS's Launcher-compatibility testing found it necessary |
| **SIGURDOS_KEY_CHAR_PICKER_BASE** (0x01000000) | On-screen character picker overlay for accented variants is a good UI pattern |
| **Existing LVGL indev setup** (3 indevs: pointer, keypad, encoder with shared group) | Works well, don't break it |
| **Test injection hooks** (`sigurdos_keyboard_inject()`, `sigurdos_keyboard_inject_codepoint()`) | Enables remote test mode and automated UI testing |
| **Telemetry** (`SIGURDOS_TELEMETRY` key/touch event reporting) | Unique feature, preserve it |

---

## What We Adopt from Wadamesh

### Phase 1: Keyboard I2C Mode (low risk, high impact)

**Switch primary keyboard mode from raw matrix to key mode.**

```
Current:  C3 → raw matrix (5 bytes) → process_raw_matrix() → enqueue_key()
Proposed: C3 → key mode (1 byte)     → process_keymode_byte() → keyboardLayoutMapHwKey() → enqueue_key()
```

**Implementation:**
1. In `sigurdos_keyboard_init()`, send `CMD_MODE_KEY` instead of `CMD_MODE_RAW` as the final mode
2. Add `tdeck_keyboard_poll_keymode()` that reads 1 byte from `Wire.requestFrom(0x55, 1)`
3. Add the `KeyboardLayouts` system (see Phase 2) that maps ASCII chars through the active layout
4. Keep raw mode as a periodic check: every ~1s, briefly switch to raw, sample the Mic/Alt keys, switch back to key mode. Or simply poll raw mode on key-up to catch modifier-only presses

**Risk:** None. The C3's key mode is its default/boot mode and is proven stable in wadamesh.

**Validation:**
- Existing `test_keyboard` tests must continue passing
- New test: inject key-mode bytes and verify correct codepoints arrive through the layout mapper
- Flash to real T-Deck: every physical key must produce the correct character
- Flash to T-Deck Plus (if available): verify all keys work

---

### Phase 2: Multi-Language Keyboard Layout System (medium risk)
[**based on wadamesh `src/ui-touch/KeyboardLayouts.cpp`**]

**Add a layout registry similar to wadamesh's but adapted to SigurdOS's architecture.**

**New files:**
```
src/hal/keyboard_layouts.h     — LayoutId enum, API declarations
src/hal/keyboard_layouts.cpp   — Layout data tables, keyboardLayoutMapHwKey()
```

**Design:**
```cpp
enum class KeyboardLayoutId : uint8_t {
    EN = 0,   // US QWERTY (pass-through)
    BG = 1,   // Bulgarian (phonetic Cyrillic)
    RU = 2,   // Russian (phonetic Cyrillic)
    // ... add as needed
    Count
};

// Called from LVGL kb indev callback for each key-mode byte:
// Returns the UTF-8 string to insert, or nullptr to pass key through unchanged.
const char* keyboardLayoutMapHwKey(KeyboardLayoutId id, int key, bool shifted);
```

**Data tables** — copy wadamesh's `hw_*_lower[26]`, `hw_*_upper[26]`, `hw_*_digits[10]`, `hw_*_digits_shift[10]` arrays directly. They're pure data — no logic changes needed.

**Persistence:** Store active layout in `sigurdos::prefs` (NVS), loaded at boot.

**Cycle mechanism:** Double-tap spacebar cycles through enabled layouts (wadamesh pattern). Single space = normal space.

**Risk:** Low. The layout tables are pure data. The mapping function is a simple lookup.

**Validation:**
- Native test: feed QWERTY 'a' through BG layout, expect "а"
- Native test: feed shifted 'A' through RU layout, expect "А"
- Flash to T-Deck: cycle through layouts, type in each, verify correct characters
- GPS coordinates, channel names, WiFi passwords must still accept ASCII (layout only remaps alpha chars)

---

### Phase 3: I2C Robustness Improvements (low risk)

**Adopt wadamesh's I2C bus diagnostics and recovery.**

**Changes to `hal/touch.cpp` and `hal/keyboard.cpp`:**
1. **Targeted I2C probes only** — probe 0x55 and 0x5D/0x14 only, never full-bus scan. Wadamesh header comment: "A full 1..0x77 sweep risked dozens of slow timeouts on a floating or wedged bus"
2. **I2C timeout bound** — call `Wire.setTimeOut(20)` after `Wire.begin()`. The ESP32 Arduino default (~1s) can stall everything on a non-responding device
3. **One-shot init probe** — cache the init result. The UI retries `sigurdos_touch_init()` / `sigurdos_keyboard_init()` in a loop until success, but probing the bus every frame crawls the device. Wadamesh pattern:
   ```cpp
   static bool s_attempted = false;
   if (s_attempted) return s_init_ok;
   s_attempted = true;
   ```
4. **I2C bus recovery** — if a peripheral is wedged mid-byte holding SDA low, clock SCL up to 9 times to let it finish. Add to `sigurdos_touch_init()` before the probe
5. **Bus idle-level read** — sample SDA/SCL as INPUT (no pull) before init. If LOW, log a warning — the peripheral power rail may not be reaching the devices

**Risk:** Very low. These are defensive additions, not functional changes.

**Validation:**
- Boot normally: no regression
- Boot from Launcher: I2C devices still detected
- Simulate bus hang in test: recovery path exercised

---

### Phase 4: Keyboard-Driven UI Navigation (optional, medium risk)
[**based on wadamesh's ESDFX nav system**]

**Add opt-in keyboard navigation so the physical keyboard can drive the UI without touching the screen or trackball.**

**Implementation:**
- E = up, X = down, S = left, F = right, D = select, Q = back
- Tab hotkeys (E/R/T/U/I → jump to Home/Chats/Contacts/Settings/etc.)
- Only active when no textarea is focused
- Toggle on/off in Settings → Keyboard → "Keyboard navigation"
- Focus-visible highlight (colored border on focused widget) — shown only while actively navigating

**Rationale:** Trackball can fail or be uncomfortable for some users. Keyboard nav is a pure software feature that adds zero hardware dependency.

**Risk:** Medium. Adding an LVGL group-based nav system touches the indev setup and may conflict with existing focus handling.

**Defer to post-Phase-3:** Don't block the keyboard mode fix on this. Ship Phase 1-3 first.

---

### What We Do NOT Adopt from Wadamesh

| Item | Reason to skip |
|------|---------------|
| **Monolithic UITask.cpp** | 16,300 lines in one file is unmaintainable. SigurdOS's modular screen-per-file structure is better |
| **FreeRTOS touch task on core 0** | Adds complexity. SigurdOS's single-threaded main-loop polling works. Defer until we have evidence of touch latency issues |
| **On-screen keyboard** | T-Deck has a physical keyboard. Wadamesh's on-screen kb is only shown on touch-only boards (Heltec V4, Tanmatsu). Not relevant for T-Deck |
| **Trackball soft cursor** | Encoder indev is simpler and sufficient. A mouse cursor on a 320×240 screen is limited utility |
| **C3 keyboard firmware modification** | Wadamesh uses the C3 as-is (key mode). We should too |

---

## Implementation Order

```
Phase 1: Keyboard I2C Mode        ← DO FIRST (fixes multi-model, low risk)
  ├── Switch to CMD_MODE_KEY
  ├── Add key-mode poll function
  └── Keep raw matrix as modifier-sampling fallback

Phase 2: Keyboard Layout System   ← DO SECOND (unblocks international users)
  ├── Add keyboard_layouts.h/cpp
  ├── Copy wadamesh layout tables
  ├── Integrate with LVGL indev callback
  └── Persist active layout in NVS prefs

Phase 3: I2C Robustness           ← DO THIRD (prevents subtle bus hangs)
  ├── Targeted probes only
  ├── I2C timeout bounding
  ├── One-shot init caching
  └── Bus recovery clocking

Phase 4: Keyboard Nav (optional)  ← DEFER (nice-to-have, higher risk)
```

---

## Affected Files

| File | Change |
|------|--------|
| `src/hal/keyboard.cpp` | Switch to key mode, add key-mode poll, keep raw fallback |
| `src/hal/keyboard.h` | Add layout-related declarations |
| `src/hal/keyboard_layouts.h` | **NEW** — LayoutId enum, mapping API |
| `src/hal/keyboard_layouts.cpp` | **NEW** — layout data tables, `keyboardLayoutMapHwKey()` |
| `src/hal/touch.cpp` | I2C robustness: targeted probes, timeout, one-shot init, bus recovery |
| `src/hal/display.cpp` | Update `lvgl_kb_cb` to route through layout mapper |
| `src/hal/tdeck_pins.h` | Add I2C bus pin aliases if missing |
| `src/hal/prefs.h` / `prefs.cpp` | Add `kbd_layout` pref field |
| `test/test_keyboard/test_keyboard.cpp` | Add layout-mapping tests, key-mode byte tests |
| `test/test_input_contract/test_input_contract.cpp` | Verify key→codepoint contract through layouts |
| `docs/KNOWN_ISSUES.md` | Add multi-model keyboard issue (to close after Phase 1) |
| `docs/CHAT_SCREEN.md` | Update input routing docs for layout system |

---

## Testing Strategy

### Per Phase

**Phase 1:**
- [ ] All existing `test_keyboard` tests pass unchanged
- [ ] New test: `test_keyboard_keymode_bytes` — inject key-mode ASCII bytes, verify correct codepoints
- [ ] New test: `test_keyboard_raw_fallback` — verify raw mode still works when key mode returns <1 byte
- [ ] Hardware: flash to T-Deck, type every key, verify correct output
- [ ] Hardware: flash to T-Deck Plus (if available), verify keys

**Phase 2:**
- [ ] New test: `test_keyboard_layout_en` — EN layout is pass-through
- [ ] New test: `test_keyboard_layout_bg` — 'a' → "а", 'A' → "А"
- [ ] New test: `test_keyboard_layout_cycle` — double-space cycles correctly
- [ ] Hardware: cycle layouts, type in each, verify on screen

**Phase 3:**
- [ ] Boot normally: no regression
- [ ] Boot from Launcher: I2C devices still found
- [ ] New test: `test_touch_oneshot_init` — second call is cache hit

### Regression Gates

Before merging any phase:
```bash
pio test -e native_test -v          # all native tests must pass
pio run -e SigurdOS_TDeck           # firmware must compile
# Flash to real T-Deck and verify:
#   - Boot OK
#   - Keyboard types correctly
#   - Touch works
#   - Trackball works
#   - Launcher compatibility maintained
```

---

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Key mode drops the Mic/Alt unlabeled keys that raw mode exposes | Keep raw mode as a periodic sampler. Sample raw every ~1s or on key-up to detect modifier-only presses |
| Layout tables increase flash usage | The 12-language tables are ~4KB total (11 × ~350 bytes per language). Acceptable on T-Deck's 16MB flash |
| C3 keyboard firmware may behave differently on different T-Deck variants in key mode | Key mode is the C3's default — it's the mode it boots into. Should be consistent across all LilyGo T-Deck C3 firmware versions |
| Layout system adds lookup overhead per keystroke | Table lookup is O(1) array access. Negligible |
| Breaking Launcher compatibility | Test Launcher → SigurdOS warm boot after each phase |

---

## References

- Wadamesh keyboard driver: `src/helpers/input/TDeckKeyboard.cpp` (64 lines, key mode)
- Wadamesh layout system: `src/ui-touch/KeyboardLayouts.cpp` (775 lines, 12 languages)
- Wadamesh touch driver: `src/helpers/input/TDeckTouch.cpp` (383 lines, I2C robustness)
- Wadamesh UI integration: `src/ui-touch/UITask.cpp` (~25000-25210, `handleHwKey`)
- SigurdOS keyboard HAL: `src/hal/keyboard.cpp` (583 lines, raw matrix mode)
- SigurdOS touch HAL: `src/hal/touch.cpp` (309 lines)
- SigurdOS display/input integration: `src/hal/display.cpp` (1010 lines, indev callbacks)
- SigurdOS pin definitions: `src/hal/tdeck_pins.h`
