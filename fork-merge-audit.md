# Fork Merge Audit: n30nex/SigurdOS-tdeck-fork → hermes-gadget/SigurdOS-tdeck

**Date:** 2026-07-04  
**Fork:** https://github.com/n30nex/SigurdOS-tdeck-fork (branch: `dev`)  
**Upstream:** https://github.com/hermes-gadget/SigurdOS-tdeck (branch: `dev`)  
**Common ancestor:** `e7e7b12` ("docs: add PR merge log for #757, #758, #759")

## Executive Summary

The fork has **16 non-merge commits** (18 total including 2 merges) on top of the common ancestor. Our upstream `dev` has **2 commits** ahead of the same ancestor. **No direct conflicts** — our changes (I2C clock fix, CI intelhex dep) are independent of all fork changes.

The fork adds: a **radio profile system** (country presets), **input/GPS/SD diagnostics**, an **RX-only validation mode**, **CI matrix builds**, and various UX refinements (onboarding date picker, build info dialog, quiet boot).

**Porting strategy:** Cherry-pick or re-implement changes in focused PRs by area. Do NOT merge the fork wholesale. Preserve all upstream behavior, especially keyboard key-mode (I2C ASCII mode not raw matrix), LVGL v9 screen lifecycle, MeshCore compatibility, and existing settings/prefs format.

---

## 1. Changes Worth Porting

### 1.1 Radio Profile System ⭐ HIGH
**Files:** `src/hal/radio_profiles.cpp` (new), `src/hal/radio_profiles.h` (new), `test/test_radio_profiles/` (new), `src/hal/prefs.cpp`, `src/hal/prefs.h`, `src/ui/onboarding_screen.cpp`, `src/ui/screens/screen_radio_setup.cpp`, `src/ui/screens/screen_settings_radio.cpp`, `platformio.ini`

**What it does:**
- Predefined RF profiles for US, Canada, UK, EU (4 profiles, 2 with same 915MHz params)
- Profile selector replaces hardcoded frequency presets in onboarding and radio setup
- `radio_profile` field in NodePrefs (NVS-persisted, 16-char string)
- `radio_profile_match()` and `radio_profile_apply()` utilities
- Region-default map views based on radio profile

**Porting plan:** Port as a single focused PR. Keep upstream's `NodePrefs` struct fully backward-compatible (unset profile == empty string → treated as custom). The radio_profiles module is self-contained and test-covered.

**Risks:** Must ensure prefs backward compatibility — existing nodes with no `radio_profile` key must still work. The `custom` fallback path must be identical to current manual RF behavior.

### 1.2 Input Diagnostics ⭐ HIGH
**Files:** `src/hal/keyboard.cpp/h`, `src/hal/touch.cpp/h`, `src/hal/trackball.cpp/h`, `src/test/test_controller.cpp`, `src/ui/screens/screen_settings_system.cpp`

**What it does:**
- Per-input diagnostic structs (`SigurdOSKeyboardDiag`, `SigurdOSTouchDiag`, `SigurdOSTrackballDiag`)
- `get_diag()` snapshot functions (non-destructive reads)
- "Input Self-Test" dialog in System Settings
- Test controller commands: `keydiag`, `inputdiag`, `gpsdiag`, `removechannel`

**Porting plan:** Port diagnostic structs and getters. Port the self-test dialog and test controller commands. This is additive — no behavior changes to existing input paths, just observation instrumentation.

**Risks:** Low. Diagnostic state is purely read-only. Must not change keyboard key-mode behavior (upstream uses I2C key mode, fork shouldn't assume raw matrix).

### 1.3 SD Card Improvements ⭐ MEDIUM
**Files:** `src/hal/sdcard.cpp`, `src/hal/sdcard.h`, `src/ui/screens/screen_settings_system.cpp`

**What it does:**
- Retry/backoff logic (`SDCARD_INIT_MAX_ATTEMPTS=3`, `SDCARD_LAZY_RETRY_MAX_ATTEMPTS=3`)
- Backoff: 0ms → 120ms → 300ms → 600ms
- Diagnostic struct `SigurdosSdMountDiagnostic` with attempt count, last source, last error
- SD diagnostic dialog in System Settings

**Porting plan:** Port the retry/backoff logic carefully. Our upstream SD init is "single attempt at boot + lazy retry on demand" — the fork adds bounded retries within init itself. This is a net improvement. Port diagnostics.

**Risks:** Must verify boot time isn't significantly impacted (3 retries with 120ms/300ms backoff = ~420ms max extra). Must handle edge case where SD card is genuinely absent (don't retry forever).

