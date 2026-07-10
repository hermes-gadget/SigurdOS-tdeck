# SigurdOS T-Deck Repository Audit

**Audit date:** 2026-07-10
**Repository state:** branch `dev` @ `89ad92a`, firmware version `beta-0.1.43-RC5`
**Auditor:** automated static + build analysis (no on-device flashing performed)
**Scope:** entire repository — firmware source, HAL, mesh/protocol integration, UI, storage, transports, build system, CI, tests, scripts, docs.

---

## 1. Executive Summary

SigurdOS-tdeck is a mature, heavily-reviewed LVGL firmware for the LilyGo T-Deck that speaks the MeshCore protocol. The codebase is unusually disciplined for an embedded hobby project: defensive null checks, bounded string copies, versioned on-disk formats with magic numbers, an atomic temp+rename message store, TLS certificate pinning for OTA, per-session CSRF tokens on the WiFi OTA endpoint, a hardware I²C bus-recovery routine, and a genuinely large native test suite (835 test cases across 60 modules, all passing). Roughly 50 prior issues have been closed, and two recently-tracked bugs (#686 chat name-stride corruption, #687 unauthenticated OTA) are **confirmed fixed on `dev`**.

**Overall health: good, with a small number of real defects that warrant fixing before the next release.**

Most serious risks identified:

1. **`BUG-001` — GPS→epoch conversion is arithmetically wrong** (`hal/gps.cpp`). The day-count formula double-subtracts its epoch offset, producing a negative day count that wraps through `uint32_t` and a second overflow in the `×86400` multiply. On the first GPS fix the device calls `settimeofday()` with a garbage value, corrupting the system clock that the entire mesh layer uses for message timestamps, ACK matching, and advert freshness. The correct algorithm (`makeEpoch()`) already exists elsewhere in the same codebase, so this is a localized regression with an easy fix. **Confirmed numerically.**

2. **`BUG-002` — dangling `g_wifi_icon` use-after-free** (`ui/screens_common.cpp` + `ui/chat_screen.cpp`). The 1 Hz WiFi-status refresh writes to a global widget pointer that the chat screen fails to clear on entry, so opening a DM from Contacts leaves the pointer aimed at a freed LVGL object. A reachable, common navigation path leads to a heap use-after-free.

3. **`HW-001` — deep-sleep wake configured on a non-RTC GPIO** (`hal/tdeck_board.h`). Wake-on-LoRa-packet and the critical-battery deep-sleep both program `ext1` wake on GPIO45, which is not an RTC-capable pin on the ESP32-S3. The result is that wake-on-packet silently never works, and the low-battery deep-sleep has **no wake source at all** — recovery requires a physical power cycle.

Major architectural concerns: heavy reliance on file-scope global state across UI/mesh/HAL (mitigated by disciplined `LV_EVENT_DELETE` cleanup, but the `g_wifi_icon` bug shows the pattern is fragile); the SPIFFS message store performs an O(n) storm of file-open operations on every incoming message (`PERF-001`); and the display flushes the full 320×240 framebuffer synchronously on every redraw (`PERF-002`).

Test coverage is broad on pure logic (protocol parsing, stores, validation, layout math) but has **no coverage of the two most impactful bugs found** — GPS time-sync arithmetic and screen-transition widget lifecycle — because both live in `#if ESP32_PLATFORM`/hardware paths excluded from native tests.

**Confidence level:** High for static findings; the two hardware-path findings (`HW-001`, and the crash manifestation of `BUG-002`) are marked *Requires hardware validation* for their runtime symptoms, though the underlying code defects are confirmed by inspection.

**Limitations:** No device was flashed or connected (per audit constraints). MeshCore submodule internals were consulted where SigurdOS calls into them but were not themselves audited as project code.

---

## 2. Audit Methodology

### Repository areas inspected
- **Boot / lifecycle:** `main.cpp`, `hal/tdeck_board.h`, `hal/storage.cpp`, `hal/prefs.cpp`
- **HAL:** `display.cpp`, `touch.cpp`, `keyboard.cpp`, `trackball.cpp`, `gps.cpp`, `battery.cpp`, `sdcard.cpp`, `spi_shared.cpp`, `i2c_bus.cpp/.h`, `buzzer.cpp`, `lv_pool.cpp`, `wifi_ota.cpp`, `github_ota.cpp`, `github_ota_plan.cpp`, `launcher_env.cpp`, `radio_profiles.cpp`, `tdeck_pins.h`
- **Mesh/protocol:** `sigurd_mesh_v2.h/.cpp`, `mesh_wrapper.cpp`, `persistence_store.cpp`, `contact_store.cpp`, `message_store.cpp`, `regions.cpp`, `companion_bridge.cpp/.h`, `observed_ble_interface.cpp`
- **UI:** `navigation.cpp`, `screens_common.cpp`, `chat_screen.cpp` (targeted), `screen_terminal.cpp`, `screen_settings_system.cpp` (targeted), `home_screen.cpp` (targeted)
- **App:** `map_renderer.cpp` (targeted), `lv_conf.h`
- **Diagnostics:** `telemetry_crash.cpp` (targeted)
- **Build/CI/scripts:** `platformio.ini`, `.github/workflows/*.yml`, `scripts/check_security_patches.py`, `scripts/smoke_build_matrix.py`, `scripts/build_metadata.py`, `scripts/merge_bin.py`
- **Docs:** `CLAUDE.md`/`AGENTS.md`, `docs/KNOWN_ISSUES.md`, `docs/MISSING_FEATURES.md`

### Commands run (see §17 for outputs)
- `pio test -e native_test` — full native suite
- `pio run -e SigurdOS_TDeck` — release firmware build (warm + clean)
- Warning collection via clean-rebuild grep
- Python cross-check of the GPS epoch arithmetic against ground truth
- `git ls-files` / `git status` for tracked-artifact hygiene
- Repo-wide `grep` for `TODO/FIXME/HACK`, `Serial.print`, buffer patterns

### Static-analysis / search methods
- Manual call-chain following (radio RX handlers → queue → UI; nav → screen show → PIN gate; append → messageExists → file I/O)
- Buffer/lifetime inspection against the project's own Code Audit Checklist in `CLAUDE.md`
- Cross-referencing memory notes from prior review sessions and verifying each claim against current code

### Areas not fully validated
- Runtime behavior on real hardware (no device connected).
- MeshCore submodule (`lib/meshcore/`) internal correctness — treated as a third-party dependency; only SigurdOS's *use* of it was audited.
- Full line-by-line read of `chat_screen.cpp` (2885 lines), `companion_bridge.cpp` (1293 lines, ~60% read), `map_renderer.cpp`, emoji/font generated data.

### Assumptions
- The attached bench unit is a standard 16 MB flash / 8 MB PSRAM T-Deck (per project memory).
- `native_test` mocks faithfully represent the hardware contracts they stand in for.

---

## 3. Repository Architecture

### Major components
```
main.cpp ── boot orchestration + super-loop
  ├─ hal/          board, display(LVGL+LovyanGFX), touch(GT911), keyboard(C3 I²C),
  │                trackball(GPIO poll), gps(NMEA), battery(ADC), sdcard(SPI),
  │                spi_shared(FSPI singleton), i2c_bus(shared bus + recovery),
  │                prefs(NVS), storage(SPIFFS), wifi_ota/github_ota, launcher_env
  ├─ mesh/         SigurdMeshV2 : BaseChatMesh (MeshCore) ── all radio/protocol
  │                mesh_wrapper (sigurdos::mesh::* facade the UI calls)
  │                contact_store / message_store / persistence_store / regions
  ├─ comms/        companion_bridge (BLE/USB phone-app protocol) + observed_ble
  ├─ ui/           navigation (Screen enum + stack), screens_common (make_screen_full,
  │                PIN gate), home/chat screens, screens/* (26 screens)
  ├─ app/          map_renderer (PNG tiles→PSRAM canvas), tile_cache, qr_show
  └─ diagnostics/  log, telemetry(+collectors/rings/protocol), debug, build_info
```

### Main execution flow
`setup()` runs a fixed boot sequence (below); `loop()` is a cooperative super-loop that services, in order: display/LVGL, buzzer, WiFi OTA web server, GitHub OTA downloader, WiFi STA maintenance, a 1 Hz WiFi icon refresh, GPS (interval-gated), mesh (`g_mesh->loop()` + companion bridge), then UI. A 30 s battery check can trigger low-battery deep sleep.

### Startup sequence (`main.cpp`)
Serial → board.begin (peripheral power, I²C, ADC) → battery/buzzer → **display init (before SPIFFS, so the splash shows during mounts)** → SPIFFS → load prefs + apply theme + brightness → **deferred input init (touch/keyboard/trackball)** → GPS (if enabled) → mesh init (identity, radio) → restore persisted chats → SD card → map → WiFi auto-connect. Display-init failures are tracked in `RTC_NOINIT_ATTR` across reboots with a 3-strike safe-mode.

### UI architecture
LVGL v9, 16-bit color, full-frame render mode into a 153.6 KB PSRAM buffer (with DRAM/partial and emergency fallbacks). Three input devices are registered as LVGL indevs: touch (POINTER), keyboard (KEYPAD, 10 ms read), trackball (ENCODER). Screens are `lv_obj_create(nullptr)` roots loaded via `lv_scr_load_anim(..., auto_del=true)`; global widget pointers are nulled in `LV_EVENT_DELETE` handlers. `make_screen_full()` builds the shared top/bottom bars for most screens; Home and Chat build custom bars.

### Hardware abstraction
Each peripheral is a thin C-style module. The SPI bus (display, LoRa, SD) is a single `SPIClass(FSPI)` singleton in `spi_shared.cpp`, re-`begin()`'d per SD mount to force a `periph_module_reset`. The I²C bus (touch + keyboard) is centralized in `i2c_bus.cpp` at 100 kHz with a 20 ms transaction timeout and a 9-clock bus-recovery routine run before `Wire.begin()`.

### MeshCore integration
`SigurdMeshV2` subclasses `BaseChatMesh` and overrides ~25 virtual handlers (message/channel/command/anon/signed receive, ACK processing, contact discovery/overwrite, request/response, trace, flood scoping). The UI never touches MeshCore directly — everything routes through the `sigurdos::mesh::*` facade in `mesh_wrapper.cpp`, which owns the radio objects, identity persistence, an incoming message ring buffer (`MAX_QUEUED=64`), and a packet log.

### Storage & configuration
- **NVS (`Preferences`):** `NodePrefs` (radio, display, GPS, BLE, OTA, region, PINs), channel table, repeater passwords.
- **SPIFFS:** identity (`/mesh_id`), contacts (`/contacts`, versioned+magic), companion messages (`/companion_msgs`, versioned+magic+atomic replace), regions (`/regions2`), OTA blobs.

### Transport architecture
LoRa SX1262 via RadioLib (`CustomSX1262Wrapper`); companion protocol over BLE (`SerialBLEInterface`, PIN-paired) or USB CDC; WiFi STA for GitHub OTA and WiFi AP for local web OTA.

### Major global state
`g_mesh`, `board`, radio objects, message/packet/ACK ring buffers (`mesh_wrapper.cpp`); `own_name`; UI globals `g_wifi_icon`, `s_back_btn`, per-screen widget pointers, `dyn_channels[16][37]` chat model; region map singleton; input queues in each HAL module.

### Important inter-subsystem dependencies
- UI clock/timestamps depend on `rtc_clock` → **corrupted by `BUG-001`**.
- `make_screen_full` sets `g_wifi_icon`; the 1 Hz loop dereferences it → **`BUG-002` when a custom-bar screen fails to clear it**.
- Radio RX handlers push into `mesh_wrapper` queues and, in parallel, the companion offline store → coupling point for `PERF-001`.
- SD, LoRa, display all share one SPI peripheral (documented fragility).

---

## 4. Critical Findings

No finding was assessed as unconditionally Critical (permanent data loss at rest, guaranteed brick, or trivially remote code execution). The two findings with the highest damage potential — `BUG-002` (use-after-free / memory corruption) and `HW-001` (unrecoverable deep sleep) — are documented in §5 as High severity because each requires a specific reachable condition and, for their worst symptoms, hardware validation. Either could be re-classified Critical if hardware testing shows a reliable crash/brick.

---

## 5. High-Severity Findings

### BUG-001 — GPS-to-Unix-epoch conversion is arithmetically wrong, corrupting the system clock

**Severity:** High
**Confidence:** Confirmed (numerically verified)
**Category:** BUG / correctness / timing / persistence-of-time
**Affected files:** `src/hal/gps.cpp:361-381` (`sigurdos_gps_loop`, first-fix time sync block)

**Description.** On the first valid GPS fix with `year >= 2020`, the driver computes days-since-epoch with:
```c
uint32_t days = (uint32_t)(365LL*y + y/4 - y/100 + y/400
             - (365LL*1970 + 1970/4 - 1970/100 + 1970/400)   // = 719527
             + (uint32_t)(30.6001*(m+1)) + d - 719469);        // second offset
```
It subtracts **both** the full civil-days term for 1970 (`719527`) **and** the constant `719469`. The correct Howard-Hinnant form subtracts the offset exactly once. The result is a large *negative* day count (~−698,761 for 2026-07-10), which wraps when cast to `uint32_t`, and then `epoch = days*86400UL` overflows a second time, yielding an arbitrary `uint32_t`. That value is passed straight to `settimeofday()`.

**Evidence.** Python cross-check against `datetime` ground truth for five dates (2020–2026) showed the formula produces `truth − 719405` uniformly (always negative):
```
2026-07-10  code=-698761  truth=20644  delta=-719405
2024-01-01  code=-699682  truth=19723  delta=-719405
2020-03-01  code=-701083  truth=18322  delta=-719405
```
The **correct** algorithm already exists in this repository: `mesh_wrapper.cpp:1451 makeEpoch()` uses the proper `−719468` single-offset Hinnant form and is used by the onboarding and manual time-set paths.

**Root cause.** Copy/derivation error: the `term(1970)` subtraction and the literal `−719469` epoch shift were both applied, double-counting the offset. `settimeofday` and the `days*86400` multiply are unsigned, so the negative intermediate silently wraps instead of failing.

**Trigger conditions.** GPS enabled, a valid `$G_RMC/$G_GGA` fix acquired with a date field parsing to `year >= 2020`, and `time_synced` not yet latched (once per boot). This is the normal, expected path whenever GPS gets a fix.

**User impact.** System clock jumps to a wrong time. Because `rtc_clock`/`ESP32RTCClock` reads the same OS clock, every mesh timestamp is affected: top-bar/chat clocks show wrong times; outgoing message timestamps (used for ACK correlation and shown to peers) are wrong; advert freshness and duplicate-suppression on *other* nodes may reject or mis-order our packets. Manual time set (onboarding/settings) is unaffected because it uses `makeEpoch()`.

**Risk.** Fixing changes the value written to the RTC — low regression risk, but any code that (accidentally) depended on the wrong monotonic-but-wrong clock would shift. None found.

**Recommended fix.** Delete the hand-rolled formula and call the existing correct helper: compute `epoch = sigurdos::mesh::makeEpoch(year, month, day, hour, minute) + seconds` (or inline the Hinnant algorithm with a single `−719468`). Guard against `settimeofday` receiving a value that implies a pre-2020 date.

**Suggested tests.** A native unit test that feeds `sigurdos_gps_loop` a canned `$GPRMC`/`$GPGGA` pair with a known date/time and asserts the computed epoch equals the `datetime`-derived truth (extract the epoch math into a testable free function). Regression test comparing GPS-path epoch to `makeEpoch()` for a table of dates.

**Suggested PR scope.** Single-file fix in `gps.cpp` plus one new test module `test/test_gps_time/`. No hardware required to validate the arithmetic; a bench GPS fix would confirm end-to-end.

**Related findings:** `TEST-001` (no coverage of this path).

---

### BUG-002 — Dangling `g_wifi_icon` pointer causes a use-after-free on a common navigation path

**Severity:** High
**Confidence:** High confidence (crash manifestation *Requires hardware validation*; the dangling dereference is confirmed by inspection)
**Category:** BUG / use-after-free / UI lifecycle
**Affected files:** `src/ui/screens_common.cpp:44,144-145,174-188` (`g_wifi_icon`, `update_wifi_status`, `screens_clear_wifi_icon`); `src/ui/chat_screen.cpp:2408-2422` (`chat_screen_show`); `src/main.cpp:206-212` (1 Hz caller); contrast `src/ui/home_screen.cpp:387-388`.

**Description.** `make_screen_full()` stores a pointer to the screen's bottom-bar WiFi label in the file-scope global `g_wifi_icon`. `update_wifi_status()` — invoked every second from the main loop, and again during each `make_screen_full` — dereferences `g_wifi_icon` with only a `if (!g_wifi_icon) return;` null check and **no `lv_obj_is_valid()` check**. When a screen built with `make_screen_full` is replaced, `lv_scr_load_anim(..., auto_del=true)` frees that screen and its WiFi label. `home_screen_show()` correctly calls both `screens_clear_back_btn()` and `screens_clear_wifi_icon()`; but `chat_screen_show()` (which builds a *custom* top bar via `create_top_bar()`) calls only `screens_clear_back_btn()` — it never nulls `g_wifi_icon`.

**Evidence.**
- `screens_common.cpp:174` — `void update_wifi_status(){ if(!g_wifi_icon) return; ... lv_label_set_text(g_wifi_icon,...); }` (no validity check).
- `home_screen.cpp:387-388` — clears both globals.
- `chat_screen.cpp:2410` — clears only `s_back_btn`; grep of the whole file shows no `screens_clear_wifi_icon` and no `make_screen_full` usage (custom bars at `create_top_bar`, line 1218).

**Reachable path.** Contacts screen (`screen_contacts.cpp` uses `make_screen_full` → sets `g_wifi_icon`) → tap a contact → `chat_screen_open_dm()` → `navigate_to(Screen::Chat)` deletes the Contacts screen (freeing its WiFi label) → Chat builds a custom bar and does **not** clear `g_wifi_icon` → within 1 s the loop's `update_wifi_status()` calls `lv_label_set_text()` on the freed object.

**Root cause.** Ownership of a global widget pointer is split between the setter (`make_screen_full`) and each screen's teardown, and the invariant "custom-bar screens must clear `g_wifi_icon`" is enforced by convention. Home follows it; Chat does not.

**Trigger conditions.** Navigate to Chat from any `make_screen_full` screen (Contacts→DM is the most common), then remain on Chat for ≥1 s.

**User impact.** Use-after-free: at best a benign write into recycled heap, at worst heap corruption and a crash/reboot while the user is reading a DM. LVGL object pools make the exact symptom timing-dependent, which is why runtime confirmation needs hardware.

**Risk.** The fix is small and low-risk; failing to fix leaves a latent memory-safety defect on a primary user flow.

**Recommended fix.** Two independent, complementary hardening steps: (1) have `chat_screen_show()` (and any other custom-bar screen) call `screens_clear_wifi_icon()`, matching `home_screen`; (2) make `update_wifi_status()` defensive: `if (!g_wifi_icon || !lv_obj_is_valid(g_wifi_icon)) { g_wifi_icon = nullptr; return; }`. Prefer doing both.

**Suggested tests.** A UI-lifecycle native test (using the LVGL mock) that simulates make_screen_full → screen delete → `update_wifi_status()` and asserts no access to a freed object / that the global is nulled. Add a screen-transition matrix test (Contacts→Chat→Home) asserting `g_wifi_icon` is either valid or null after each transition.

**Suggested PR scope.** `screens_common.cpp` + `chat_screen.cpp`; small. No hardware strictly required for the guard, but a bench Contacts→DM soak would confirm the crash is gone.

**Related findings:** `TEST-002`; architectural note `ARCH-001` (global widget pointers).

---

### HW-001 — Deep-sleep wake programmed on GPIO45, which is not RTC-capable on ESP32-S3

**Severity:** High
**Confidence:** Requires hardware validation (code defect confirmed by inspection; ESP32-S3 RTC-GPIO range is documented)
**Category:** HW / power management / reliability
**Affected files:** `src/hal/tdeck_board.h:75-84` (wake-reason detection), `:122-140` (`sleep()`); `src/main.cpp:189-198` (critical-battery `board.sleep(0)`); pin from `tdeck_pins.h:42` (`PIN_LORA_DIO1 = 45`).

**Description.** `sleep()` calls `rtc_gpio_set_direction()`, `rtc_gpio_pulldown_en()`, and `esp_sleep_enable_ext1_wakeup(1<<45, ANY_HIGH)` on `PIN_LORA_DIO1 = GPIO45`. On the ESP32-S3, RTC-capable GPIOs are **0–21 only**; GPIO45 is a normal digital pin with no RTC function. Every one of these RTC-GPIO/ext1 calls returns `ESP_ERR_INVALID_ARG`, and all return values are ignored. `rtc_gpio_hold_en(PIN_LORA_NSS=9)` *is* valid (9 ≤ 21).

**Evidence.** Pin map `tdeck_pins.h:42`; `SIGURDOS_LORA_DIO1_WAKE_MASK = 1<<45` (`tdeck_pins.h:49`); `sleep()` body. Prior bench work (project memory) added `rtc_gpio_is_valid_gpio` guards for this exact symptom, corroborating that the calls fail on hardware.

**Root cause.** The wake-on-DIO1 design was ported from a MeshCore board whose DIO1 lands on an RTC-capable pin; on the T-Deck, SX1262 DIO1 is on GPIO45, outside the RTC domain.

**Trigger conditions.** (a) Any attempt to wake from deep sleep on an incoming LoRa packet — never works. (b) `main.cpp` low-battery path calls `board.sleep(0)` (sleep *indefinitely*, no timer wake) — with ext1 also non-functional, **there is no configured wake source**, so the device can only be revived by a physical reset/power cycle.

**User impact.** "Wake on packet" is silently dead; the `BD_STARTUP_RX_PACKET` branch in `begin()` is unreachable. More seriously, a device that deep-sleeps on low battery cannot wake itself even after charging — the user must power-cycle. For a field mesh node this looks like a hard hang.

**Risk.** Any fix touches deep-sleep and battery paths; must be validated on hardware to ensure the radio still wakes the SoC via whatever pin/mechanism is actually RTC-routable, or that a timer wake is always armed.

**Recommended fix.** Guard all `rtc_gpio_*`/`ext1` calls with `rtc_gpio_is_valid_gpio()` and, when the wake pin is not RTC-capable, fall back to a timer wake (arm a non-zero timer even in the "sleep indefinitely" battery case, e.g. wake every N minutes to re-check charge). If wake-on-packet is a required feature, route it through a valid RTC GPIO or use light sleep with GPIO wake. Check every `esp_sleep_*`/`rtc_gpio_*` return code.

**Suggested tests.** Not natively testable; document as an on-device test: (1) deep sleep, transmit a packet from a peer, confirm wake behavior; (2) force low battery, confirm the device wakes after charging. Add a host-side unit test that `sleep(0)` always arms *some* wake source.

**Suggested PR scope.** `tdeck_board.h` + `main.cpp`; **requires physical T-Deck testing.**

**Related findings:** `RELI-002` (same root cause, reliability framing); the `mesh_wrapper.cpp:772-780` init comment that relies on this path.

---

## 6. Medium-Severity Findings

### BUG-003 — `onChannelMessageRecv` uses a stack buffer after it leaves scope

**Severity:** Medium
**Confidence:** Confirmed (undefined behavior by inspection; currently works by luck)
**Category:** BUG / undefined behavior
**Affected files:** `src/mesh/sigurd_mesh_v2.cpp:560-578`

**Description.** The sender name is copied into `char sender_buf[32]` declared **inside** an `if (colon)` block, and `sender_name` is pointed at it. After the block closes, `sender_name` (still pointing at the now-out-of-scope `sender_buf`) is passed to `mesh_v2_queue_push(sender_name, ...)`. Reading through a pointer to an object whose lifetime has ended is undefined behavior.

**Evidence.**
```cpp
const char* sender_name = text;
const char* colon = strstr(text, ": ");
if (colon && colon > text) {
    ...
    char sender_buf[32];
    memcpy(sender_buf, text, nlen);
    sender_buf[nlen] = '\0';
    sender_name = sender_buf;      // points into block-scoped buffer
    msg_text = colon + 2;
}                                  // sender_buf lifetime ends here
...
mesh_v2_queue_push(sender_name, chname, msg_text, ...);  // dangling read
```
`mesh_v2_queue_push` immediately `strncpy`s from `sender_name`, so the byte read happens right away.

**Root cause.** Buffer scoped too tightly; happens to work because no intervening call reuses the stack slot before `queue_push`.

**Trigger conditions.** Every inbound channel/group message whose text matches the MeshCore `"<name>: <text>"` wire format — i.e., essentially all of them.

**User impact.** Today: none observable (the stack slot survives). Under a different compiler, optimization level, or after adding a call between the block and `queue_push`, sender attribution on channel messages could become garbage.

**Recommended fix.** Hoist `char sender_buf[32]` to function scope (declare it before the `if`), or pass the sender substring by copying into a caller-owned buffer.

**Suggested tests.** Native test that injects a channel packet with `"Alice: hi"` and asserts the queued message's sender is exactly `Alice`. (Currently passing-by-luck, so a sanitizer build would catch the regression.)

**Suggested PR scope.** One-line scope change in `sigurd_mesh_v2.cpp`. Consider adding `-fsanitize=address` to a CI native-test variant to catch this class generally.

---

### BUG-004 — `processAck` returns the wrong contact, causing bogus return-path packets

**Severity:** Medium
**Confidence:** High confidence
**Category:** BUG / COMPAT / protocol
**Affected files:** `src/mesh/sigurd_mesh_v2.cpp:459-481` (`processAck`); interacts with `lib/meshcore/src/helpers/BaseChatMesh.cpp:347-365` (`onAckRecv`/`handleReturnPathRetry`).

**Description.** When an ACK matches a pending entry, `processAck` returns `&_contact_cache` populated with **contact index 0** (the first `getContactByIdx` that succeeds), not the contact the ACK actually came from — `PendingAck` stores only `dest_name`, never the pubkey. Upstream `BaseChatMesh::onAckRecv` uses this return value: if the ACK arrived flood-routed and the returned contact has a known direct path, it calls `handleReturnPathRetry(*from, ...)`, which builds a `createPathReturn` encrypted with **contact[0]'s** shared secret and sends it down **contact[0]'s** path.

**Evidence.** `processAck` loops pending ACKs, and on match does `for (j...) if (getContactByIdx(j, _contact_cache)) return &_contact_cache;` — returning the first contact unconditionally. The correct sender is identifiable: `PendingAck.dest_name` is known and could be resolved with `lookupContactByPubKey`/name lookup.

**Trigger conditions.** An ACK for one of our sent DMs arrives while still flood-routed (sender hasn't learned our reciprocal path), and contact[0] happens to have a direct path. Direct-routed ACKs are unaffected in practice.

**User impact.** A spurious `PATH_RETURN` packet encrypted for the wrong node is transmitted (wasted airtime; the intended sender never gets the reciprocal-path retry, so it may keep flooding). At a third node, an unexpected path-return could momentarily corrupt path state. Not a crash; a subtle protocol-efficiency and correctness divergence from upstream.

**Recommended fix.** Add a `uint8_t pub_key[…]` (or prefix) field to `PendingAck`, populate it in `addPendingAck` from the resolved contact, and in `processAck` return `lookupContactByPubKey(pending.pub_key, …)`. Return `nullptr` if the contact is gone.

**Suggested tests.** Native test with two contacts: register a pending ACK for contact B, inject an ACK CRC, assert `processAck` returns B (not A). Extend to assert no path-return is generated for a direct-routed ACK.

**Suggested PR scope.** `sigurd_mesh_v2.h` (`PendingAck` struct) + `sigurd_mesh_v2.cpp` (`addPendingAck`, `processAck`). Related to `COMPAT-001`.

---

### SEC-001 — WiFi OTA firmware endpoint protected only by a brute-forceable 4-digit PIN with no rate limiting

**Severity:** Medium
**Confidence:** High confidence
**Category:** SEC
**Affected files:** `src/hal/wifi_ota.cpp:131-198` (`/update` upload handler); `src/ui/screens_common.cpp:274-333` and `src/ui/screens/screen_settings_system.cpp:673-701` (4-digit PIN set/entry).

**Description.** The WiFi OTA web endpoint authenticates firmware uploads with `device_pin`, which the on-device UI constrains to **4 numeric digits** (`lv_textarea_set_max_length(..., 4)`, `atoi`). The `/update` handler validates PIN + CSRF but imposes **no attempt limit, lockout, or backoff** — an attacker can POST unlimited firmware-flash attempts. The `#687` fix correctly refuses to start OTA when `device_pin == 0`, and the per-session CSRF token is a good control, but the PIN keyspace is only 10⁴.

**Evidence.** `wifi_ota.cpp` upload handler aborts on wrong PIN via `Update.abort()` and returns, with no counter/delay. PIN keyspace is bounded by the 4-digit UI. OTA runs on an open (or WPA2) softAP the user explicitly starts.

**Trigger conditions.** User starts WiFi OTA (creating the AP or binding on STA IP); attacker within WiFi range (or on the same LAN in STA mode) scripts POSTs. 10⁴ attempts at even a few per second is minutes.

**User impact.** An attacker in radio range during an OTA session can brute-force the PIN and flash arbitrary firmware. Requires the user to have OTA active and the attacker to be nearby/on-net — realistic at a hackerspace/event, less so at home. Impact if successful is complete device compromise.

**Recommended fix.** Add server-side rate limiting to `/update`: cap failed PIN attempts per session (e.g. 5), then refuse further uploads until OTA is restarted; add an exponential delay between attempts. Consider allowing a longer PIN/passphrase for OTA specifically. Optionally auto-stop OTA after N minutes idle.

**Suggested tests.** Unit test `otaPinAccepts()` already exists (from #687); add a handler-level test asserting the attempt counter locks after N failures. Document an on-device test that scripted brute force is rejected.

**Suggested PR scope.** `wifi_ota.cpp` only. Related to `SEC-002`.

---

### PERF-001 — Message store performs an O(n) storm of SPIFFS file opens on every incoming message

**Severity:** Medium
**Confidence:** High confidence
**Category:** PERF / storage / battery
**Affected files:** `src/mesh/message_store.cpp:139-160` (`readRecordAt`), `:308-319` (`messageExists`), `:442-482` (`messageStoreAppend`); driven from `mesh_wrapper.cpp` `storeIncomingMessageForCompanion` on every RX.

**Description.** `messageStoreAppend` calls `messageExists`, which iterates all stored records (up to `MESSAGE_STORE_MAX_RECORDS = 64`) calling `readRecordAt` for each. `readRecordAt` **opens the store file twice per record** — once inside `readHeader` (open/read/close) and once to seek and read the record. For a full store that is ~128 SPIFFS `open()` calls (each a directory scan) for a single incoming message, before the append itself. `messageStoreMarkAcked`/`MarkCompanionSent` additionally `malloc(count·sizeof(StoredMessage))` and rewrite the whole file on every ACK.

**Evidence.** `readRecordAt` calls `readHeader(&count)` (opens file) then opens the file again to `seek`+`read`. `messageExists` loops `count` times calling `readRecordAt`. `messageStoreAppend` calls `messageExists` unconditionally.

**Trigger conditions.** Any incoming DM/channel/room message that gets mirrored to the companion store (the normal path). Worst case once the store reaches its 64-record cap.

**User impact.** SPIFFS opens are slow; ~128 of them per message can add tens to hundreds of milliseconds of stall inside the mesh loop, degrading UI responsiveness during message bursts and increasing flash wear. On battery this is avoidable wakeful work.

**Recommended fix.** Rewrite `messageExists`/load paths to open the file **once** and stream records sequentially (single header read + sequential record reads). Cache the record count in RAM. Consider an in-RAM index of `(conversation,sender,timestamp)` identities to answer `messageExists` without touching flash.

**Suggested tests.** Existing `test/test_message_store` covers correctness; add a test that counts mock file-open calls per append and asserts it is O(1) (or a small constant), not O(n).

**Suggested PR scope.** `message_store.cpp` internals only; public API unchanged. Medium risk (touches persistence) — lean on the existing store tests.

---

### PERF-002 — Full 320×240 framebuffer is flushed synchronously on every redraw

**Severity:** Medium
**Confidence:** High confidence
**Category:** PERF / rendering / battery
**Affected files:** `src/hal/display.cpp:770-807` (full-render mode, single 153.6 KB buffer), `:529-595` (`lvgl_flush_cb`), `:969-970` (loop pacing).

**Description.** The display is configured with `LV_DISPLAY_RENDER_MODE_FULL` and a single PSRAM buffer, so LVGL renders and the flush callback pushes the **entire** 320×240×2 = 153,600-byte frame over 40 MHz SPI on every invalidation, synchronously (`lv_display_flush_ready` is called only after `tft.endWrite()`), with no second buffer or DMA/render overlap. A one-pixel cursor blink or clock tick repaints and re-transmits the whole frame (~30 ms at 40 MHz).

**Evidence.** `lv_display_set_buffers(..., LV_DISPLAY_RENDER_MODE_FULL)` with `draw_buf` only; flush writes `w*h` pixels then `flush_ready`. Comment at `:99-101` states full mode is intentional to avoid tear lines.

**Trigger conditions.** Any UI change; continuous when animations, text cursors, or the terminal are active.

**User impact.** Higher CPU/SPI utilization and power draw than partial/dirty-rectangle rendering; competes with LoRa/SD for the shared SPI bus. Chosen deliberately to eliminate tearing, so this is a trade-off rather than a defect — but worth revisiting for battery life.

**Recommended fix.** Evaluate a **two-buffer** setup (double-buffered full or partial mode) so rendering of frame N+1 overlaps the DMA push of frame N, and/or DMA the flush (`startWrite`+`pushImageDMA`) so the CPU isn't blocked during transmit. If tearing is the concern, a second full PSRAM buffer with DMA push preserves tear-free output while freeing the CPU. Measure before/after with the telemetry render-flush counter.

**Suggested tests.** Bench measurement of flush time and frame rate (telemetry `report_render_flush`); no native test.

**Suggested PR scope.** `display.cpp` buffer setup + flush; **on-device visual + timing validation required** (tearing regression risk).

---

## 7. Low-Severity Findings

### HW-002 — Literal `\n` inside a comment swallows a `pinMode()` call in `TDeckBoard::begin()`
**Severity:** Low · **Confidence:** Confirmed · **Category:** BUG / HW
**Affected:** `src/hal/tdeck_board.h:62`
The line is a single physical line: `// Trackball button ... (GPIO 0 shared with BOOT)\n        pinMode(PIN_TRACKBALL, INPUT_PULLUP);`. The `\n` is a literal backslash-n **inside** the `//` comment, so the `pinMode` is part of the comment and never compiles. This is exactly the "`\n` literal instead of real newline" pattern the project's own rejection-trigger table forbids. **Mitigated:** the deferred input init `sigurdos_trackball_init()` sets `INPUT_PULLUP` on all five trackball pins (`trackball.cpp:253-255`), so GPIO0 only lacks a pull-up between `board.begin()` and deferred input init. **Fix:** replace the literal `\n` with a real newline so the `pinMode` executes in `begin()` (matching the DIO1/battery pin setup around it). **Test:** none automatable; code review.

### SEC-002 — On-device PIN gate cannot accept a `device_pin > 9999` set via the companion app
**Severity:** Low · **Confidence:** High confidence · **Category:** SEC / BUG
**Affected:** `src/ui/screens_common.cpp:280,315-320`; companion `CMD_SET_DEVICE_PIN` (`companion_bridge.cpp:841-847`)
The PIN-entry field is capped at 4 digits and compares `atoi(text) == device_pin`. The companion protocol sets `device_pin` as an arbitrary `uint32_t`. A phone-set PIN outside 0–9999 (or with significant leading zeros) can never be reproduced by the 4-digit on-device gate, locking the user out of Settings/Terminal on the device itself (recoverable only via the companion app). **Fix:** either widen the on-device PIN field to match the protocol range, or clamp/validate `CMD_SET_DEVICE_PIN` to the 4-digit space the UI supports and document it. Related to `SEC-001`.

### SEC-003 — Sensitive values stored in plaintext NVS/SPIFFS (informational)
**Severity:** Informational · **Confidence:** Confirmed · **Category:** SEC
**Affected:** `hal/prefs.cpp` (`wifi_password`, `device_pin`, `ble_pin`, repeater passwords), `persistence_store.cpp` (identity `/mesh_id`, channel PSKs)
WiFi/repeater passwords, PINs, the mesh private key, and channel secrets are stored unencrypted. This is normal practice for ESP32 firmware without flash encryption, but it means a physical flash dump exposes all credentials and the node identity. **Recommendation:** document the exposure; if the threat model warrants it, enable ESP32 flash encryption / NVS encryption. No code change proposed.

### DEAD-001 — `ctrl_held` is never set; `sigurdos_keyboard_is_ctrl()` always returns false
**Severity:** Low · **Confidence:** Confirmed · **Category:** DEAD
**Affected:** `src/hal/keyboard.cpp:161,607-610,662` (declared, reset, returned, never assigned `true`)
No code path sets `ctrl_held = true`. The accessor and its callers are effectively dead. **Fix:** either wire Ctrl detection into the raw modifier sampler or remove `ctrl_held`/`sigurdos_keyboard_is_ctrl()` and their (no-op) call sites. Verify no screen relies on the always-false return before removing.

### DEAD-002 — `platformio.ini.bak` is a tracked backup file
**Severity:** Low · **Confidence:** Confirmed · **Category:** DEAD / hygiene
**Affected:** repo root `platformio.ini.bak` (tracked in git)
A committed `.bak` of the build config. Dead clutter that can drift from `platformio.ini`. **Fix:** delete from the repo and add `*.bak` to `.gitignore`.

### DEAD-003 — Stale planning/audit artifacts committed to the repo
**Severity:** Low · **Confidence:** Medium confidence · **Category:** DEAD / DOC
**Affected:** `fork-merge-audit.md`, `merge-prs.md`, `docs/AUDIT.md`, `docs/AUDIT-2026-06-21.md` (all tracked)
Multiple one-off audit/merge planning documents are committed to the tree. They are point-in-time artifacts, not living docs, and duplicate/precede this audit. **Fix:** move to an `archive/` folder or delete; keep a single canonical `audit.md`. (This file overwrites the previous root `audit.md` per the audit mandate.)

### DEAD-004 — GT911 init reads then writes back 186 config bytes unchanged
**Severity:** Low · **Confidence:** High confidence · **Category:** DEAD / PERF
**Affected:** `src/hal/touch.cpp:153-172`
The touch init reads the 186-byte GT911 config in 32-byte chunks and writes it straight back without modifying anything. This is a no-op round trip (harmless, one-time) that also risks re-triggering a config checksum/update cycle on the controller. **Fix:** drop the read/write-back unless a field is actually being changed; if the intent was to force a config refresh, do so explicitly with the checksum/flag write the GT911 expects.

### RELI-001 — Touch controller has no re-init path after a persistent wedge
**Severity:** Low · **Confidence:** High confidence · **Category:** reliability
**Affected:** `src/hal/touch.cpp:45-46,236-248,271-277`
After `TOUCH_MAX_CONSECUTIVE_ERRORS` (5) consecutive I²C read failures the driver forces a release but never attempts re-initialization, and `init_attempted` latches so `sigurdos_touch_init()` won't retry. A wedged GT911 (bus glitch, ESD) means touch is dead until reboot. **Fix:** on sustained error, attempt a bounded re-init (reset via INT pin + re-probe) before giving up; expose a recovery counter in diagnostics.

### RELI-002 — Critical-battery deep sleep arms no wake source (reliability facet of HW-001)
See `HW-001`. Framed here as reliability: `board.sleep(0)` should always arm *some* wake (timer) so the node can re-evaluate charge state and recover without a power cycle.

### RELI-003 — `atomicReplaceStore` removes the live file before the rename
**Severity:** Low · **Confidence:** High confidence · **Category:** reliability
**Affected:** `src/mesh/message_store.cpp:351-356`
The "atomic" replace writes a temp file, then `SPIFFS.remove(STORE_PATH)` **before** `SPIFFS.rename(TMP_PATH, STORE_PATH)`. A power loss between the remove and the rename leaves no live store (the temp still exists but is not promoted on next boot). True atomic-rename semantics require renaming over the existing file, or promoting a surviving `.tmp` on load. **Fix:** rename temp→store without the prior remove if SPIFFS supports replace-rename, or on `messageStoreBegin` detect a leftover `.tmp` and promote it. Low probability, bounded blast radius (companion message history only).

### DOC-001 — Stale/incorrect comments
**Severity:** Low · **Confidence:** Confirmed · **Category:** DOC
- `navigation.cpp:34` says "max 8 entries" but `MAX_HISTORY = 16`.
- `touch.cpp:210` says "200kHz compromise" but the shared bus runs at `BUS_CLOCK_HZ = 100000` (100 kHz, `i2c_bus.h`).
- `keyboard.cpp:577` / general: comments reference the I²C clock being set "once in `TDeckBoard::begin()`" — accurate, but `configure_runtime()` re-asserts it in several inits; harmless, just noisy.
**Fix:** correct the comments during any nearby edit.

---

## 8. Performance and Efficiency Findings

Grouped by resource. High-value items are written up in §5–§7; the rest are collected here.

**CPU**
- `PERF-002` — full-frame synchronous flush blocks the CPU during SPI transmit (§6).
- `PERF-007` — contact lookups (`getPathLen`, `sendTextTo`, `sendRequest`, `findContactIndex`, favourite/DM helpers in `mesh_wrapper.cpp`) do a **linear scan copying a full `ContactInfo` per index** (up to `MAX_CONTACTS=350`) on each call, several of which run from UI refresh paths. Consider a name→index cache or `lookupContactByPubKey` where a key is known.

**RAM / Heap**
- `PERF-003` — `display.cpp:797` declares `static uint8_t emergency_buf[TFT_WIDTH*20*2]` (12,800 B) permanently in BSS even though the surrounding comment claims fallbacks are only allocated on PSRAM failure. Only reachable if both PSRAM *and* DRAM `heap_caps_malloc` fail. **Fix:** allocate it lazily like the other fallbacks, reclaiming ~12.8 KB of internal RAM in the common case.
- `PERF-006` — several hot-path helpers copy the entire `NodePrefs` **by value** (`SigurdMeshV2::getAirtimeBudgetFactor()`, `pathHashSize()`, `setDutyCycle`), and `prefs_get()` returns a reference but callers sometimes copy. Prefer `const NodePrefs&`.

**Flash usage**
- Release build: **Flash 40.4% (2,647,313 / 6,553,600 B)**, **RAM 40.9% (134,008 / 327,680 B)** — comfortable headroom; no flash-pressure findings.
- `persistence_store.cpp` leaves stale `ch_N_*` NVS keys when the channel count shrinks (only `ch_cnt` gates load). Wasted NVS space, no correctness impact.

**Rendering**
- `PERF-002` (§6). Also the character-picker and layout-indicator overlays are rebuilt per keypress — acceptable.

**Startup**
- Boot is dominated by SPIFFS mount, radio `std_init`, and map init; all necessary. `storage_init` only probes the first 64 bytes for the erased-partition heuristic — cheap. No slow-start finding.

**Networking / Message processing**
- `PERF-001` — per-message SPIFFS open storm (§6).
- `mesh_wrapper.cpp` `sendChannelMessage` does an O(channels) scan plus a nested O(contacts)×O(contacts) room-server loop (`getLoggedInRoomServerCount` + `getLoggedInRoomServerName` each rescan all contacts). Minor; only on send.

**Storage**
- `PERF-001`; `RELI-003`.

**Battery / power**
- `PERF-004` — `touch.cpp` polls the GT911 status register every 10 ms (100 Hz) even when `INT` is HIGH and idle, so the I²C bus is never quiet. Consider trusting `INT` edge more aggressively or lengthening the idle poll interval.
- `PERF-005` — `keyboard.cpp` issues a raw-modifier sample (mode-switch write + 5-byte read + mode-switch write) every 20 ms **and** an *extra* raw sample immediately after every ASCII byte, giving ~3 I²C transactions per 20 ms on the shared bus during typing. Bounded and functional, but a candidate for reducing bus churn.
- `display.cpp` loop caps LVGL idle sleep at `delay(min(next,5))`, so the super-loop spins at ≥~200 Hz with no light-sleep even when idle — expected for an interactive device, but a power lever if deep idle is ever desired.

---

## 9. Dead and Obsolete Code

### Safe to remove (evidence given)
- **`DEAD-001`** `ctrl_held` / `sigurdos_keyboard_is_ctrl()` — never set true; grep shows no assignment. Remove after confirming no caller depends on the always-false value.
- **`DEAD-002`** `platformio.ini.bak` — tracked backup of the build config; superseded by `platformio.ini`.
- **`DEAD-003`** `fork-merge-audit.md`, `merge-prs.md`, `docs/AUDIT.md`, `docs/AUDIT-2026-06-21.md` — point-in-time planning artifacts.
- **`DEAD-004`** GT911 read-then-write-back-unchanged block (`touch.cpp:153-172`) — functional no-op.

### Confirmed *not* dead (checked before flagging)
- All `env:*` build variants in `platformio.ini` are exercised by `smoke_build_matrix.py` and/or the CI build steps.
- `src/test/test_controller.cpp` and `src/validation/*` compile only under specific envs (`SIGURDOS_REMOTE_TEST`, `SIGURDOS_GPS_VALIDATION`) — not dead.
- `companion_adapter.inc` is `#include`d from `mesh_wrapper.cpp` (not a standalone TU) — intentional.
- `getBlobByKey`/`putBlobByKey`, LPP telemetry writers, region handlers — all reached via MeshCore virtual dispatch or the companion protocol.

### Appears unused but not proven dead
- Some `SigurdMeshV2` request/response and group-data helpers are only reachable through the companion protocol (`companion_adapter.inc`) or specific screens not fully traced in this pass (`screen_node_status`, `screen_telemetry`, `screen_trace`). They are wired to real commands; a deeper companion-protocol trace is needed before calling any of them dead.
- The `SIGURDOS_TRACKBALL_DEBUG_SHADOW` path and several `_debug_*` envs are diagnostic-only; retain.

---

## 10. MeshCore Compatibility Findings

### Confirmed incompatibility
- None outright. SigurdMeshV2 implements the required `BaseChatMesh` virtuals and the companion protocol frames match upstream layouts (device-info, self-info, contact iteration, message-recv v1/v3, channel data, trace, login, sign).

### Likely incompatibility / divergence
- **`COMPAT-001` (= `BUG-004`)** — `processAck` returning the wrong contact makes `handleReturnPathRetry` emit a `PATH_RETURN` for the wrong node on flood-routed ACKs, diverging from upstream's intended reciprocal-path behavior and potentially confusing standards-compliant peers. Medium.

### Compatibility risk
- **Timestamp corruption from `BUG-001`** — a wrong system clock means our outgoing message/advert timestamps are wrong, which can interact badly with peers' freshness/duplicate logic (advert acceptance, message ordering). This is a *risk* rather than a fixed incompatibility because it depends on the GPS path running.
- **Path-hash mode** — `pathHashSize()` originates floods with the configured 1–3 byte hash size to match companion firmware; `sendScopedImpl` sets `codes[1]=0` with a self-noted "REVISIT upstream" (`sigurd_mesh_v2.h:733`). Home/return region handling may not fully match upstream multi-region semantics; low risk, flagged by the author.

### Intentional SigurdOS-specific behavior (not defects)
- Hashtag channels (`sha256(name)` secret) and the built-in "Public" PSK channel — documented and interoperable by construction.
- Zero-hop PING/PONG and the `0x80/0x90` node-discovery control protocol in `onControlDataRecv` — SigurdOS extensions designed to interoperate with MeshCore repeaters/sensors; bounded parsing looked correct (minor: `atoi` on a non-terminated PONG payload is a bounded in-struct read, low risk).

---

## 11. T-Deck Hardware Findings

- **`HW-001`** (High) — deep-sleep/ext1 wake on non-RTC GPIO45; wake-on-packet dead and low-battery sleep unrecoverable. **Requires hardware validation.** (§5)
- **`HW-002`** (Low) — literal `\n` comment swallows the trackball pull-up `pinMode` in `begin()`; mitigated by deferred init. (§7)
- **`RELI-001`** (Low) — no touch re-init after a persistent I²C wedge. (§7)
- **Shared SPI bus** — display (SPI2_HOST/40 MHz via LovyanGFX), LoRa (RadioLib), and SD (4 MHz) share one FSPI peripheral with per-mount `begin()` resets. Documented as fragile in `CLAUDE.md`; `PERF-002`'s full-frame flush increases contention. No new defect, but the coupling is a standing risk for any timing change.
- **I²C** — centralized 100 kHz bus with a 20 ms transaction timeout and a 9-clock recovery routine before `Wire.begin()`; keyboard warm-handoff retries 8× — solid. Idle polling cost noted in `PERF-004/005`.
- **Battery** — `sigurdos_battery_mv()` uses `analogReadMilliVolts` (efuse-calibrated) ×2 for the divider; critical threshold 3200 mV. Reasonable; the *action* on critical (deep sleep) is the problem (`HW-001`).
- **Display init ordering** — backlight-before-panel and `lv_tick_set_cb` after `lv_init` are correctly ordered (matches the project's own gotcha list). Rotation/touch-transform constants match the documented landscape mapping.
- **Areas needing physical validation:** all of the above wake/sleep/touch-recovery items, plus the `PERF-002` double-buffer/DMA change and any keyboard/trackball timing changes (remote-test mode cannot exercise the physical layer, per `CLAUDE.md`).

---

## 12. Testing and CI Assessment

### Existing test types
- **Native unit tests** (`test/test_*`, googletest via `native_test` env): 60 modules, **835 cases (834 pass, 1 skipped)**. Cover keyboard/trackball/touch HAL contracts, prefs, storage, contact/message stores, channel validation, companion protocol, telemetry rings/protocol, layout/responsive math, navigation contract, emoji integrity, GPS *parsing*, OTA auth (`test_ota_auth`), github-ota contract, map/lodepng, QR, regions.
- **Build tests in CI**: release, debug, remote-test-radio, and two USA/Canada radio variants compile on every PR; nightly smoke matrix builds the roadmap env set.
- **Advisory lint**: `pio check` static analysis, a raw-`Serial.print` policy grep, and a doc-reference existence check — all in a `continue-on-error` job.

### Test quality
- Store and protocol tests are genuinely good: versioned formats, magic numbers, and boundary handling are exercised. The companion-protocol and message-store suites give real confidence in those subsystems.

### Missing coverage (highest-value gaps)
- **`TEST-001`** — **no test of the GPS time-sync arithmetic** (`BUG-001`). `test_gps` covers NMEA field parsing and checksum but not the epoch conversion, so a numerically wrong formula ships green. A single table-driven epoch test would have caught it.
- **`TEST-002`** — **no test of screen-transition widget lifecycle** (`BUG-002`). The `g_wifi_icon` dangling-pointer path is invisible to the suite because UI navigation + the 1 Hz refresh aren't simulated together. A mock-LVGL lifecycle test asserting globals are nulled on delete would cover it.
- No sanitizer (`-fsanitize=address,undefined`) build in CI — would catch `BUG-003` (out-of-scope stack read) and any latent UAF class.
- Hardware-in-the-loop is necessarily out of CI; `HW-001`, `PERF-002`, touch-recovery, and sleep/wake have **no automated coverage** and rely on manual bench testing.

### CI / build matrix
- Good breadth of compile targets; native tests gate PRs (hard fail). The static-analysis/logging/doc-ref job is **advisory only** (`continue-on-error: true`), so `pio check` findings never block a merge (`CI-001`).
- The logging-policy grep carries a large `not_migrated` whitelist (map_renderer, display, github_ota, keyboard, storage, trackball, wifi_ota, main, mesh_wrapper, sigurd_mesh_v2, chat_screen, home_screen, some screens) still permitted raw `Serial.print` (`CI-002`) — intended to shrink over time.

### Flaky / ineffective tests
- One test is permanently skipped (consistent across runs; not investigated here). No flakiness observed across the run.

### Tests that should be added
- GPS epoch table test (`TEST-001`); UI lifecycle / `g_wifi_icon` test (`TEST-002`); `processAck` correct-contact test (`BUG-004`); channel-sender parsing test with ASan (`BUG-003`); message-store file-open-count assertion (`PERF-001`); OTA PIN attempt-lockout test (`SEC-001`).

---

## 13. Maintainability and Architecture Findings

- **`ARCH-001` — Global widget pointers with convention-based ownership.** `g_wifi_icon`, `s_back_btn`, and many per-screen globals are cleared by each screen's teardown *by convention*. `BUG-002` is the direct consequence when one screen (Chat) forgets. **Recommendation:** centralize bottom-bar refresh so the loop asks the *current screen* for its live icon (or always `lv_obj_is_valid`-guards), rather than caching a raw pointer across screen lifetimes.
- **Very large files.** `chat_screen.cpp` (2885 lines) and `mesh_wrapper.cpp` (2377) dominate; both mix model, view, and dispatch. Small edits carry regression risk (the chat name-stride bug #686 lived here). Not urgent, but candidates for extraction (e.g. split the chat *model* `dyn_channels`/`ch_meta` from the *view*).
- **Duplicated queue logic.** `mesh_wrapper.cpp` has two near-identical `queue_push`/`mesh_v2_queue_push` implementations; drift risk. Consolidate.
- **Time-algorithm duplication.** The correct epoch math exists in `makeEpoch()` but was re-implemented (wrongly) in `gps.cpp` — the root of `BUG-001`. A single shared `civil_to_epoch()` used everywhere would prevent recurrence.
- **Conditional-compilation breadth.** Many `SIGURDOS_*` flags and ~15 build envs; the `#if SIGURDOS_DEBUG_*` scattering is manageable but dense. The `platformio.ini` `P_LORA_*` are defined *both* in the ini and (unguarded) in `tdeck_pins.h`, producing ~140 redefinition warnings that bury real ones (`BUILD-001`).
- **UI/protocol/HAL separation** is otherwise clean: the UI only calls `sigurdos::mesh::*`, HAL modules are self-contained, storage is behind typed stores. This is a genuine strength.

---

## 14. Security Assessment

Realistic threat model: an attacker with (a) physical possession, (b) WiFi/BLE radio proximity during an active session, or (c) the ability to send crafted LoRa/companion frames.

- **`SEC-001` (Medium)** — OTA firmware flash gated by a 4-digit PIN with no rate limiting; brute-forceable in minutes by a nearby attacker while OTA is active. Requires user to have started OTA. Highest realistic-impact security item. (§6)
- **`SEC-002` (Low)** — companion-set PINs outside the 4-digit range lock the on-device gate; a usability/lockout issue more than an exposure. (§7)
- **`SEC-003` (Info)** — plaintext credentials/identity in NVS/SPIFFS; standard without flash encryption; physical-access threat. (§7)
- **Companion protocol parsing (reviewed, no defect found).** `handleFrame` bounds every command with explicit `len >=` checks; `_cmd_frame`/`_out_frame` are `MAX_FRAME_SIZE+1` so the `_cmd_frame[len]=0` terminator is in-bounds; `MAX_FRAME_SIZE=176`. The signing buffer (`CMD_SIGN_DATA`) is length-checked against `SIGURDOS_COMPANION_MAX_SIGN_DATA`, and in-progress signing state is cleared on BLE disconnect (the `#712` fix), preventing cross-session signature injection. Channel-data and path-length fields are validated via `pathByteLen`. This is a well-hardened parser.
- **LoRa RX handlers.** `onControlDataRecv` PING/PONG/discovery parsing uses bounded copies and `memchr`-delimited fields; the one `atoi` on a non-terminated PONG tail is a bounded in-struct read (low). Anon/signed/channel message handlers copy into fixed buffers with clamps.
- **GitHub OTA.** TLS certificate is **pinned** to the Sectigo root (no `setInsecure`), the release JSON parser is hand-rolled with bounded copies, content-length is validated (≤6 MB), and the download URL is constructed, not taken from arbitrary input. Good posture.
- **WiFi web OTA.** Per-session CSRF token from the hardware RNG, PIN required (`#687`), refuses to start without a PIN. Weakness is the PIN strength/rate-limit (`SEC-001`).
- **`check_security_patches.py`** hard-fails the build if the bundled Arduino WebServer CVE patches (multipart boundary length, CRLF header sanitization) cannot be applied — a strong supply-chain control.

No hard-coded credentials, no secrets logged in release builds (debug prints are `#if SIGURDOS_DEBUG*`-gated), no path-traversal reachable from external input was found (SD/SPIFFS paths are internally constructed).

---

## 15. Recommended Remediation Roadmap

### Phase 1 — Critical safety and correctness
1. **Fix GPS epoch (`BUG-001`).** *Objective:* stop corrupting the system clock. *Files:* `hal/gps.cpp` (+ `test/test_gps_time/`). *Benefit:* correct timestamps, ACK matching, advert freshness. *Risk:* low. *Testing:* new native epoch table test; bench GPS fix. *Hardware:* not required for arithmetic; nice-to-have end-to-end. *PR scope:* tiny, isolated. *Deps:* none.
2. **Fix `g_wifi_icon` UAF (`BUG-002`).** *Objective:* remove the use-after-free. *Files:* `ui/chat_screen.cpp`, `ui/screens_common.cpp`. *Benefit:* eliminates a crash on Contacts→DM. *Risk:* low. *Testing:* UI-lifecycle native test; bench soak. *Hardware:* recommended to confirm crash gone. *PR scope:* small. *Deps:* none.
3. **Fix deep-sleep wake (`HW-001`).** *Objective:* device can always wake/recover. *Files:* `hal/tdeck_board.h`, `main.cpp`. *Benefit:* no unrecoverable low-battery hang; correct wake feature. *Risk:* medium (sleep path). *Testing:* on-device sleep/wake + low-battery. *Hardware:* **required.** *PR scope:* focused. *Deps:* none.

### Phase 2 — Reliability and compatibility
4. **Fix `processAck` contact (`BUG-004`/`COMPAT-001`).** Store pubkey in `PendingAck`; return the real contact. *Hardware:* not required (native test).
5. **Hoist channel-sender buffer (`BUG-003`).** One-line scope fix + ASan CI variant.
6. **Touch re-init on wedge (`RELI-001`)** and **atomic-store rename (`RELI-003`).** *Hardware:* touch item benefits from bench validation.

### Phase 3 — Performance and efficiency
7. **Message-store O(1) append (`PERF-001`).** Single-open streaming / RAM index; lean on existing store tests. *Hardware:* not required.
8. **Display double-buffer/DMA (`PERF-002`).** *Hardware:* **required** (tearing regression risk).
9. **Lazy `emergency_buf` (`PERF-003`)**, **idle-poll tuning (`PERF-004/005`)**, **contact-lookup caching (`PERF-007`).**

### Phase 4 — Dead-code removal and cleanup
10. Remove `ctrl_held` (`DEAD-001`), `platformio.ini.bak` (`DEAD-002`), stale audit/merge docs (`DEAD-003`), GT911 no-op round-trip (`DEAD-004`); correct stale comments (`DOC-001`); guard `P_LORA_*` redefinitions (`BUILD-001`).

### Phase 5 — Architecture and maintainability
11. Centralize bottom-bar/status-icon ownership (`ARCH-001`); consolidate duplicate `queue_push`; extract a shared `civil_to_epoch()`; begin splitting `chat_screen.cpp`/`mesh_wrapper.cpp`; make the CI static-analysis job blocking once clean (`CI-001`) and shrink the logging whitelist (`CI-002`).

---

## 16. Suggested Pull Request Breakdown

Each PR below is independently reviewable; none is created here.

**PR 1 — Fix GPS→epoch conversion**
- *Scope:* replace the wrong day-count formula with the shared Hinnant algorithm. *Findings:* `BUG-001`, `TEST-001`. *Files:* `hal/gps.cpp`, new `test/test_gps_time/`. *Tests:* native epoch table. *On-device:* optional GPS fix. *Risk:* low. *Deps:* none.

**PR 2 — Fix WiFi-icon use-after-free**
- *Scope:* clear `g_wifi_icon` in `chat_screen_show()` + `lv_obj_is_valid` guard in `update_wifi_status()`. *Findings:* `BUG-002`, `TEST-002`, `ARCH-001`. *Files:* `ui/chat_screen.cpp`, `ui/screens_common.cpp`, new UI-lifecycle test. *On-device:* Contacts→DM soak. *Risk:* low. *Deps:* none.

**PR 3 — Deep-sleep wake correctness (hardware)**
- *Scope:* `rtc_gpio_is_valid_gpio` guards + always-arm a timer wake. *Findings:* `HW-001`, `RELI-002`. *Files:* `hal/tdeck_board.h`, `main.cpp`. *On-device:* **required** (sleep/wake, low-battery). *Risk:* medium. *Deps:* none.

**PR 4 — ACK contact correctness**
- *Scope:* add pubkey to `PendingAck`; return correct contact. *Findings:* `BUG-004`/`COMPAT-001`. *Files:* `sigurd_mesh_v2.h/.cpp` + test. *On-device:* optional two-node ACK test. *Risk:* low. *Deps:* none.

**PR 5 — Message-store performance**
- *Scope:* single-open streaming + count cache. *Findings:* `PERF-001`. *Files:* `message_store.cpp` + open-count test. *On-device:* none. *Risk:* medium (persistence). *Deps:* none.

**PR 6 — Display double-buffer/DMA (hardware)**
- *Scope:* two-buffer full or DMA flush. *Findings:* `PERF-002`. *Files:* `hal/display.cpp`. *On-device:* **required** (tearing/timing). *Risk:* medium. *Deps:* none.

**PR 7 — Small-correctness batch**
- *Scope:* `BUG-003` buffer hoist, `HW-002` comment `\n`, `DOC-001` comments, `PERF-003` lazy buffer. *Files:* `sigurd_mesh_v2.cpp`, `tdeck_board.h`, `display.cpp`, `navigation.cpp`, `touch.cpp`. *Tests:* ASan CI variant. *Risk:* low. *Deps:* none.

**PR 8 — OTA PIN hardening**
- *Scope:* rate-limit/lockout `/update`, optional longer PIN. *Findings:* `SEC-001`, `SEC-002`. *Files:* `hal/wifi_ota.cpp` (+ maybe PIN UI). *On-device:* OTA session test. *Risk:* low. *Deps:* none.

**PR 9 — Repo hygiene / dead code**
- *Scope:* remove `platformio.ini.bak`, stale audit/merge docs, `ctrl_held`, GT911 no-op; guard `P_LORA_*` redefs; make static-analysis blocking. *Findings:* `DEAD-001..004`, `BUILD-001`, `CI-001/2`. *Risk:* low. *Deps:* none.

---

## 17. Validation Commands and Results

| Command | Result |
|---|---|
| `pio test -e native_test` | **835 test cases: 1 skipped, 834 succeeded** in ~54 s. PASS. |
| `pio run -e SigurdOS_TDeck` (warm) | **SUCCESS**, ~21 s. RAM 40.9% (134,008 B), Flash 40.4% (2,647,313 B). |
| `pio run -e SigurdOS_TDeck` (clean rebuild) | **SUCCESS**. 198 compiler warnings collected. |
| Warning breakdown | ~140 are `P_LORA_*` "redefined" (`tdeck_pins.h:150-156` vs `platformio.ini`); remainder are MeshCore submodule `-Wreorder`, `ESP32Board.h` inline-variable C++17 note, RadioLib god-mode/USB-CDC `#warning`s (intentional), vendored `qrcode.c` `#pragma mark`/type-limits, and `FSPI` redefined. No project-code warnings of substance beyond the redefinition noise. |
| GPS epoch cross-check (Python vs `datetime`) | Formula yields `truth − 719405` (negative) for all tested dates → **confirms `BUG-001`**. |
| `git ls-files` (root artifacts) | `audit.md`, `fork-merge-audit.md`, `merge-prs.md`, `platformio.ini.bak` are **tracked** → `DEAD-002/003`. |
| `git status` | Clean except untracked `goal.md`. |
| `grep -rn TODO/FIXME/HACK src/` | Only 5 hits (one real `FIXME` in `telemetry_crash.cpp` — documented panic-handler limitation; the rest are benign). |

*No code was modified to make any build or test pass. The single skipped native test is skipped by the suite itself, consistently across runs.*

---

## 18. Audit Limitations

- **No device flashed or connected.** All hardware-runtime symptoms (`HW-001`, `RELI-001`, the crash manifestation of `BUG-002`, and `PERF-002` timing) are inferred from code and the documented ESP32-S3 RTC-GPIO range; they are marked *Requires hardware validation* accordingly.
- **MeshCore submodule not audited** as project code — only SigurdOS's calls into it.
- **Partial reads of the largest files:** `chat_screen.cpp` (~2885 lines) and `companion_bridge.cpp` (~1293 lines) were read at their critical regions (message flow, top-bar lifecycle, protocol dispatch, bounds checks) but not line-by-line in full; `map_renderer.cpp` and generated emoji/font data were spot-checked only. Additional findings may exist in the unread portions.
- **Native-test blind spots:** anything under `#if ESP32_PLATFORM` or in the sleep/wake/radio/display-DMA paths is not exercised by the suite, which is precisely where `BUG-001` and `HW-001` live.
- Severity/reachability for security items assumes the stated threat model; a more permissive model (e.g. always-on OTA) would raise `SEC-001`.

---

## 19. Final Prioritised Findings Table

| ID | Title | Severity | Confidence | Category | Affected files | User impact | Recommended action | HW test? | PR |
|---|---|---|---|---|---|---|---|---|---|
| BUG-001 | GPS→epoch math wrong; corrupts system clock | High | Confirmed | BUG/timing | `hal/gps.cpp:361-381` | Wrong timestamps, ACK/advert issues | Use `makeEpoch()` / correct offset | No (arith) | PR 1 |
| BUG-002 | Dangling `g_wifi_icon` use-after-free | High | High (crash: HW-val) | BUG/UAF | `screens_common.cpp`, `chat_screen.cpp` | Heap corruption/crash on DM open | Clear global + validity guard | Rec. | PR 2 |
| HW-001 | Deep-sleep wake on non-RTC GPIO45 | High | Requires HW val | HW/power | `tdeck_board.h:75-140`, `main.cpp:189-198` | No wake-on-packet; unrecoverable low-batt sleep | Guard RTC-GPIO; always arm timer wake | **Yes** | PR 3 |
| BUG-004 | `processAck` returns wrong contact | Medium | High | BUG/COMPAT | `sigurd_mesh_v2.cpp:459-481` | Bogus path-return, wasted airtime | Store pubkey in PendingAck | Opt. | PR 4 |
| BUG-003 | Out-of-scope stack buffer in channel RX | Medium | Confirmed | BUG/UB | `sigurd_mesh_v2.cpp:560-578` | Latent garbage sender attribution | Hoist buffer to fn scope | No | PR 7 |
| SEC-001 | OTA gated by brute-forceable 4-digit PIN, no rate limit | Medium | High | SEC | `hal/wifi_ota.cpp:131-198` | Unauthed flash by nearby attacker during OTA | Rate-limit/lockout, longer PIN | Rec. | PR 8 |
| PERF-001 | O(n) SPIFFS open storm per incoming message | Medium | High | PERF/storage | `message_store.cpp:139-160,308-319` | UI stalls, flash wear on RX bursts | Single-open streaming + RAM index | No | PR 5 |
| PERF-002 | Full-frame synchronous flush every redraw | Medium | High | PERF/render | `hal/display.cpp:770-807,529-595` | Higher CPU/power, SPI contention | Double-buffer / DMA flush | **Yes** | PR 6 |
| HW-002 | Literal `\n` comment swallows trackball `pinMode` | Low | Confirmed | BUG/HW | `tdeck_board.h:62` | GPIO0 pull-up deferred (mitigated) | Real newline | **PR #776** | ✅ Fixed |
| SEC-002 | Companion PIN >4 digits locks on-device gate | Low | High | SEC/BUG | `screens_common.cpp:280`, `companion_bridge.cpp:841` | Owner locked out of Settings/Terminal | Align PIN ranges | No | PR 8 |
| SEC-003 | Plaintext secrets in NVS/SPIFFS | Info | Confirmed | SEC | `prefs.cpp`, `persistence_store.cpp` | Flash dump exposes creds/identity | Document; consider flash encryption | No | — |
| PERF-003 | 12.8 KB `emergency_buf` always in BSS | Low | High | PERF/RAM | `hal/display.cpp:797` | Wasted internal RAM | Allocate lazily | No | PR 7 |
| PERF-004 | Touch status polled 100 Hz when idle | Low | High | PERF/battery | `hal/touch.cpp:202-232` | Constant idle bus/power | Trust INT / longer idle poll | Rec. | Phase 3 |
| PERF-005 | Keyboard raw-sample I²C churn + extra post-byte sample | Low | High | PERF/battery | `hal/keyboard.cpp:569-592` | Extra bus traffic while typing | Reduce sampling | Rec. | Phase 3 |
| PERF-007 | Full-`ContactInfo`-copy linear scans in hot paths | Low | High | PERF/CPU | `sigurd_mesh_v2.cpp`, `mesh_wrapper.cpp` | CPU on UI refresh | Name→index cache | No | Phase 3 |
| RELI-001 | No touch re-init after persistent I²C wedge | Low | High | reliability | `hal/touch.cpp:236-248,271-277` | Touch dead until reboot | Bounded re-init/reset | Rec. | Phase 2 |
| RELI-003 | `atomicReplaceStore` removes before rename | Low | High | reliability | `message_store.cpp:351-356` | Store lost on power-cut mid-swap | Replace-rename / promote `.tmp` | No | Phase 2 |
| DEAD-001 | `ctrl_held`/`is_ctrl()` always false | Low | Confirmed | DEAD | `hal/keyboard.cpp:161,607` | none | Remove or wire up | **PR #776** | ✅ Fixed |
| DEAD-002 | `platformio.ini.bak` tracked | Low | Confirmed | DEAD | `/platformio.ini.bak` | none | Delete + gitignore | **PR #776** | ✅ Fixed |
| DEAD-003 | Stale audit/merge docs tracked | Low | Medium | DEAD/DOC | `fork-merge-audit.md`, `merge-prs.md`, `docs/AUDIT*.md` | none | Archive/delete | **PR #776** | ✅ Fixed |
| DEAD-004 | GT911 config read/write-back no-op | Low | High | DEAD | `hal/touch.cpp:153-172` | none (minor init cost) | Remove round-trip | **PR #776** | ✅ Fixed |
| BUILD-001 | ~140 `P_LORA_*` redefinition warnings | Low | Confirmed | BUILD | `tdeck_pins.h:150-156` | Real warnings buried | `#ifndef` guards | **PR #776** | ✅ Fixed |
| DOC-001 | Stale comments (history size, I²C clock) | Low | Confirmed | DOC | `navigation.cpp:34`, `touch.cpp:210` | Confusing | Correct both comments | **PR #776** | ✅ Fixed |
| CI-001 | Static-analysis job is advisory (non-blocking) | Low | Confirmed | CI | `.github/workflows/pr-ci.yml` | `pio check` never gates | Remove continue-on-error | **PR #776** | ✅ Fixed |
| CI-002 | Large raw-`Serial.print` whitelist | Low | Confirmed | CI | `pr-ci.yml` logging grep | Policy under-enforced | Shrink whitelist | No | Phase 5 |
| TEST-001 | No GPS time-sync/epoch test | Medium | Confirmed | TEST | `test/test_gps/` | Let `BUG-001` ship | Add epoch table test | No | PR 1 |
| TEST-002 | No screen-transition lifecycle test | Medium | Confirmed | TEST | `test/` (missing) | Let `BUG-002` ship | Add UI-lifecycle test | No | PR 2 |
| ARCH-001 | Global widget pointers, convention ownership | Low | Confirmed | ARCH | `ui/*` | Fragile (root of BUG-002) | Centralize status-icon ownership | No | Phase 5 |
| COMPAT-001 | Wrong-contact path-return vs upstream | Medium | High | COMPAT | `sigurd_mesh_v2.cpp:459-481` | Protocol divergence | See BUG-004 | Opt. | PR 4 |
| DIAG-001 | Crash telemetry captures shutdown, not crash site | Low | Confirmed | diagnostics | `telemetry_crash.cpp:86` | Unreliable crash PCs | Use panic-handler API | Rec. | Phase 5 |

---

*End of audit. This file is the sole artifact produced; no source code, configuration, tests, or hardware were modified, and no branches/PRs were created, per the audit mandate.*
