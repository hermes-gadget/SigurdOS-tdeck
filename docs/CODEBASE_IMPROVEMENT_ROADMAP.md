# SigurdOS T-Deck — Codebase Improvement Roadmap

Date: 2026-06-09
Baseline: `dev` at `064e9c9` (`docs: add Launcher compatibility roadmap (#568)`)
Related issue: [#569](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/569)

This document is an **engineering-quality roadmap**: a prioritized, phased plan for making
the codebase more efficient, reliable, maintainable, and robust **without removing or
changing existing functionality**. It complements (and does not replace):

- [`docs/ROADMAP.md`](ROADMAP.md) — the feature/product roadmap (companion parity, hardware
  validation gates, release readiness). Items already tracked there are referenced, not
  duplicated.
- [`docs/AUDIT.md`](AUDIT.md) — the 2026-06-07 point-in-time end-to-end audit with build/test
  evidence and a risk register focused on release hardening.
- [`docs/KNOWN_ISSUES.md`](KNOWN_ISSUES.md) — the live tracker for observed bugs.

Every recommendation below cites repository evidence, states the problem, proposes a fix,
estimates benefit and risk, and gives validation steps. Each is tagged **first-PR-safe**
(small, isolated, low risk) or **later work** (needs design, review, or hardware time).

Baseline evidence gathered while writing this document (2026-06-09, local checkout of `dev`):

| Check | Result |
| --- | --- |
| `pio test -e native_test` | 744 test cases: 743 succeeded, 1 skipped, 00:04:26 |
| Largest source files | `src/ui/screens.cpp` 7,829 lines; `src/ui/chat_screen.cpp` 2,862; `src/mesh/mesh_wrapper.cpp` 2,314; `src/mesh/sigurd_mesh_v2.cpp` 1,199; `src/app/map_renderer.cpp` 1,077 |
| Release build RAM/flash (from `docs/AUDIT.md`, 2026-06-06 refresh) | RAM 33.8%, flash 30.1% |

---

## Executive Summary

SigurdOS T-Deck is a healthy, actively maintained firmware with an unusually strong native
test suite (56 suites, 744 cases, run on every PR), strict warnings, an issue-first
workflow, and a steady stream of focused bug-fix PRs (#543–#568). Static RAM use dropped
from 86.4% to 33.8% after the PSRAM pool work, leaving real headroom.

The highest-leverage improvements are not features — they are:

1. **CI does not compile the firmware on PRs** (`.github/workflows/pr-ci.yml` runs only
   native tests). A PR can pass CI and still break `pio run -e SigurdOS_TDeck`.
2. **`src/ui/screens.cpp` is a 7,829-line monolith** holding ~30 screens and 88 file-static
   globals. It is the single biggest drag on review speed, merge-conflict rate, and
   agent/contributor onboarding.
3. **Agent-facing documentation has drifted from the code** (`CLAUDE.md`/`AGENTS.md`
   reference files, repos, and gotchas that no longer exist), which actively misleads the AI
   agents this project relies on.
4. **A handful of reliability gaps**: boot hangs forever if display init fails, the contacts
   persistence format has no version/magic header, and buzzer patterns block the entire
   main loop.
5. **Reproducibility gaps**: caret version ranges in `lib_deps` mean two clean clones can
   build different dependency versions.

All of these are addressable incrementally, with behavior preserved, using the existing
test suite and the documented remote-test/hardware validation flows as safety nets.

---

## Current Strengths

Worth naming explicitly so future work preserves them:

- **Native test coverage**: 56 `test/test_*/` suites, 744 cases, mocked hardware
  (`test/mocks/`), run on every PR (`.github/workflows/pr-ci.yml`) and before every release
  (`.github/workflows/build-release.yml`).
- **Strict warnings**: `-Wall -Wextra` in `platformio.ini` `[common]` `build_flags_common`.
- **Memory discipline**: PSRAM-first allocation with DRAM fallback in
  `src/hal/lv_pool.cpp:36-39`, `src/app/map_renderer.cpp:116-117` and `:617-619`; LVGL pool
  routed through `sigurdos_lv_pool_alloc` (`src/lv_conf.h`).
- **Input robustness**: GPS NMEA checksum validation with a failure counter
  (`src/hal/gps.cpp:269-299`, exposed via `sigurdos_gps_checksum_failures()`).
- **Secure OTA**: GitHub OTA uses `WiFiClientSecure::setCACert(GITHUB_ROOT_CA)`
  (`src/hal/github_ota.cpp:202-203`, `:282-283`) — no `setInsecure()` anywhere.
- **Non-blocking boot networking**: WiFi auto-connect uses the async
  `wifi_sta::beginConnect()` state machine (`src/main.cpp:128`,
  `src/hal/wifi_ota.cpp:245-259`), not the blocking variant.
- **Flash-write hygiene**: `shutdown()` and `factoryReset()` in
  `src/mesh/mesh_wrapper.cpp` save state and insert settle delays (150/200 ms) before
  sleep/restart; factory reset wipes NVS namespaces, SPIFFS, and calls `nvs_flash_erase()`.
- **Graceful degradation**: touch or keyboard init failure does not halt boot
  (`src/hal/display.cpp` — device continues with the remaining input methods).
- **Release tooling**: `scripts/merge_bin.py` merges with `flash_mode: keep` (avoids the
  documented DIO/QIO bootloop) and generates a `webflasher/manifest.json` with SHA-256
  checksums; `scripts/build_metadata.py` embeds git SHA/tag/dirty state into the binary.
- **Shared-bus correctness**: SPI2_HOST consolidated into a shared singleton
  (`src/hal/spi_shared.cpp`, PR #546), ending per-driver reinitialisation.
- **Test/debug infrastructure**: remote-test serial controller (`src/test/test_controller.cpp`),
  telemetry builds, per-feature debug envs, and validation harnesses under
  `scripts/validation/`.

---

## Key Risks and Technical Debt

Identifiers (R1…) are used in the risk matrix and phase plan below.

### R1 — PR CI never compiles the firmware

- **Evidence**: `.github/workflows/pr-ci.yml` has a single job running
  `pio test -e native_test -v`. Only `.github/workflows/build-release.yml` (tag-triggered)
  runs `pio run -e SigurdOS_TDeck`.
- **Problem**: the native test env mocks all hardware and excludes most firmware-only code
  (`build_src_filter` in `[env:native_test]` compiles a subset of `src/`). A PR that breaks
  ESP32-only code paths (display, OTA, BLE, mesh radio glue) passes CI and is only caught
  at release time or by a maintainer building locally.
- **Proposed fix**: add a `build` job to `pr-ci.yml` running `pio run -e SigurdOS_TDeck`
  with the existing PlatformIO cache strategy. Optionally extend later with the already
  existing-but-unused `scripts/smoke_build_matrix.py` (profiles `release`, `debug`,
  `roadmap`) as a nightly or label-gated job to cover `SigurdOS_TDeck_telemetry` and
  `SigurdOS_TDeck_remote_test*` envs.
- **Expected benefit**: eliminates the most common silent-breakage path for contributor and
  AI-agent PRs; `docs/AUDIT.md` recorded a cold build at ~10.5 min and a warm one at
  ~1.5 min, so cached CI cost is acceptable.
- **Risk level**: Low (CI-only change; no firmware behavior).
- **Validation**: open a draft PR with an intentional firmware-only compile error and
  confirm CI fails; revert; confirm green.
- **First PR-safe?**: Yes — but note `CONTRIBUTING.md` "Protected Files" requires a
  dedicated PR for `.github/workflows/*`, so it must ship as its own PR.

### R2 — `src/ui/screens.cpp` monolith

- **Evidence**: `src/ui/screens.cpp` is 7,829 lines, contains ~30 screens (every
  `make_screen_full(...)` call site from "Packets" at line 511 to "Regions" at line 7554),
  88 file-static declarations, and even a duplicated include
  (`#include "../hal/display.h"` at both lines 34 and 36). Next largest:
  `src/ui/chat_screen.cpp` (2,862) and `src/mesh/mesh_wrapper.cpp` (2,314).
- **Problem**: every screen change touches the same file → merge conflicts between
  concurrent PRs, slow reviews, accidental coupling through shared statics, and the file
  exceeds what humans (and AI agents with bounded context) can reason about whole.
- **Proposed fix** (behavior-preserving, incremental):
  1. Create `src/ui/screens/` and move **one screen at a time** into its own translation
     unit (e.g. `screen_packets.cpp`, `screen_settings.cpp`…), keeping the public
     `*_screen_show()` signatures declared in `screens.h` unchanged.
  2. Promote shared helpers (`make_screen_full`, the PIN-entry gate, the WiFi icon
     updater) into a small `screens_common.cpp/h` first, since each split file needs them.
  3. Keep per-screen statics within their new file; do not redesign state handling in the
     same PR as a move.
- **Expected benefit**: smaller diffs, parallel-friendly PRs, per-screen ownership, easier
  targeted review; no binary behavior change (same code, new file boundaries).
- **Risk level**: Medium (mechanical moves can drop a static initializer or break include
  order; mitigated by one-screen-per-PR and the build).
- **Validation** per split PR: `pio test -e native_test -v` (navigation/UI contract suites),
  `pio run -e SigurdOS_TDeck`, then remote-test smoke: `nav <screen>` to every moved screen
  via the serial test controller, plus `scripts/batch` screenshot comparison where
  available.
- **First PR-safe?**: The `screens_common` extraction + 1–2 small screens, yes. Full split
  is later, phased work.

### R3 — Agent-facing documentation drift

- **Evidence** (all verifiable in the current tree):
  - `CLAUDE.md` line 89 documents `src/mesh/slop_mesh.h`, which does not exist —
    `src/mesh/` contains `mesh_wrapper.*`, `sigurd_mesh_v2.*`, `message_store.*`,
    `regions.*`, `channel_validation.*`.
  - `CLAUDE.md` says "Main + dev branch model … `main` — stable releases only" while
    `CONTRIBUTING.md` (Development Workflow) says "There is no `main` branch; releases are
    tagged directly on `dev`."
  - `CLAUDE.md` instructs agents to open issues on `hermes-gadget/SlopOS-tdeck`; the actual
    origin is `hermes-gadget/SigurdOS-tdeck` (and `CONTRIBUTING.md` links there).
  - `CLAUDE.md` Gotchas table still lists "GPS NMEA — No checksum validation in `gps.cpp`",
    but validation was implemented (`src/hal/gps.cpp:269-299`).
  - `docs/AUDIT.md:53` and `docs/BLE_HARDWARE_VALIDATION.md` (multiple lines) cite
    `pio run -e SigurdOS_TDeck_ble`, an env that no longer exists in `platformio.ini` —
    BLE is now in the base env via `-D SIGURDOS_COMPANION_BLE=1`; only
    `SigurdOS_TDeck_ble_validation` remains.
- **Problem**: this project explicitly onboards AI agents via these files. Stale
  instructions send agents to the wrong repo, the wrong files, and the wrong build envs,
  and cause duplicated "fixes" for already-fixed gotchas.
- **Proposed fix**: a docs-only refresh pass. Note `CLAUDE.md`/`AGENTS.md` are owner-only
  ("Any PR that touches CLAUDE.md or AGENTS.md will be rejected") — those specific files
  must be updated **by the repo owner**; this roadmap can only flag the exact lines.
  `docs/AUDIT.md` / `docs/BLE_HARDWARE_VALIDATION.md` can be corrected by normal PR (add an
  "env renamed" note rather than rewriting historical evidence).
- **Expected benefit**: agents and contributors stop acting on wrong information; fewer
  rejected/duplicated PRs.
- **Risk level**: Low (documentation only).
- **Validation**: grep-based check that doc-referenced paths exist, e.g.
  `grep -oP 'src/[a-z_/]+\.(h|cpp)' CLAUDE.md | sort -u | xargs -I{} test -f {}` — could be
  a tiny CI step later.
- **First PR-safe?**: Yes for non-protected docs; owner action for `CLAUDE.md`/`AGENTS.md`.

### R4 — Boot hangs forever on display-init failure

- **Evidence**: `src/main.cpp:65-68` —
  `if (!sigurdos_display_init()) { Serial.println("[boot] FATAL…"); while (1) delay(1000); }`
- **Problem**: a transient display/SPI fault (or a brown-out during init) leaves the device
  in a silent infinite loop: no retry, no reboot, no headless fallback — radio and serial
  console are never started, so a remote/portable node just dies until physically reset.
- **Proposed fix**: retry display init 2–3 times with short backoff; on persistent failure
  either (a) `ESP.restart()` after a delay (recover-by-reboot, matches embedded norm), or
  (b) continue headless with mesh + serial alive so the node still functions and can be
  diagnosed. Option (a) is the smaller change; (b) needs UI guards everywhere.
- **Expected benefit**: converts a permanent field failure into a self-recovering or at
  least diagnosable one.
- **Risk level**: Low for (a) — a reboot loop on truly dead hardware is no worse than a
  hang loop and is visible on serial. Medium for (b).
- **Validation**: temporarily force `sigurdos_display_init()` to return false in a debug
  build; verify retry logs + restart over serial; restore.
- **First PR-safe?**: Yes (option a — a few lines, isolated).

### R5 — Floating dependency versions

- **Evidence**: `platformio.ini` `lib_deps_common`: `jgromes/RadioLib @ ^7.6.0`,
  `rweather/Crypto @ ^0.4.0`, `lovyan03/LovyanGFX @ ^1.2.0`,
  `adafruit/Adafruit BusIO @ ^1.16.1` (caret ranges) — while `lvgl @ 9.3.0`,
  `platformio/espressif32@6.11.0`, and `googletest @ 1.17.0` are exact. `[env:native_test]`
  `platform = native` is unversioned.
- **Problem**: two clean clones at different times can resolve different RadioLib/LovyanGFX
  minor versions → non-reproducible builds, "works for me" CI/local divergence, and
  surprise breakage when an upstream minor release changes behavior (RadioLib minors have
  historically changed radio API behavior).
- **Proposed fix**: pin exact versions for all four caret deps (choose the currently
  resolved versions; `pio pkg list -e SigurdOS_TDeck` shows them) and document the bump
  procedure in `CONTRIBUTING.md`. Tradeoff: upstream fixes now require an explicit bump PR
  — that is the desired behavior for firmware.
- **Expected benefit**: reproducible builds across machines/time; CI cache stability.
- **Risk level**: Low (pinning to the already-resolved versions changes nothing today).
- **Validation**: `rm -rf .pio && pio run -e SigurdOS_TDeck && pio test -e native_test`
  on a clean clone; compare `pio pkg list` output before/after.
- **First PR-safe?**: Yes.

### R6 — `platformio.local.ini` is tracked and shadows a real env

- **Evidence**: `platformio.local.ini` is committed at HEAD (header comment: "Local
  overrides for T-Deck — adds MESH_DEBUG") and redefines
  `[env:SigurdOS_TDeck_remote_test_radio]`, which also exists in `platformio.ini` with a
  different flag set (the tracked local file adds `-D SIGURDOS_DEBUG_MESH=1`).
- **Problem**: a file whose purpose is *local* overrides is shared by everyone who clones,
  and it silently diverges one env's flags from the canonical definition — which definition
  a given build used becomes ambiguous in bug reports.
- **Proposed fix**: delete the tracked copy, add `platformio.local.ini` to `.gitignore`,
  and fold any genuinely-wanted flags into a named env in `platformio.ini` (e.g. an
  explicit `…_meshdebug` env). Document the local-override convention in `CONTRIBUTING.md`.
- **Expected benefit**: one source of truth for build flags; reproducible bug reports.
- **Risk level**: Low — but confirm with the owner which flag set is canonical before
  deleting (see Open Questions).
- **Validation**: `pio run -e SigurdOS_TDeck_remote_test_radio` before/after; diff the
  compiler command lines (`pio run -v`) to confirm intended flags.
- **First PR-safe?**: Yes, once the owner confirms intent.

### R7 — Contacts persistence format has no version/magic/bounds check

- **Evidence**: `src/mesh/mesh_wrapper.cpp` `saveContacts()`/`loadContacts()`
  (~lines 1541-1596): writes a raw `int` count then fixed-width records
  (32-byte pubkey, 32-byte name, type, perm); `loadContacts()` reads `n` from the file and
  only checks `n <= 0` — no magic number, no format version, no upper bound against
  `MAX_CONTACTS` (350, from `platformio.ini`), no checksum. A truncated/corrupted SPIFFS
  file yields partially-garbage contacts silently (reads stop at EOF but whatever parsed
  is kept).
- **Problem**: any future format change (the perm byte was already appended once, per the
  inline comment) has no way to detect old files; corruption is indistinguishable from
  valid data.
- **Proposed fix**: add a 4-byte magic + 1-byte version header on save; on load, accept
  both headered (new) and headerless (legacy) files for one release cycle; clamp `n` to
  `MAX_CONTACTS`; optionally append a CRC32. Same pattern applies to the `/msgs` //
  `/companion_msgs` stores — but store unification is already tracked in
  `docs/ROADMAP.md` ("Message persistence"), so coordinate rather than duplicate.
- **Expected benefit**: forward-compatible persistence; corruption detected instead of
  ingested; safe future migrations.
- **Risk level**: Medium (touches user data on upgrade — needs the dual-read path and a
  test for legacy-file load).
- **Validation**: new `test/test_*` suite cases: save→load round-trip, legacy-format load,
  truncated-file load, `n > MAX_CONTACTS` clamp; then hardware check that existing devices
  keep their contacts across the upgrade.
- **First PR-safe?**: Later work (Phase 2) — small but it touches persisted user data.

### R8 — Release-logging policy is enforced by eyeball

- **Evidence**: the project's own rejection trigger is "Unconditional `Serial.printf`
  without `#if defined(SIGURDOS_DEBUG)` guard" (`CLAUDE.md`), yet
  `grep -rn "Serial.print" src --include="*.cpp"` returns ~126 call sites outside the
  debug/test/telemetry modules. Many are intentional warnings/errors (the documented policy
  allows "critical errors and warnings" in release), e.g. `src/main.cpp:52`, but nothing
  mechanically distinguishes an allowed warning from a leaked debug print.
- **Problem**: reviewers must hand-classify every print; the policy can't be linted; noise
  can leak into release builds unnoticed.
- **Proposed fix**: introduce thin logging macros (e.g. `SIG_LOGE/SIG_LOGW` always-on,
  `SIG_LOGD` compiled out unless `SIGURDOS_DEBUG*`), migrate call sites incrementally
  (file-by-file, no behavior change for E/W sites), then add a CI grep forbidding raw
  `Serial.print` outside the HAL/test/diagnostics whitelist.
- **Expected benefit**: the existing policy becomes mechanically enforceable; release
  binaries lose accidental chatter (minor flash/CPU win).
- **Risk level**: Low per-file; the only hazard is mis-classifying an error print as debug
  (review each file's diff).
- **Validation**: `pio run -e SigurdOS_TDeck` + serial boot-log comparison before/after on
  a debug and a release build (release log should be unchanged or strictly quieter).
- **First PR-safe?**: Macro header + one pilot file, yes; full migration is phased.

---

## Efficiency Improvements

### E1 — Buzzer patterns block the entire main loop

- **Evidence**: `src/hal/buzzer.cpp:14-23` — `buzzer_play_pattern()` walks the pattern with
  `delay(pattern[i].duration_ms)` per step, called synchronously from UI paths
  (`buzzer_beep_short/double`).
- **Problem**: during a beep, `loop()` is stalled — LVGL stops animating, touch/keyboard
  input queues stall, and `sigurdos::mesh::loop()` is delayed by the total pattern length.
  Short beeps make this minor today, but any longer notification pattern would directly
  stall mesh servicing.
- **Proposed fix**: convert to a non-blocking pattern player: store pattern + index +
  next-transition `millis()` in statics, advance from a `buzzer_loop()` called in
  `loop()` (or an `esp_timer`). Public API unchanged.
- **Expected benefit**: zero main-loop stalls from audio; enables richer patterns safely.
- **Risk level**: Low (isolated module; existing `test_buzzer` suite can be extended with
  a tick-based fake clock — `test/mocks/mock_arduino.cpp` already mocks `millis`).
- **Validation**: extend `test/test_buzzer`; on hardware, trigger a beep during a screen
  animation and verify no frame hitch (remote-test `tb`/`nav` during beep).
- **First PR-safe?**: Yes.

### E2 — Fixed boot delays

- **Evidence**: `src/main.cpp:35-37` — `delay(250)` ("Let WebSerial port close") +
  `delay(500)` after `Serial.begin()`; `src/hal/display.cpp:748` — `delay(50)` backlight
  pulse; `src/hal/sdcard.cpp:44-53` — up to 2×`delay(500)` retrying `SD.begin()` when **no
  card is present**. Worst case ≈ 1.8 s of pure waiting before the UI is interactive.
- **Problem**: every boot pays for the worst case. The serial delays exist for USB-CDC
  enumeration (documented), but they also run on battery boots where no host is attached;
  the SD retries pay 1 s on every cardless boot.
- **Proposed fix** (each independently):
  - Gate the 750 ms serial settle on USB actually being attached if detectable, or reduce
    empirically (open question below — needs hardware measurement, the comment documents a
    real race).
  - SD: attempt once at boot; if it fails, retry lazily/asynchronously (e.g. next loop
    iterations or on first SD-dependent action like the Map screen) instead of blocking
    `setup()`.
- **Expected benefit**: ~1–1.7 s faster cold boot in the common cardless/battery case.
- **Risk level**: Medium — these delays guard real hardware races (USB CDC claim, SD
  power-up). Changes must be validated on hardware, both T-Deck revisions if possible.
- **Validation**: debug-build boot logs with timestamps (`[boot] step N`), 10 boots each:
  USB-attached, battery-only, card present, card absent; confirm SD still mounts and
  serial still enumerates.
- **First PR-safe?**: No — hardware-validation-gated (Phase 3).

### E3 — Per-loop WiFi status label rewrite

- **Evidence**: `src/main.cpp:159` calls `sigurdos::ui::update_wifi_status()` every loop
  iteration; `src/ui/screens.cpp:368-383` then `snprintf`s and `lv_label_set_text()` +
  `lv_obj_set_style_text_color()` on every call while connected. `lv_label_set_text`
  reallocates/invalidates even for identical text.
- **Problem**: thousands of needless label updates per second while WiFi is connected —
  wasted CPU and LVGL invalidation churn on a value (RSSI) that changes slowly.
- **Proposed fix**: interval-gate the call (e.g. 1–2 s, matching the bottom-bar clock
  granularity) or early-return when `connected`+`rssi` are unchanged since last call.
- **Expected benefit**: removes constant background UI churn; measurable loop-time
  improvement visible in telemetry builds (`report_loop_timing`).
- **Risk level**: Low.
- **Validation**: telemetry build before/after loop-timing comparison; visually confirm
  the icon still appears/disappears within ~2 s of connect/disconnect.
- **First PR-safe?**: Yes.

### E4 — `prefs_save()` rewrites every key

- **Evidence**: `src/hal/prefs.cpp` `prefs_save()` writes ~40 NVS keys unconditionally on
  every call.
- **Problem**: mostly theoretical — ESP-IDF NVS skips writes when the value is unchanged,
  so flash wear is likely a non-issue, but each call still costs ~40 NVS lookups.
- **Proposed fix**: none urgently; if profiling shows cost, add a dirty-flag or per-group
  save. Listed mainly so future work doesn't assume per-key saves exist.
- **Expected benefit**: marginal.
- **Risk level**: Low; **First PR-safe?**: defer (verify first — see Open Questions).

---

## Reliability Improvements

(R4 boot hang and R7 contacts format above are the two largest; these are additional.)

### L1 — Retire or fence the blocking WiFi `connect()`

- **Evidence**: `src/hal/wifi_ota.cpp:286-310` — `wifi_sta::connect()` busy-waits up to
  15 s with `delay(200)`. No caller exists in `src/` today (`main.cpp:128` and
  `screens.cpp:6995` both use `beginConnect()`).
- **Problem**: a public, exported 15-second blocking call is a landmine — if a future
  screen/agent PR calls it from an LVGL event handler, the UI and mesh loop freeze for up
  to 15 s (and the LVGL watchdog-less main loop just stalls).
- **Proposed fix**: either delete it (preferred if truly unused — verify validation builds
  don't link it) or mark it `[[deprecated("use beginConnect()")]]` with a comment.
- **Expected benefit**: removes a foot-gun; zero behavior change.
- **Risk level**: Low.
- **Validation**: `pio run` across the env matrix (`scripts/smoke_build_matrix.py
  --profile roadmap`) to confirm nothing links it; `pio test -e native_test`.
- **First PR-safe?**: Yes (deprecation form); deletion after the matrix check.

### L2 — Crash-reporting gap is known but unscheduled

- **Evidence**: `src/diagnostics/telemetry_crash.cpp:86` — `// FIXME: Replace with
  esp_panic_handler_register_with_id() to capture …` (documented limitation, see also
  commit `5bc7842 chore: document crash handler backtrace limitation`).
- **Problem**: panics currently lose backtrace detail that would make field-failure triage
  far faster.
- **Proposed fix**: implement the noted panic-handler registration in a telemetry-build
  iteration; out of scope here, but it belongs on this roadmap so it isn't only a code
  comment.
- **Risk level**: Medium (panic-path code must be minimal and IRAM-safe).
- **Validation**: forced-crash test (`abort()` behind a debug serial command) and verify
  the stored crash record on next boot.
- **First PR-safe?**: Later work.

### L3 — Watchdog interaction of long blocking flash operations

- **Evidence**: `factoryReset()` (`src/mesh/mesh_wrapper.cpp`) calls `SPIFFS.format()`
  synchronously; GitHub OTA `Update.end(true)` finalization (`src/hal/github_ota.cpp:353`)
  also blocks.
- **Problem**: whether these can trip a task watchdog (or starve LVGL long enough to look
  like a hang) on real hardware is **unverified** — flagged as an open question rather than
  asserted, because Arduino-ESP32 WDT defaults vary by core version.
- **Proposed fix**: measure first (debug build + serial timestamps around both paths);
  only then decide whether to feed/disable the WDT or show a "please wait" screen state.
- **First PR-safe?**: Measurement task only.

---

## Maintainability Improvements

- **M1 — Split `screens.cpp`** — see R2. The single highest-leverage maintainability item.
- **M2 — Then `chat_screen.cpp` and `mesh_wrapper.cpp`**: same incremental treatment once
  M1 establishes the pattern (`src/ui/chat_screen.cpp` 2,862 lines mixes channel state,
  message cache, search, and rendering; `src/mesh/mesh_wrapper.cpp` 2,314 lines mixes API
  surface, persistence, and packet logging — persistence is the natural first extraction,
  and it dovetails with R7).
- **M3 — Documentation refresh** — see R3; includes adding a short "which doc is
  authoritative for what" index (this file vs `ROADMAP.md` vs `AUDIT.md` vs
  `AGENT_GUIDE.md`, the last of which is auto-synced per its header).
- **M4 — Untrack `platformio.local.ini`** — see R6.
- **M5 — Trivial hygiene** (bundle into any Phase 1 PR): duplicate
  `#include "../hal/display.h"` in `src/ui/screens.cpp:34,36`; boot-log step numbering in
  `src/main.cpp` jumps from "step 4" to "step 6" (no step 5), which makes
  `CONTRIBUTING.md`'s "all `[boot] step N:` messages appear in sequence" check misleading.
- **M6 — Add a `.clang-format`**: `CONTRIBUTING.md` documents conventions ("no
  `.clang-format` file — conventions are listed below"). Encoding them in a config (applied
  only to *new/changed* lines, never a big-bang reformat that would destroy blame) makes
  agent contributions consistent. Risk: low; first-PR-safe: yes (config file + docs note,
  no reformat commit).

---

## Build / CI / Release Improvements

- **B1 — Compile firmware on PRs** — see R1 (highest priority in this section).
- **B2 — Pin dependencies exactly** — see R5.
- **B3 — Align the two workflows**: `pr-ci.yml` uses Python 3.12 and caches
  `~/.platformio/{.cache,packages,platforms}`; `build-release.yml` uses Python 3.11 and
  caches all of `~/.platformio`. Harmonize versions and cache strategy so PR-green reliably
  predicts release-green. Risk: low; protected-file PR.
- **B4 — Checksums on release assets**: `build-release.yml` uploads `firmware.bin` /
  `firmware-merged.bin` without digests, while `scripts/merge_bin.py` already computes
  SHA-256 for the webflasher manifest. Add a `sha256sum` step and attach a checksums file
  to the GitHub release. (Also called for in `docs/ROADMAP.md` "release operations".)
- **B5 — Tag/version consistency check**: the release flow sets `SIGURDOS_VERSION` in
  `src/hal/tdeck_pins.h` manually (per `CLAUDE.md` release steps). Add a release-workflow
  step that fails if the tag name and `SIGURDOS_VERSION` disagree.
- **B6 — Stop growing git with binaries (decision needed)**: `firmware/*.bin` (~4.1 MB) is
  committed per release; `.git` is already 59 MB. Options: Git LFS, or distribute solely
  via GitHub Releases + webflasher manifest pointing at release assets.
  **Constraint**: must not break the web-flasher hosting flow documented in
  `firmware/README.md` — owner decision (Open Questions).
- **B7 — Static analysis job**: add `pio check` (cppcheck) as a non-blocking CI job to
  start; promote to blocking once the baseline is triaged. Catches exactly the
  null-deref/buffer classes listed in the project's own audit checklist.
- **B8 — Use `scripts/smoke_build_matrix.py` in CI**: the script and its profiles already
  exist but nothing invokes them in CI; a nightly `--profile roadmap` run would catch
  breakage of the telemetry/remote-test envs that PR CI (even with B1) won't build.

---

## Testing and Validation Plan

What exists: 56 native suites / 744 cases (mocked HAL, LVGL, RadioLib in `test/mocks/`),
remote-test serial controller, validation harnesses (`scripts/validation/*.py`), and
hardware-evidence docs (`docs/RC2_HARDWARE_VALIDATION.md`, `docs/GPS_HARDWARE_VALIDATION.md`,
`docs/BLE_HARDWARE_VALIDATION.md`). Hardware-gap closure (RF interop, OTA negative tests,
sleep/wake, soak) is already tracked in `docs/ROADMAP.md` — not duplicated here.

Additions this roadmap proposes:

| Gap | Proposal | Phase |
| --- | --- | --- |
| Firmware compile not tested on PRs | B1 build job | 5 |
| Persistence corruption untested | R7 suite: round-trip, legacy, truncated, oversized-count | 2 |
| Buzzer timing untested non-blockingly | E1: tick-based tests in `test/test_buzzer` | 3 |
| Debug/test envs can rot silently | B8 nightly smoke matrix | 5 |
| Static analysis absent | B7 `pio check` job | 5 |
| Doc/code reference rot | R3: CI grep that doc-cited paths exist | 5 |
| Size regressions invisible | Report RAM/flash from `pio run` output as a PR comment or job-summary line; alert on >1% jump | 5 |
| Boot-time regressions invisible | E2: timestamped `[boot] step` deltas recorded in validation docs per release | 6 |

Standard validation commands (unchanged from `CONTRIBUTING.md`):

```bash
pio test -e native_test -v          # all native tests
pio run -e SigurdOS_TDeck           # release firmware build
pio run -e SigurdOS_TDeck_debug     # debug build for hardware boot-log checks
python scripts/smoke_build_matrix.py --profile roadmap   # env matrix
```

---

## Risk Matrix

| ID | Item | Likelihood of harm today | Impact if it bites | Fix effort | Fix risk | Phase |
| --- | --- | --- | --- | --- | --- | --- |
| R1/B1 | PR CI skips firmware build | High (every PR) | Broken dev branch builds | S | Low | 5* |
| R2/M1 | `screens.cpp` monolith | High (every UI PR) | Conflicts, slow review | L (phased) | Medium | 4 |
| R3/M3 | Agent doc drift | High (every agent session) | Misdirected work | S | Low | 1 |
| R4 | Display-init hang | Low | Dead field unit | S | Low | 2 |
| R5/B2 | Floating dep versions | Medium | Irreproducible builds | S | Low | 1 |
| R6/M4 | Tracked local ini | Medium | Ambiguous build flags | S | Low | 1 |
| R7 | Unversioned contacts format | Low–Medium | Silent data corruption | M | Medium | 2 |
| R8 | Manual logging policy | Medium | Release log noise | M (phased) | Low | 2 |
| E1 | Blocking buzzer | Medium | UI/mesh stalls | S | Low | 3 |
| E2 | Boot delays | Certain (1.8 s) | Slow boot only | M | Medium (hw races) | 3 |
| E3 | Per-loop label rewrite | Certain | CPU/LVGL churn | S | Low | 3 |
| L1 | Blocking `connect()` foot-gun | Low | 15 s UI freeze | S | Low | 1 |
| L2 | Crash backtrace gap | Medium | Slow triage | M | Medium | 6 |
| B6 | Binaries in git | Certain (repo growth) | Clone size | M | Medium (flasher) | 5 |

\* R1 is Phase 5 by category but is recommended as one of the **first** PRs chronologically
(it's small, protected-file-isolated, and makes every later phase safer).

---

## Prioritized Roadmap

### Phase 0 — Baseline build and behavior verification (before any change)

- `pio test -e native_test -v` → expect 744 cases / 743 pass / 1 skip (2026-06-09 baseline).
- `pio run -e SigurdOS_TDeck` → record RAM/flash (% baseline: 33.8% / 30.1% per
  `docs/AUDIT.md` 2026-06-06 refresh) — re-record at current HEAD.
- `python scripts/smoke_build_matrix.py --profile roadmap` → record per-env pass/fail.
- Debug build on hardware: capture a timestamped boot log per `CONTRIBUTING.md` "Hardware
  Testing" and archive it as the behavior baseline (boot steps, mesh init line).

### Phase 1 — Low-risk cleanup (each its own small PR)

1. R3 doc refresh (non-protected docs); flag `CLAUDE.md`/`AGENTS.md` lines to owner.
2. R5 exact-pin `lib_deps`.
3. R6 untrack `platformio.local.ini` (after owner confirms canonical flags).
4. L1 deprecate (or remove, post-matrix-check) blocking `wifi_sta::connect()`.
5. M5 hygiene: duplicate include; boot-step renumbering.
6. M6 `.clang-format` config (no reformat commit).

### Phase 2 — Error handling and recovery hardening

1. R4 display-init retry + restart.
2. R7 versioned contacts persistence with legacy-read path + new tests (coordinate with
   `docs/ROADMAP.md` message-store unification).
3. R8 logging macros: header + pilot file, then file-by-file migration.
4. L3 measurement: WDT/stall characterization of `SPIFFS.format()` and OTA finalize.

### Phase 3 — Performance and memory improvements

1. E3 interval-gate `update_wifi_status()`.
2. E1 non-blocking buzzer.
3. E2 boot-delay reduction — hardware-gated, both board revisions, per the validation
   recipe above.
4. E4 only if profiling justifies it.

### Phase 4 — Architecture / module cleanup

1. M1 extract `screens_common.cpp/h`, then split `screens.cpp` one screen per PR
   (suggested order: smallest/leaf screens first — Packets, Advertise, Signal — ending
   with Settings family).
2. M2 extract persistence from `mesh_wrapper.cpp` (pairs with R7); then factor
   `chat_screen.cpp` (state vs rendering).

### Phase 5 — Build, CI, and release hardening (protected-file PRs, one each)

1. B1 PR firmware-build job (recommend doing this first chronologically).
2. B3 workflow alignment; B4 release checksums; B5 tag/version check.
3. B7 `pio check` non-blocking; B8 nightly smoke matrix; size-delta reporting; R3 doc-path
   CI grep.
4. B6 binary-distribution decision + migration (owner-led).

### Phase 6 — Regression and hardware validation

- Re-run Phase 0 baseline and diff: test count/pass, RAM/flash, boot-log steps/timing.
- Remote-test smoke across all screens (`scripts/validation/remote_test_smoke.py`).
- The hardware gates already enumerated in `docs/ROADMAP.md` (RF interop, OTA
  positive/negative, SD/map, sleep/wake, soak) remain the release bar; L2 crash-handler
  work lands here.

---

## Open Questions

1. **`platformio.local.ini` intent (R6)**: is `-D SIGURDOS_DEBUG_MESH=1` meant to be part
   of the canonical `SigurdOS_TDeck_remote_test_radio` env, or genuinely local? Verify
   with the owner; check which flag set recent radio-debug sessions actually used.
2. **Boot serial delays (E2)**: can the 250 ms + 500 ms in `src/main.cpp:35-37` be reduced
   or USB-attach-gated without breaking WebSerial/CDC enumeration? Verify empirically on
   both T-Deck revisions across 10+ boots each.
3. **WDT behavior (L3)**: do `SPIFFS.format()` (factory reset) or OTA `Update.end(true)`
   approach any watchdog limit on the current Arduino-ESP32 core? Measure with a
   debug build before changing anything.
4. **NVS write dedup (E4)**: confirm the ESP-IDF version in use skips identical-value
   writes (read `nvs_set_*` source for the pinned core) before deciding `prefs_save()`
   needs a dirty-flag.
5. **`firmware/*.bin` distribution (B6)**: does the web-flasher hosting
   (`firmware/README.md` references `flasher.sigurdos.dev`) fetch from the git repo or
   from release assets? The answer determines whether the binaries can leave the repo.
6. **Blocking `wifi_sta::connect()` (L1)**: retained intentionally for a validation build
   or future use? `grep -rn "wifi_sta::connect" src/` finds no callers — confirm across
   all build envs before deleting.
7. **`AGENT_GUIDE.md` sync job**: commit `23c4751` is tagged `[auto]`, but the sync
   mechanism isn't visible in `.github/workflows/`. Where does it run, and should it be
   documented (or brought into the repo) so it survives infrastructure changes?
8. **Native test runtime budget**: the suite went from ~4.5 min (audit baseline) to
   8.3 min (683 cases, audit refresh) and back to ~4.4 min (744 cases, this baseline,
   different machine). Is there a CI time budget at which suite sharding/parallelism
   should be introduced?
9. **8 MB flash variants**: `boards/t-deck.json` hard-codes 16 MB
   (`default_16MB.csv`). Are smaller-flash T-Deck units in scope for support? If yes,
   partition-table variants and OTA-slot sizing need a design pass.