### 1.4 GPS Diagnostics ⭐ MEDIUM
**Files:** `src/ui/screens/screen_settings_gps.cpp`

**What it does:**
- GPS diagnostic dialog showing: fix state, satellite count, SNR, UART stats (baud, chars, lines), NMEA sentence counts (GGA, RMC, GSV, GSA), checksum failures, baud switches, position, UTC time
- GPS status row in Settings ("GPS: Fix acquired" / "GPS: No fix")
- Diagnostic assessment string for quick triage

**Porting plan:** Port the diagnostic dialog and status row. This is additive — no changes to GPS parser behavior.

**Risks:** Low. Requires GPS HAL to expose all the counters — verify they exist in our upstream.

### 1.5 Build Info Enhancement ⭐ MEDIUM
**Files:** `src/diagnostics/build_info.h`, `src/diagnostics/build_info.cpp`, `scripts/build_metadata.py`, `src/ui/screens/screen_settings_system.cpp`

**What it does:**
- Extended `BuildInfo` struct with: `build_source`, `actions_run_id`, `actions_run_attempt`, `actions_ref`, `actions_run_url`
- `build_metadata.py` captures GitHub Actions env vars
- "Build Info" dialog in System Settings

**Porting plan:** Port the extended struct fields and build_metadata.py changes. The build info dialog is a nice UX addition. Our existing `BuildInfo` struct has fewer fields — this is a superset.

**Risks:** Build metadata script changes are backward-compatible (extra defines don't break anything). Must ensure local builds still work (env vars absent → sane defaults).

### 1.6 RX-Only Mode ⭐ MEDIUM
**Files:** `src/mesh/mesh_wrapper.cpp`, `src/mesh/companion_adapter.inc`, `platformio.ini`

**What it does:**
- `SIGURDOS_REMOTE_TEST_RX_ONLY` compile-time define
- Guards all radio TX paths: `sendRequest`, `sendRequestWithData`, `sendRoomMsgFetchRequest`, `sendRoomMessage`, `requestStatus`, `requestTelemetry`, companion BLE send paths
- New platformio envs: `SigurdOS_TDeck_remote_test_radio_usca`, `SigurdOS_TDeck_remote_test_radio_usca_rxonly`

**Porting plan:** Port the RX-only gating and platformio envs. This is useful for validation testing and passive monitoring.

**Risks:** Must ensure ALL TX paths are gated (check for any new TX paths added since fork diverged). The `radioTxAllowed()` function must be exported for UI to show "RX-only" status.

### 1.7 CI Improvements ⭐ LOW-MEDIUM
**Files:** `.github/workflows/build-validation-matrix.yml` (new), `.github/workflows/pr-ci.yml`

**What it does:**
- New `build-validation-matrix.yml` workflow with matrix builds across all envs
- PR CI builds for US/CA and RX-only envs

**Porting plan:** Port the CI changes. The build matrix workflow is useful for pre-release validation.

**Risks:** CI workflow must work with our GitHub repo's secrets/variables. The wifi-gated builds check for `vars.GPS_WIFI_SSID` and `vars.GPS_WIFI_HOST` — these must exist in our repo.

### 1.8 Documentation ⭐ LOW
**Files:** `docs/FIELD_VALIDATION.md` (new), `docs/BLE_COMPANION_VALIDATION.md` (new), `docs/WIFI_OTA_VALIDATION.md` (new), `docs/RF_INTEROP_TEST_PLAN.md` (new), `docs/WINDOWS_COM8_SERIAL.md` (new), various doc updates

**What it does:** Validation runbooks, RF interop test plans, Windows COM8 serial notes.

**Porting plan:** Port the validation docs. Skip doc changes that alter feature descriptions in ways that don't match our upstream behavior.

**Risks:** Must verify docs reference correct filenames/paths (our upstream may differ).

### 1.9 Onboarding UX Refinements ⭐ MEDIUM
**Files:** `src/ui/onboarding_screen.cpp`, `src/ui/onboarding_screen.h`

**What it does:**
- Date/time with +/− button rows instead of free-text textareas
- `onboarding_clamp_day()`, `onboarding_wrap_range()` helpers
- Radio profile picker in Step 3 (replaces hardcoded frequency buttons)
- Group navigation for trackball/keyboard usability
- `load_current_datetime_once()` for consistent time display

**Porting plan:** Port the date/time UX improvements. The +/− button approach is less error-prone than free-text date entry. Port radio profile picker.

**Risks:** Must preserve upstream onboarding flow (3 steps). Must add group navigation for all focusable widgets. Must NOT break keyboard key-mode input handling.

### 1.10 Radio Setup Screen Refinements ⭐ MEDIUM
**Files:** `src/ui/screens/screen_radio_setup.cpp`

**What it does:**
- Profile picker with custom RF option
- Scrollable container for long content
- CR adjustment added (± buttons, was missing before)
- Simplified button creation, consistent +/- pattern
- `applyRadioParams()` call on apply

**Porting plan:** Port the profile picker and CR adjustment. The scrollable container is a good UX improvement.

**Risks:** The fork removed the textarea-based custom RF entry method. Our upstream has textareas for custom values. We should KEEP our textarea approach (more precise) while ADDING the profile picker as an alternative. The two can coexist: profile picker as quick preset, textareas as advanced custom.

### 1.11 Map Renderer: Region-Default Views ⭐ LOW
**Files:** `src/app/map_renderer.cpp`, `src/app/map_renderer.h`

**What it does:**
- Default map center/zoom based on radio profile (US center vs Canada center)
- Changed default from London to US geographic center

**Porting plan:** Port the profile-based view selection. Keep our existing `SIGURDOS_MAP_DEFAULT_*` as fallback, add Canada profile entry.

**Risks:** Changing the default from London to US center is a behavioral change. We should keep the DEFAULT as-is (UK-centric since the project started there) but add the profile-based override. Users who select "USA 902-928" will get US center, "Canada 902-928" gets Canada center, others get UK center (or whatever the default is).

### 1.12 Logging Policy Consistency ⭐ LOW
**Files:** `src/hal/i2c_bus.cpp`, `src/hal/keyboard.cpp`, `src/hal/touch.cpp`

**What it does:** Replaces `Serial.printf()` / `Serial.println()` with `SIG_LOGD()` macro for debug-level logging.

**Porting plan:** Port the logging consistency changes. These are mechanical replacements.

**Risks:** None — purely cosmetic, improves consistency.

### 1.13 download_maps.py Alignment ⭐ LOW
**Files:** `scripts/download_maps.py`

**What it does:** Changes output from JPEG/maps/ to PNG/tiles/ to match firmware expectations. Removes Pillow dependency.

**Porting plan:** Port. Our firmware already expects PNG tiles at `/tiles/{z}/{x}/{y}.png` but the downloader script outputs JPEG to `maps/`. This is a bug fix.

**Risks:** Users with existing JPEG tile collections in `maps/` will need to re-download. Document this clearly.

---

## 2. Changes to Skip or Review Carefully

### 2.1 CORE_DEBUG_LEVEL=0 (Quiet Boot) ⚠️ SKIP
**Files:** `platformio.ini`  
**Reason:** Changes Arduino core debug level from 1 (warnings) to 0 (none). This suppresses boot diagnostics that can be helpful for debugging. Our upstream uses level 1 deliberately. The fork's justification ("clean release boots") is cosmetic. The debug envs already override this.  
**Decision:** SKIP. Keep `CORE_DEBUG_LEVEL=1`. If quiet boot is desired, it should be a separate env or runtime toggle.

### 2.2 Map Tile Format Change (PNG→?) ⚠️ REVIEW
**Files:** `scripts/download_maps.py`  
**Note:** The fork's download_maps.py change is actually a CORRECTION — the firmware expects PNG at `tiles/` but our downloader outputs JPEG to `maps/`. We should PORT this fix. See §1.13.

### 2.3 Default Map Center Change ⚠️ MODIFIED PORT
**Files:** `src/app/map_renderer.cpp`  
**Note:** The fork changed the hardcoded default from London (51.5/-0.12) to US center (39.8/-98.6). We should keep London as the fallback default but add profile-based overrides. See §1.11.

### 2.4 Onboarding Date Entry ⚠️ STRUCTURAL CHANGE
**Files:** `src/ui/onboarding_screen.cpp`  
**Note:** The fork replaced textarea-based date entry with +/− button rows. This is a significant UX change. While the button approach is arguably better, it's a behavioral change. We should port it but verify it works with keyboard and trackball navigation.

---

## 3. Changes Already in Upstream (No Port Needed)
- Our I2C clock fix (100kHz) is newer than fork — keep ours
- Our CI intelhex fix is newer than fork — keep ours

---

## 4. Potential Conflicts with Upstream Features
- **Keyboard key-mode (CMD 0x04):** The fork's keyboard diagnostics and polling must not assume raw matrix mode. Our upstream uses I2C key mode exclusively.
- **LVGL v9 screen lifecycle:** Fork dialogs use `lv_obj_del_async()` which is correct for LVGL v9. Verify no `lv_obj_del()` in event handlers.
- **Prefs backward compatibility:** New `radio_profile` field must default to empty string for existing NVS stores.
- **Screen navigation:** Fork adds group navigation to onboarding/radio-setup — must not break our existing trackball/keyboard navigation model.

---

## 5. Porting Plan

### PR 1: Radio Profile System (high priority)
- `src/hal/radio_profiles.cpp/h` (new)
- `test/test_radio_profiles/` (new)
- `src/hal/prefs.cpp/h` (add radio_profile field)
- `src/ui/onboarding_screen.cpp/h` (profile picker in step 3)
- `src/ui/screens/screen_radio_setup.cpp` (profile picker + CR adjust)
- `src/ui/screens/screen_settings_radio.cpp` (profile display)
- `platformio.ini` (add radio_profiles.cpp to build, new envs for USCA/RX-only)
- Tests, build verification

### PR 2: Diagnostics System
- `src/hal/keyboard.cpp/h` (diag struct)
- `src/hal/touch.cpp/h` (diag struct)
- `src/hal/trackball.cpp/h` (diag struct)
- `src/hal/sdcard.cpp/h` (diag struct + retry/backoff)
- `src/ui/screens/screen_settings_system.cpp` (input self-test, SD diag dialog)
- `src/ui/screens/screen_settings_gps.cpp` (GPS diag dialog + status row)
- `src/test/test_controller.cpp` (diag commands)
- Build info extension
- Tests

### PR 3: UX & Refinements
- Onboarding date/time UX
- Map region-default views
- download_maps.py alignment
- Logging consistency
- CI improvements

### PR 4 (optional): Documentation
- Validation runbooks
- Doc updates

---

## 6. Test Plan
1. **Build** all envs after each PR: `pio run -e SigurdOS_TDeck -e native_test`
2. **Native tests**: `pio test -e native_test -v` — all must pass
3. **New test**: `test_radio_profiles` must pass
4. **Flash to T-Deck** for hardware-facing changes (keyboard, touch, trackball, SD):
   - Verify keyboard still works in key-mode (CMD 0x04)
   - Verify touch/trackball navigation works
   - Verify SD card mount with retry
   - Verify onboarding flow with profile picker
5. **Regression check**: settings persistence (write → reboot → verify)
