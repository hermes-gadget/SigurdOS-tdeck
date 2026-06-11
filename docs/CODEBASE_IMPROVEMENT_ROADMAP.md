# SigurdOS T-Deck — Codebase Improvement Roadmap

Version: 2 (hardened for agent implementation)
Date: 2026-06-09
Baseline: `dev` at `064e9c9` (`docs: add Launcher compatibility roadmap (#568)`)
Related issues: [#569](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/569) (initial roadmap),
[#571](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/571) (this hardening pass)

This document is an **implementation playbook**: a prioritized, phased plan for making the
codebase more efficient, reliable, maintainable, and robust **without removing or changing
existing behavior**. It is written so that a junior engineer or a less capable AI agent can
implement each task without guessing. It complements (and does not replace):

- [`docs/ROADMAP.md`](ROADMAP.md) — the feature/product roadmap (companion parity, hardware
  validation gates, release readiness).
- [`docs/AUDIT.md`](AUDIT.md) — the 2026-06-07 point-in-time end-to-end audit.
- [`docs/KNOWN_ISSUES.md`](KNOWN_ISSUES.md) — the live tracker for observed bugs.

---

## 0. Implementation Rules for Future Agents

Read this section before implementing **any** task below. These rules are binding.

1. **Read `CONTRIBUTING.md` and `CLAUDE.md`/`AGENTS.md` first.** The contribution workflow
   (issue-first, PR template, testing declaration) applies to every task here.
2. **One task = one PR.** Never bundle tasks, even small ones, unless a task explicitly
   says "bundle with".
3. **Never touch protected files in a task PR.** Per `CONTRIBUTING.md` "Protected Files",
   these require their own dedicated PR: `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md`,
   `docs/KNOWN_ISSUES.md`, `docs/MISSING_FEATURES.md`, `.github/workflows/*`.
   Tasks T20–T24 are workflow changes and are sized as dedicated PRs for this reason.
   **Never edit `CLAUDE.md` or `AGENTS.md` at all** — owner-only.
4. **Line numbers in this document are anchors, not gospel.** They were recorded at commit
   `064e9c9`. The codebase moves. Every task includes a *relocation grep* — run it first
   and work from what you find. If the grep returns nothing, or returns something
   materially different from the Evidence description, **stop and flag it** in the PR/issue
   instead of improvising.
5. **Do not start a task whose "Depends on" tasks are not merged.**
6. **Do not start a task marked "Blocked on OQ-n"** until the owner has answered that open
   question (in the issue tracker or in this document).
7. **Run the baseline before changing anything**: `pio test -e native_test -v` must pass
   and `pio run -e SigurdOS_TDeck` must build *before* your change, so you can tell whether
   you broke something or it was already broken.
8. **Behavior preservation is the contract.** Every task lists "Behavior that must not
   change". If your implementation cannot satisfy that list, stop and report — do not
   reinterpret the requirement.
9. **If any validation step fails, stop.** Report the failure output in the PR. Do not
   "fix forward" into unrelated code.
10. **Never flash or rebuild the device into `SigurdOS_TDeck_remote_test` mode without
    explicit user/owner consent** (rule from `CLAUDE.md` — that build disables the LoRa
    radio).
11. **Do not commit unrelated working-tree changes.** Stage only the files your task names
    (`git add <specific paths>` — never `git add -A`).
12. **PR body must declare hardware testing** ("Remote test", "Physical hardware test",
    "Both", or for docs/CI-only changes "Not applicable — no firmware change") per
    `CONTRIBUTING.md`.

---

## 1. Executive Summary

SigurdOS T-Deck is a healthy, actively maintained firmware with an unusually strong native
test suite (56 suites, 744 cases, run on every PR), strict warnings, an issue-first
workflow, and a steady stream of focused bug-fix PRs (#543–#568). Static RAM use dropped
from 86.4% to 33.8% after the PSRAM pool work (`docs/AUDIT.md`).

The highest-leverage improvements are not features:

1. **CI does not compile the firmware on PRs** (`.github/workflows/pr-ci.yml` runs only
   native tests) — task T20.
2. **`src/ui/screens.cpp` is a 7,829-line monolith** (~30 screens, 88 file-static
   globals) — tasks T14–T18.
3. **Agent-facing documentation has drifted from the code** — task T5 + owner actions.
4. **Reliability gaps**: boot hangs forever on display-init failure (T7), unversioned
   contacts persistence (T8), buzzer patterns block the main loop (T12).
5. **Reproducibility gaps**: caret version ranges in `lib_deps` (T1).

All tasks below are incremental, behavior-preserving, and individually revertable.

---

## 2. Current Codebase Strengths (do not regress these)

- **Native test coverage**: 56 `test/test_*/` suites, 744 cases (743 pass, 1 skip,
  ~4.5 min — 2026-06-09 baseline), mocked hardware in `test/mocks/`, run on every PR
  (`.github/workflows/pr-ci.yml`).
- **Strict warnings**: `-Wall -Wextra` in `platformio.ini` `[common] build_flags_common`.
- **Memory discipline**: PSRAM-first with DRAM fallback (`src/hal/lv_pool.cpp:36-39`,
  `src/app/map_renderer.cpp:116-117`, `:617-619`).
- **Input robustness**: NMEA checksum validation (`src/hal/gps.cpp:269-299`,
  `sigurdos_gps_checksum_failures()` at `gps.cpp:403`).
- **Secure OTA**: `WiFiClientSecure::setCACert(GITHUB_ROOT_CA)`
  (`src/hal/github_ota.cpp:202-203`, `:282-283`); no `setInsecure()` anywhere.
- **Non-blocking boot networking**: `wifi_sta::beginConnect()` (`src/hal/wifi_ota.cpp:245`)
  used at `src/main.cpp:128` and `src/ui/screens.cpp:6995`.
- **Flash-write hygiene**: settle delays before sleep/restart in `shutdown()` /
  `factoryReset()` (`src/mesh/mesh_wrapper.cpp`); factory reset wipes NVS namespaces,
  SPIFFS, and calls `nvs_flash_erase()`.
- **Graceful degradation**: touch/keyboard init failure does not halt boot
  (`src/hal/display.cpp`, after the LVGL indev setup).
- **Release tooling**: `scripts/merge_bin.py` (merges with `flash_mode: keep`, generates
  `webflasher/manifest.json` with SHA-256 digests); `scripts/build_metadata.py` (embeds
  `git describe` version + SHA + dirty flag).
- **Shared-bus correctness**: SPI2_HOST singleton (`src/hal/spi_shared.cpp`, PR #546).
- **Test/debug infra**: remote-test serial controller (`src/test/test_controller.cpp`),
  telemetry builds, per-feature debug envs, validation harnesses (`scripts/validation/`).

---

## 3. Phase 0 — Baseline Verification (run before the first task PR)

Record all of the following in the implementation issue before any change is merged:

```bash
# 1. Clean clone state
git submodule update --init --recursive
git rev-parse HEAD && git -C lib/meshcore rev-parse HEAD

# 2. Native tests (2026-06-09 baseline: 744 cases — 743 pass, 1 skip, 00:04:26)
pio test -e native_test -v

# 3. Release build — record RAM/flash percentages from the output
#    (2026-06-06 baseline per docs/AUDIT.md: RAM 33.8%, flash 30.1%)
pio run -e SigurdOS_TDeck

# 4. Resolved dependency versions (needed by T1)
pio pkg list -e SigurdOS_TDeck

# 5. Env matrix (script exists; not yet in CI)
python scripts/smoke_build_matrix.py --profile roadmap
```

Hardware baseline (owner/maintainer, physical device):
capture a debug-build boot log per `CONTRIBUTING.md` "Hardware Testing" and archive it.
The boot log to preserve (from `src/main.cpp`, `SIGURDOS_DEBUG_UI` builds): steps
1,2,3,4,6,7,8,(9),10 — note the numbering currently skips 5; T4 renumbers it.

---

## 4. Recommendations

Each task uses this template: Evidence → Problem → Proposed fix (step-by-step) →
Behavior that must not change → Edge cases → Expected benefit → Risk → Validation →
First PR or later → Depends on.

Task IDs (T1…T27) are stable; use them in branch names (`fix/T7-display-init-retry`)
and PR titles.

---

### Phase 1 — Low-risk cleanup (safe first PRs)

---

#### T1: Pin exact library dependency versions

**Evidence**

- File: `platformio.ini`
- Section: `[common]` → `lib_deps_common`
- Relocation grep: `grep -n "lib_deps_common" platformio.ini`
- Why: four libraries use caret ranges while `lvgl @ 9.3.0`, the platform
  (`platformio/espressif32@6.11.0`), and `googletest @ 1.17.0` are exact. Resolved
  versions on 2026-06-09 (`pio pkg list -e SigurdOS_TDeck`):
  `Adafruit BusIO 1.17.4`, `Crypto 0.4.0`, `LovyanGFX 1.2.21`, `RadioLib 7.7.1`.

**Problem**
Caret ranges (`^7.6.0` etc.) mean two clean clones at different times can build different
library versions — non-reproducible builds and surprise breakage from upstream minors.

**Proposed fix**

1. Run `pio pkg list -e SigurdOS_TDeck` on a clean checkout and note the resolved version
   of each caret-ranged library (do **not** blindly copy the versions above — re-resolve).
2. Edit `platformio.ini` `lib_deps_common`, replacing each caret range with the exact
   resolved version, e.g.:
   - `jgromes/RadioLib @ ^7.6.0` → `jgromes/RadioLib @ 7.7.1`
   - `rweather/Crypto @ ^0.4.0` → `rweather/Crypto @ 0.4.0`
   - `lovyan03/LovyanGFX @ ^1.2.0` → `lovyan03/LovyanGFX @ 1.2.21`
   - `adafruit/Adafruit BusIO @ ^1.16.1` → `adafruit/Adafruit BusIO @ 1.17.4`
3. Do not touch `SPI`, `Wire`, `lvgl`, or `file://lib/meshcore`.
4. No new helper files.

**Behavior that must not change**
The built firmware. Pinning to the already-resolved versions produces an identical build
configuration.

**Edge cases to preserve**

- RadioLib is *also* a transitive dependency of the MeshCore submodule
  (`lib/meshcore` declares `jgromes/RadioLib @ ^7.6.0`; resolution shows it nested under
  `MeshCore @ 1.10.0`). The exact pin `7.7.1` satisfies that constraint. If you pin a
  version outside MeshCore's range, the build will fail dependency resolution — don't.
- `[env:native_test]` has its own `lib_deps` (googletest only) — leave it alone.

**Expected benefit**: reproducible builds across machines and time; stable CI caches.
**Risk level**: Low.
**Validation**

```bash
rm -rf .pio
pio run -e SigurdOS_TDeck        # must succeed; RAM/flash within 0.1% of baseline
pio test -e native_test          # must pass
pio pkg list -e SigurdOS_TDeck   # all four libraries at the exact pinned versions
```

**First PR or later?** Safe first PR — config-only, reverts cleanly.
**Depends on**: nothing.

> **Status (2026-06-11): ✅ Complete** — branch `roadmap/T1-pin-deps`.
> Files changed: `platformio.ini` (four caret ranges → exact pins: RadioLib 7.7.1,
> Crypto 0.4.0, LovyanGFX 1.2.21, Adafruit BusIO 1.17.4; re-resolved on a clean
> `.pio` before pinning, matching the versions recorded above).
> Validation: `rm -rf .pio && pio run -e SigurdOS_TDeck` SUCCESS — RAM 40.7%
> (133,472 B) / Flash 39.2% (2,569,853 B), byte-identical to the pre-change
> baseline; `pio test -e native_test` 749 cases (748 pass / 1 skip);
> `pio pkg list -e SigurdOS_TDeck` resolves all four at the exact pins
> (RadioLib appears nested under MeshCore @ 1.10.0, as the Evidence predicted —
> 7.7.1 satisfies the submodule's `^7.6.0`). Hardware: not applicable — build
> configuration only, resolved versions unchanged.

---

#### T2: Add a `.clang-format` config (no reformat)

**Evidence**

- File: `CONTRIBUTING.md` → "Style Guide": "no `.clang-format` file — conventions are
  listed below" (2 spaces, camelCase functions, PascalCase classes, ~100 col).
- Relocation grep: `ls .clang-format` (should not exist yet).

**Problem**
Style conventions exist only as prose; agents and contributors cannot apply them
mechanically, and reviewers re-litigate formatting.

**Proposed fix**

1. Create `.clang-format` at the repo root with at minimum:

   ```yaml
   BasedOnStyle: Google
   IndentWidth: 2
   ColumnLimit: 100
   PointerAlignment: Left
   DerivePointerAlignment: false
   SortIncludes: false        # include order is load-bearing in HAL files
   ```

2. Do **not** run `clang-format` over the tree. Do not add a CI format check yet.
3. Add one sentence to the task PR description noting the config is for *new/changed
   lines only* (editor integration), and that a repo-wide reformat is explicitly out of
   scope (it would destroy `git blame`).

**Behavior that must not change**: all source files — zero code diffs in this PR.
**Edge cases to preserve**: `SortIncludes: false` is required — e.g. `src/lv_conf.h`
macros and HAL include ordering are order-sensitive.
**Expected benefit**: consistent style on new code without churn.
**Risk level**: Low.
**Validation**: `git diff --stat` shows exactly one added file; `pio test -e native_test`
passes (nothing compiled changed).
**First PR or later?** Safe first PR.
**Depends on**: nothing. (Note: `CONTRIBUTING.md` itself is protected — do *not* edit it
in this PR; if its prose should mention the config, file that as an owner follow-up.)

> **Status (2026-06-11): ✅ Complete** — branch `roadmap/T2-clang-format`.
> Files changed: `.clang-format` (new file only — zero code diffs; config keys
> per the task plus comments stating the new/changed-lines-only intent and the
> `SortIncludes: false` rationale). No repo-wide reformat performed and no CI
> format check added, per the task. Validation: exactly one added file;
> `pio test -e native_test` 749 cases (748 pass / 1 skip — nothing compiled
> changed). Hardware: not applicable. Owner follow-up: CONTRIBUTING.md's
> "no `.clang-format` file" prose is now stale — protected file, owner edit.

---

#### T3: Deprecate the blocking `wifi_sta::connect()`

**Evidence**

- Files: `src/hal/wifi_ota.h` (declaration, line 97: `bool connect(const char* ssid,
  const char* password);` with comment "Blocks up to 15s"), `src/hal/wifi_ota.cpp`
  (definition, line 286 — busy-waits with `delay(200)` up to 15 s).
- Callers: none. `git grep -n "wifi_sta::connect"` → only the definition;
  `src/main.cpp:128` and `src/ui/screens.cpp:6995` use `beginConnect()`.
- Relocation grep: `grep -rn "bool connect(" src/hal/wifi_ota.h src/hal/wifi_ota.cpp`

**Problem**
A public 15-second blocking call is a foot-gun: if any future PR calls it from an LVGL
event handler, the UI and `sigurdos::mesh::loop()` freeze for up to 15 s.

**Proposed fix**

1. Edit `src/hal/wifi_ota.h`: directly above the `bool connect(...)` declaration add
   `[[deprecated("Blocks up to 15 s — use beginConnect()/getStatus() instead")]]`.
2. Do **not** delete the function or change `src/hal/wifi_ota.cpp` — deletion is a later
   task gated on OQ-6 and a full env-matrix link check.
3. No other edits.

**Behavior that must not change**: everything — the attribute only emits a compile
warning *if* someone calls the function; nothing calls it today, so warning output must
be unchanged.
**Edge cases to preserve**: validation builds
(`SigurdOS_TDeck_gps_validation*`, `*_ble_validation`) must still compile —
they don't call it either, but confirm via the smoke matrix.
**Expected benefit**: future misuse becomes visible at compile time.
**Risk level**: Low.
**Validation**

```bash
pio run -e SigurdOS_TDeck 2>&1 | grep -i "deprecat"   # expect no output (no callers)
pio test -e native_test
python scripts/smoke_build_matrix.py --profile roadmap
```

**First PR or later?** Safe first PR.
**Depends on**: nothing.

> **Status (2026-06-11): ✅ Complete** — branch `roadmap/T3-deprecate-connect`.
> Files changed: `src/hal/wifi_ota.h` (one `[[deprecated(...)]]` line above the
> `bool connect(...)` declaration; definition in `wifi_ota.cpp` untouched per step 2).
> Validation: `pio run -e SigurdOS_TDeck 2>&1 | grep -ci deprecat` → **0** (no
> callers, warning output unchanged); RAM 40.7% / Flash 39.2% byte-identical;
> `pio test -e native_test` 749 cases (748 pass / 1 skip);
> `smoke_build_matrix --profile roadmap` all 4 envs PASS (includes the
> validation-build edge case). Hardware: not applicable — attribute-only change,
> binary identical. Deleting the function remains gated on OQ-6.

---

#### T4: Source hygiene — duplicate include + boot-step renumbering

**Evidence**

- `src/ui/screens.cpp:34` and `:36` both contain `#include "../hal/display.h"`.
  Relocation grep: `grep -n '#include "../hal/display.h"' src/ui/screens.cpp`
  (expect 2 hits before the fix).
- `src/main.cpp` boot logging (under `#if SIGURDOS_DEBUG_UI`) numbers steps
  1, 2, 3, 4, then jumps to 6, 7, 8, (9 under `SIGURDOS_DEBUG_DIAG`), 10 — there is no
  "step 5". Relocation grep: `grep -n '\[boot\] step' src/main.cpp`.
  Verified: no script parses these numbers (`grep -rn "boot\] step" scripts/` → empty),
  but `CONTRIBUTING.md` tells maintainers to check that "all `[boot] step N:` messages
  appear in sequence", which the gap defeats.

**Problem**
Trivial wart + a misleading debug sequence that makes the documented boot-log check
ambiguous ("is step 5 missing because of a fault or by design?").

**Proposed fix**

1. `src/ui/screens.cpp`: delete one of the two duplicate `#include "../hal/display.h"`
   lines (keep the first).
2. `src/main.cpp`: renumber the boot-step *strings only* so they are sequential
   (`step 6`→`step 5`, `step 7`→`step 6`, `step 8`→`step 7`, `step 9`→`step 8`,
   `step 10`→`step 9`). Strings appear only inside `Serial.println` literals.
3. Before step 2, re-verify nothing parses step numbers:
   `grep -rn "step 1\?[0-9]" scripts/ test/ docs/ --include="*.py" --include="*.cpp"` —
   if any hit consumes these numbers, stop and flag instead.
4. Bundle with: nothing else.

**Behavior that must not change**: release builds (the strings are compiled only under
`SIGURDOS_DEBUG_UI`/`SIGURDOS_DEBUG_DIAG`); the *order* of boot operations; every
non-string character of `main.cpp`.
**Edge cases to preserve**: the `step 9`(→`8`) line is inside `#if SIGURDOS_DEBUG_DIAG`
— keep the renumbered strings consistent in builds where that block is compiled out
(sequence 1–8 then 9 for SD; acceptable: renumber assuming all blocks present — gaps when
a feature block is compiled out are expected and were already possible).
**Expected benefit**: boot-log sequence check becomes meaningful; one less wart.
**Risk level**: Low.
**Validation**

```bash
pio test -e native_test
pio run -e SigurdOS_TDeck
pio run -e SigurdOS_TDeck_debug    # compiles the renumbered strings
```

Then (maintainer, hardware): debug-build boot log shows `step 1..9` in order.
**First PR or later?** Safe first PR.
**Depends on**: nothing.

> **Status (2026-06-11): ✅ Complete** — branch `roadmap/T4-hygiene`.
> Files changed: `src/ui/screens.cpp` (deleted the second duplicate
> `#include "../hal/display.h"`, kept the first), `src/main.cpp` (boot-step
> strings renumbered 6→5, 7→6, 8→7, 9→8, 10→9 — string literals only).
> Pre-edit check: no script/test/doc consumes step numbers (step-parse grep
> empty). Validation: `pio test -e native_test` 749 cases (748 pass / 1 skip);
> `pio run -e SigurdOS_TDeck` RAM 40.7% / Flash 39.2% **byte-identical**
> (strings are compiled only under `SIGURDOS_DEBUG_UI`/`_DIAG`);
> `pio run -e SigurdOS_TDeck_debug` SUCCESS (compiles the renumbered strings).
> Hardware: boot-log sequence check (`step 1..9` in order) left to the
> maintainer's next debug-build flash — release behavior unchanged.

---

#### T5: Refresh drifted non-protected docs + owner action list

**Evidence** (all at `064e9c9`)

- `docs/AUDIT.md:53` cites `pio run -e SigurdOS_TDeck_ble`;
  `docs/BLE_HARDWARE_VALIDATION.md` cites it at lines 22, 221, 283, 327, 377.
  No such env exists: `grep -n "SigurdOS_TDeck_ble" platformio.ini` → only
  `SigurdOS_TDeck_ble_validation`. BLE is in the base env via
  `-D SIGURDOS_COMPANION_BLE=1` (`platformio.ini` `[env:SigurdOS_TDeck]`).
- `CLAUDE.md`/`AGENTS.md` drift (owner-only files — **do not edit**):
  line 89 documents `src/mesh/slop_mesh.h` (file does not exist; `src/mesh/` has
  `mesh_wrapper.*`, `sigurd_mesh_v2.*`, `message_store.*`, `regions.*`,
  `channel_validation.*`); "Main + dev branch model" contradicts `CONTRIBUTING.md`
  ("There is no `main` branch"); issues are directed to `hermes-gadget/SlopOS-tdeck`
  (origin is `hermes-gadget/SigurdOS-tdeck`); the Gotchas table still claims "GPS NMEA —
  no checksum validation" (implemented at `src/hal/gps.cpp:269-299`).

**Problem**
Stale docs actively misdirect the AI agents this project onboards.

**Proposed fix**

1. Edit `docs/AUDIT.md`: directly under the title, add a short "Historical note
   (2026-06-09)" stating that `SigurdOS_TDeck_ble` was folded into the default env
   (`SIGURDOS_COMPANION_BLE=1`) and that historical command citations are preserved
   as-run. Do **not** rewrite the historical evidence tables.
2. Edit `docs/BLE_HARDWARE_VALIDATION.md`: same note once at the top; leave per-run
   records untouched.
3. Create nothing else. `docs/KNOWN_ISSUES.md` and `docs/MISSING_FEATURES.md` are
   protected — no edits.
4. In the PR description, include the **owner action list** verbatim (the four
   `CLAUDE.md`/`AGENTS.md` corrections in Evidence above, with line anchors) so the owner
   can apply them in an owner-only PR.

**Behavior that must not change**: nothing compiled — docs only. Historical validation
evidence must remain verbatim (append notes, never alter recorded results).
**Edge cases to preserve**: `AGENT_GUIDE.md` is auto-synced (per its header) — do not
edit it; it will regenerate.
**Expected benefit**: agents stop building/citing a nonexistent env.
**Risk level**: Low.
**Validation**: docs render on GitHub; `grep -n "SigurdOS_TDeck_ble" docs/AUDIT.md`
still finds historical mentions plus the new note; native tests pass (nothing changed).
**First PR or later?** Safe first PR.
**Depends on**: nothing.

> **Status (2026-06-11): ✅ Complete** — branch `roadmap/T5-doc-refresh`.
> Scope drift found by relocation grep (flagged per Rule 4):
> `docs/BLE_HARDWARE_VALIDATION.md` was deleted in PR #574, so only
> `docs/AUDIT.md` received the historical note; `docs/FEATURES_OVERVIEW.md:248`
> carried the same stale `[env:SigurdOS_TDeck_ble]` citation and was corrected
> (one line — same drift class, non-protected). Historical evidence tables left
> verbatim. The four `CLAUDE.md`/`AGENTS.md` corrections (owner-only) were
> re-verified at current dev and listed in the PR description for an owner PR:
> `slop_mesh.h` (line 89, file doesn't exist), "Main + dev branch model"
> (line 379, contradicts CONTRIBUTING.md), `hermes-gadget/SlopOS-tdeck` repo
> references (lines 27/43/51/396/496/503; origin is SigurdOS-tdeck), and the
> "GPS NMEA — no checksum validation" gotcha (implemented at
> `src/hal/gps.cpp:269-299`, counter at `:403`).
> Validation: `pio test -e native_test` 749 cases (748 pass / 1 skip) — docs
> only. Hardware: not applicable.

---

#### T6: Untrack `platformio.local.ini` — **Blocked on OQ-1**

**Evidence**

- `platformio.local.ini` is tracked at `064e9c9` (header: "Local overrides for T-Deck —
  adds MESH_DEBUG") and redefines `[env:SigurdOS_TDeck_remote_test_radio]`, which also
  exists in `platformio.ini` — the tracked local copy adds `-D SIGURDOS_DEBUG_MESH=1`.
- `.gitignore` contains `.pio/` but not `platformio.local.ini`.
- Relocation: `git show HEAD:platformio.local.ini`; `grep -n "remote_test_radio" platformio.ini`.

**Problem**
A file whose purpose is *local* overrides is shared by every clone and silently forks one
env's flags from the canonical definition — bug reports can't tell which definition built
the binary.

**Proposed fix** (after the owner answers OQ-1):

1. If the owner wants `SIGURDOS_DEBUG_MESH=1` kept: add it to the canonical
   `[env:SigurdOS_TDeck_remote_test_radio]` in `platformio.ini` (it already sets
   `MESH_DEBUG=1`); otherwise skip this step.
2. `git rm --cached platformio.local.ini` (removes from tracking, keeps any local copy).
3. Append `platformio.local.ini` on its own line to `.gitignore`.

**Behavior that must not change**: flag set of every env after the change must equal
whichever set the owner declares canonical; all other envs byte-identical.
**Edge cases to preserve**: contributors with an existing local copy keep it (hence
`--cached`); PlatformIO loads `platformio.local.ini` automatically if present — document
that in the PR description.
**Expected benefit**: one source of truth for build flags.
**Risk level**: Low (after OQ-1 is answered).
**Validation**

```bash
pio run -e SigurdOS_TDeck_remote_test_radio   # builds
pio run -e SigurdOS_TDeck_remote_test_radio -v 2>&1 | grep -o "DSIGURDOS_DEBUG_MESH=1"
# expect: present or absent per the owner's OQ-1 answer
git status --short                            # platformio.local.ini no longer listed
```

**First PR or later?** Safe first PR once unblocked.
**Depends on**: OQ-1.

> **Status (2026-06-11): ⛔ Blocked on OQ-1** — no PR opened. The owner must decide
> whether `SIGURDOS_DEBUG_MESH=1` becomes canonical before the file can be untracked.

---

### Phase 2 — Error handling and recovery hardening

---

#### T7: Display-init failure: retry then restart instead of hanging forever

**Evidence**

- File: `src/main.cpp:65-68`:

  ```cpp
  if (!sigurdos_display_init()) {
      Serial.println("[boot] FATAL: Display init failed");
      while (1) delay(1000);
  }
  ```

- Relocation grep: `grep -n "Display init failed" src/main.cpp`

**Problem**
A transient display/SPI fault leaves the device in a silent infinite loop — radio and UI
never start; a field unit is dead until physically reset.

**Proposed fix**

1. Edit only this block in `src/main.cpp` `setup()`. Replace with retry + restart:

   ```cpp
   bool display_ok = false;
   for (int attempt = 0; attempt < 3 && !display_ok; attempt++) {
       if (attempt > 0) {
           Serial.printf("[boot] Display init retry %d/2\n", attempt);
           delay(200);
       }
       display_ok = sigurdos_display_init();
   }
   if (!display_ok) {
       Serial.println("[boot] FATAL: Display init failed after 3 attempts — restarting in 5 s");
       delay(5000);
       ESP.restart();
   }
   ```

2. Do not modify `sigurdos_display_init()` itself (`src/hal/display.cpp`) — verify first
   (relocation: `grep -n "bool sigurdos_display_init" src/hal/display.cpp`) that calling
   it twice is safe: it must not leak a previously-created LVGL display or double-init the
   bus. If inspection shows it is not re-entrant, **reduce scope**: keep a single attempt
   and replace only `while (1) delay(1000);` with the `delay(5000); ESP.restart();` tail.
   State in the PR which variant you shipped and why.
3. No new files.

**Behavior that must not change**: the success path (first-attempt success boots exactly
as today — zero added delay); the FATAL serial message still appears on failure.
**Edge cases to preserve**

- Permanently dead display → device now reboot-loops at ~5 s intervals instead of
  hanging. This is intentional (visible on serial, recoverable after transient faults);
  call it out in the PR description.
- `Serial` is already initialized at this point (line ~36) — messages will print.

**Expected benefit**: converts a permanent field hang into self-recovery for transient
faults and a diagnosable loop for hard faults.
**Risk level**: Low (variant 2) / Medium (variant 1 — needs the re-entrancy inspection).
**Validation**

```bash
pio test -e native_test
pio run -e SigurdOS_TDeck && pio run -e SigurdOS_TDeck_debug
```

Hardware (maintainer): temporarily make `sigurdos_display_init()` return `false` in a
debug build, flash, observe retries + restart over serial, then revert the test hack and
confirm a normal boot.
**First PR or later?** Later PR (Phase 2) — touches the boot path; needs the hardware
check.
**Depends on**: Phase 0 baseline boot log.

---

#### T8a: Extract contact persistence into a testable module (no format change)

**Evidence**

- File: `src/mesh/mesh_wrapper.cpp` — `saveContacts()` at line 1543, `loadContacts()` at
  line 1567, file path constant `CONTACTS_FILE = "/contacts"` just above.
- Current on-disk format (read it from the code, verify before building on this):
  raw `int` count (4 bytes, host endian), then per contact 66 bytes:
  `pub_key[32]`, `name[32]`, `type` (1), `perm` (1).
- `loadContacts()` checks only `n <= 0`; no magic, no version, no bound against
  `MAX_CONTACTS` (350, defined in `platformio.ini`).
- Model to copy: `src/mesh/message_store.cpp` + `src/mesh/message_store.h` — already
  compiled in native tests (`platformio.ini` `[env:native_test]` `build_src_filter`
  includes `+<mesh/message_store.cpp>`) with `#if defined(ESP32_PLATFORM)` SPIFFS vs
  stdio-fallback, and tested by `test/test_message_store/`.
- Why extraction first: `mesh_wrapper.cpp` is **not** compiled in `native_test`
  (mocked by `test/mocks/mock_mesh_wrapper.cpp`), so its serialization code is currently
  untestable off-device.
- Relocation greps: `grep -n "void saveContacts\|void loadContacts\|CONTACTS_FILE" src/mesh/mesh_wrapper.cpp`

**Problem**
Serialization logic lives inside an untestable 2,314-line wrapper; format hardening (T8b)
without tests would be blind.

**Proposed fix**

1. Create `src/mesh/contact_store.h` + `src/mesh/contact_store.cpp` mirroring the
   structure/conventions of `message_store.{h,cpp}` (same license header, same
   `ESP32_PLATFORM` split, namespace `sigurdos::mesh`).
2. Move the byte-level encode/decode of a contact record and the file read/write loops
   there, exposing functions that `saveContacts()`/`loadContacts()` call. Keep the
   `g_mesh->getContactByIdx`/`addContact` interaction in `mesh_wrapper.cpp` — the store
   operates on a plain struct (define a minimal POD in `contact_store.h`: pubkey, name,
   type, perm) so the native build needs no MeshCore types.
3. **Write byte-identical output.** The new code must produce exactly the bytes the old
   code produced (same order, same widths, no header yet — that is T8b).
4. Add `+<mesh/contact_store.cpp>` to `[env:native_test]` `build_src_filter` in
   `platformio.ini`.
5. Create `test/test_contact_store/main.cpp` (+ a `test_contact_store.cpp` if you follow
   the `test_buzzer` two-file layout): round-trip save/load, empty store, count
   mismatch/truncated file (loader keeps the records read before EOF — that is current
   behavior, lock it in), `n <= 0` rejection.

**Behavior that must not change**: on-disk bytes (verify: save with old code, save with
new code, `cmp` the files in a host-side test); contact count/order after load; the
"SPIFFS unavailable → silent no-op" behavior (`if (!SPIFFS.begin(false)) return;`).
**Edge cases to preserve**: 0 contacts → file is *removed* (`SPIFFS.remove`), not
written empty; name buffer always re-terminated on load (`c.name[31] = '\0'`);
`out_path_len = OUT_PATH_UNKNOWN` and `shared_secret_valid = false` set on every loaded
contact.
**Expected benefit**: persistence becomes natively testable; prerequisite for T8b; first
slice of shrinking `mesh_wrapper.cpp`.
**Risk level**: Medium (mechanical move of persistence code).
**Validation**

```bash
pio test -e native_test -f test_contact_store -v
pio test -e native_test -v
pio run -e SigurdOS_TDeck
```

Hardware (maintainer): flash, confirm existing contacts still listed after reboot.
**First PR or later?** Later PR (Phase 2; first Phase 2 persistence PR).
**Depends on**: nothing (but read T8b before designing the API so you don't paint it in).

**T8a implementation note (roadmap/T8a-contact-store / PR: T8a: Extract contact
persistence into a testable module (no format change))**

- Status: complete.
- Files changed: `src/mesh/contact_store.{h,cpp}`, `src/mesh/mesh_wrapper.cpp`,
  `platformio.ini`, `test/test_contact_store/`, `docs/CODEBASE_IMPROVEMENT_ROADMAP.md`.
- Validation: `pio test -e native_test -f test_contact_store -v` passed (6/6);
  `pio test -e native_test -v` passed (755 test cases: 1 skipped, 754 succeeded);
  `pio run -e SigurdOS_TDeck` passed.
- RAM/flash delta: RAM unchanged at 133,472 bytes; flash +612 bytes (2,569,853 ->
  2,570,465 bytes).
- Hardware status: skipped; maintainer follow-up is to flash and confirm existing
  contacts still list after reboot.

---

#### T8b: Version the contacts file format (magic header + bounds check)

**Evidence**: as T8a, plus the inline comment in `loadContacts()` showing the format
already changed once (perm byte appended) with no version signal.

**Problem**
No way to detect old/corrupt files; a future format change repeats the unversioned-append
hack; `n` is not bounded by `MAX_CONTACTS`.

**Proposed fix**

1. In `contact_store.{h,cpp}` (from T8a) define a 4-byte magic whose little-endian
   `int32` value is **negative**, e.g. bytes `{'S','G','C',0xB1}`. Rationale (do not
   skip): firmware *older* than this change reads the first 4 bytes as the count and
   rejects the file via its existing `n <= 0` check — so a downgrade after upgrade loses
   saved contacts but **cannot ingest garbage**. Document this in a comment.
2. New write format: magic (4) + version `uint8 = 1` + count `int32` + the unchanged
   66-byte records.
3. Loader: read first 4 bytes; if they equal the magic → read version (reject > 1),
   read count, clamp `count` to `[0, MAX_CONTACTS]`, parse records. Else → seek back to
   offset 0 and run the legacy parse (current behavior, unchanged) so existing devices
   upgrade seamlessly.
4. Extend `test/test_contact_store/`: new-format round trip; legacy-format file loads;
   truncated new-format file; `count > MAX_CONTACTS` clamped; downgrade simulation
   (first 4 bytes parse as negative int).
5. Coordinate, don't duplicate: message-store unification is tracked in
   `docs/ROADMAP.md` ("Message persistence") — this task touches only `/contacts`.

**Behavior that must not change**: loading a legacy file yields exactly the same
contacts as before; save→load round trip preserves count, order, names, types, perms;
SPIFFS-unavailable no-op.
**Edge cases to preserve**: as T8a, plus: a legacy file whose first 4 bytes happen to
match the magic is only possible if a contact count equaled the magic value —
impossible (negative), state this in a test comment.
**Expected benefit**: corruption detected instead of ingested; future migrations have a
version field; safe downgrade story.
**Risk level**: Medium (touches persisted user data — the dual-read path and tests are
mandatory, not optional).
**Validation**: as T8a, plus the new tests; hardware check that contacts survive the
upgrade flash.
**First PR or later?** Later PR.
**Depends on**: T8a (hard dependency).

> **Status (2026-06-11): ✅ Complete** — branch `roadmap/T8b-contacts-format-v1`
> (stacked on `roadmap/T8a-contact-store`; merge T8a's PR first).
> New write format: magic `{'S','G','C',0xB1}` (4) + version `uint8 = 1` + count
> `int32` + unchanged 66-byte records. Loader reads the first 4 bytes: magic match →
> version check (reject > 1), count clamped to `[0, MAX_CONTACTS]`; otherwise the
> bytes are the legacy count and the legacy parse runs unchanged, so existing devices
> upgrade seamlessly. The magic's little-endian int32 value is negative, so a firmware
> downgrade rejects the new file via the old `n <= 0` check instead of ingesting
> garbage (documented in a header comment + downgrade-simulation test). Tests added:
> versioned golden bytes, legacy file loads, unknown version rejected, count clamp,
> truncated versioned file, downgrade negative-count simulation. mesh_wrapper API
> untouched. **Downgrade note for release notes**: after this ships, rolling back
> firmware loses saved contacts (re-discovered via mesh adverts). Hardware check
> (contacts survive the upgrade flash) **blocked on owner** — requires flashing.

**Evidence**

- `CLAUDE.md` rejection trigger: "Unconditional `Serial.printf` without
  `#if defined(SIGURDOS_DEBUG)` guard", yet
  `grep -rn "Serial.print" src --include="*.cpp" | wc -l` ≈ 126 sites outside the
  debug/test/telemetry modules. Policy ("only critical errors and warnings print in
  release") is enforced by eyeball.
- Existing debug-flag conventions: `src/diagnostics/debug_cfg.h` (per-feature
  `SIGURDOS_DEBUG_*` macros) — relocation: `cat src/diagnostics/debug_cfg.h`.

**Problem**
Reviewers must hand-classify every print; the policy cannot be linted.

**Proposed fix**

1. Create `src/diagnostics/log.h` (header-only, no `.cpp`):

   ```cpp
   #pragma once
   #include <Arduino.h>
   // Always-on: errors and warnings (allowed in release builds by policy).
   #define SIG_LOGE(fmt, ...) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__)
   #define SIG_LOGW(fmt, ...) Serial.printf("[W] " fmt "\n", ##__VA_ARGS__)
   // Debug-only: compiled out of release builds.
   #if defined(SIGURDOS_DEBUG)
   #define SIG_LOGD(fmt, ...) Serial.printf("[D] " fmt "\n", ##__VA_ARGS__)
   #else
   #define SIG_LOGD(fmt, ...) do {} while (0)
   #endif
   ```

   Match the license-header style of `src/diagnostics/debug_cfg.h`.
2. Pilot migration in **one file only**: `src/hal/wifi_ota.cpp` (its `[wifi-sta]` /
   `[ota]` prints are status/warning class). Convert each `Serial.printf`/`println` to
   `SIG_LOGW` (kept in release) or `SIG_LOGD` (debug-only), preserving the existing
   message text after the level tag. Classify conservatively: when unsure whether a
   message is needed in release, keep it (`SIG_LOGW`) — never silently drop release
   output in this task.
3. Subsequent files migrate one-per-PR using the same rules (each its own task PR; do
   not bulk-migrate).
4. A CI grep forbidding raw `Serial.print` outside a whitelist is **T23's** job — not
   this PR.

**Behavior that must not change**: release-build serial output of the pilot file must be
equal-or-quieter, with every error/warning message still present (modulo the `[W] `
prefix); debug-build output unchanged in content.
**Edge cases to preserve**: messages used by validation scripts — before migrating a
file, `grep -rn "<distinctive message text>" scripts/` for each changed string; if a
script matches on it, keep the text byte-identical (prefix allowed only after checking
the script's regex).
**Expected benefit**: the logging policy becomes mechanical; later CI enforcement.
**Risk level**: Low (per-file, conservative classification).
**Validation**

```bash
pio run -e SigurdOS_TDeck && pio run -e SigurdOS_TDeck_debug
pio test -e native_test
```

Hardware (maintainer): boot with WiFi creds saved; compare `[wifi-sta]` lines on debug
build before/after.
**First PR or later?** Later PR (Phase 2).
**Depends on**: nothing.

---

#### T10: Measure WDT/stall exposure of long flash operations (measurement only)

**Evidence**

- `factoryReset()` in `src/mesh/mesh_wrapper.cpp` calls `SPIFFS.format()` synchronously.
- GitHub OTA finalize `Update.end(true)` at `src/hal/github_ota.cpp` (~line 353).
- Whether these approach a task-watchdog limit on the pinned Arduino core
  (`framework-arduinoespressif32 @ 3.20017.241212`) is **unverified** (OQ-3).

**Problem**
Unknown stall budget; changing anything before measuring would be guesswork.

**Proposed fix** (no shipped code change)

1. On a debug build (temporary local patch, not committed): record `millis()` before and
   after `SPIFFS.format()` (trigger via Settings → factory reset) and around
   `Update.end(true)` (trigger a GitHub OTA on a test device).
2. Report durations + whether any WDT/brownout/LVGL freeze symptoms appeared, in issue
   #571 (or a dedicated issue), updating OQ-3 with the answer.
3. Only if a real limit is found, file a follow-up task (e.g. feed WDT / "please wait"
   screen) — out of scope here.

**Behavior that must not change**: everything (nothing is merged).
**Risk level**: Low. **First PR or later?** Later (needs hardware; maintainer-run).
**Depends on**: nothing.

> **Status (2026-06-11): ⛔ Blocked — hardware measurement only** — no PR opened
> (none is expected; the task merges nothing). Requires maintainer-run timing on a
> physical device.

---

### Phase 3 — Performance and memory improvements

---

#### T11: Interval-gate the per-loop WiFi status update

**Evidence**

- `src/main.cpp:159`: `sigurdos::ui::update_wifi_status();` called every `loop()`
  iteration.
- `src/ui/screens.cpp:368-383` `update_wifi_status()`: when connected, every call does
  `snprintf` + `lv_label_set_text()` + `lv_obj_set_style_text_color()` —
  `lv_label_set_text` reallocates/invalidates even for identical text.
- A direct call also exists at `src/ui/screens.cpp:261` ("set initial state") when the
  bottom bar is created — that immediacy must be preserved.
- Declarations: `src/ui/ui.h:40` and `src/ui/screens.h:57` (duplicate declaration —
  harmless; see OQ-10).
- Relocation grep: `grep -rn "update_wifi_status" src/`

**Problem**
Thousands of needless label rewrites per second while WiFi is connected — CPU + LVGL
invalidation churn for a slow-changing value.

**Proposed fix** (gate the call site, not the function — keeps creation-time call
immediate):

1. Edit `src/main.cpp` only. Replace the bare call at line ~159 with:

   ```cpp
   {   // WiFi icon refresh — 1 Hz is plenty for an RSSI readout
       static uint32_t last_wifi_ui = 0;
       if (millis() - last_wifi_ui >= 1000) {
           last_wifi_ui = millis();
           sigurdos::ui::update_wifi_status();
       }
   }
   ```

2. Do not modify `update_wifi_status()` itself in this task.

**Behavior that must not change**: icon appears when the bottom bar is created (the
`screens.cpp:261` direct call is untouched); icon reflects connect/disconnect within
~1 s; hidden state when disconnected.
**Edge cases to preserve**: `millis()` wraparound — the subtraction idiom above is
wrap-safe (same pattern as the battery check at `src/main.cpp:144-152`); first loop
iteration runs the update (`last_wifi_ui = 0`).
**Expected benefit**: removes constant background UI churn; measurable via telemetry
loop timing (`SigurdOS_TDeck_telemetry` build, `report_loop_timing`).
**Risk level**: Low.
**Validation**

```bash
pio test -e native_test && pio run -e SigurdOS_TDeck
```

Hardware: connect WiFi via Settings → icon appears ≤1 s; disconnect → disappears ≤1 s.
Optional before/after loop-time comparison on a telemetry build.
**First PR or later?** Safe first PR (Phase 3 by theme, but isolated enough to ship
early).
**Depends on**: nothing.

---

#### T12: Non-blocking buzzer pattern playback

**Evidence**

- `src/hal/buzzer.cpp:14-23` — `buzzer_play_pattern()` walks the pattern with
  `delay(pattern[i].duration_ms)`; called synchronously by `buzzer_beep_short()` /
  `buzzer_beep_double()`.
- Pattern data is in `src/hal/buzzer.h` (`sigurdos_buzzer_pattern()`): Short = 80 ms on;
  Double = 60 on / 60 off / 60 on → worst-case main-loop stall today ≈ **180 ms**.
- Sole production caller: `src/ui/ui.cpp:144` (`buzzer_beep_short()` on message arrival).
- Existing tests: `test/test_buzzer/` (validates the pattern tables);
  `test/mocks/mock_arduino.cpp` has a controllable `current_millis`.
- Relocation greps: `grep -n "buzzer_play_pattern\|delay(" src/hal/buzzer.cpp`;
  `grep -rn "buzzer_beep" src/ --include="*.cpp"`

**Problem**
During a beep the entire `loop()` stalls — LVGL, input, and `sigurdos::mesh::loop()`
freeze for up to ~180 ms per notification.

**Proposed fix**

1. Edit `src/hal/buzzer.cpp`: replace the blocking walk with a state machine held in
   file-statics: `const BuzzerPatternStep* s_pattern`, `size_t s_count`, `size_t s_idx`,
   `uint32_t s_step_started_ms`, `bool s_active`.
   - `buzzer_beep_short()/_double()`: load the pattern, apply step 0's GPIO level
     immediately, record `millis()`, set `s_active = true`, **return immediately**.
     If a pattern is already playing, the new pattern replaces it (restart semantics —
     document in a comment; no call site can trigger overlap today: the only caller is
     one event path).
   - New function `void buzzer_loop();` — declared in `src/hal/buzzer.h` next to
     `buzzer_init()`: if `s_active` and `millis() - s_step_started_ms >=` current step
     duration, advance to the next step (apply its GPIO level); after the last step,
     drive the pin LOW and clear `s_active`. Steps with `duration_ms == 0` are terminal
     markers in the existing tables — apply their level and finish.
2. Edit `src/main.cpp` `loop()`: add `sigurdos::hal::buzzer_loop();` next to the other
   per-loop services (after `sigurdos_display_loop()` is fine).
3. Keep `buzzer_init()` and the pattern tables in `buzzer.h` unchanged.
4. Extend `test/test_buzzer/`: drive `buzzer_loop()` with the mock `millis` and assert
   the GPIO level sequence and timings for both patterns, plus the restart-while-playing
   case. Follow the existing two-file layout (`main.cpp` + `test_buzzer.cpp`).
5. Check the `#ifndef PLATFORMIO_UNIT_TESTING` guard in `buzzer.cpp` (it wraps the
   implementation) — keep the new code inside the same guard structure so native tests
   compile only what they compiled before plus what your new tests need.

**Behavior that must not change**: audible result of both patterns (same on/off
durations within one loop-iteration of jitter — the loop runs every ≤5 ms per
`src/hal/display.cpp:862`); buzzer pin idles LOW (`buzzer_init()` behavior); message
arrival still beeps (`src/ui/ui.cpp:144` call site untouched).
**Edge cases to preserve**: polarity — the tables drive `level_high` through
`digitalWrite(PIN_BUZZER, ...)`; do not invert anything (note `CLAUDE.md`'s hardware
table says "Buzzer GPIO 46 active low" while the code+tables treat HIGH as "on" — see
OQ-11; do not "fix" polarity in this task); `buzzer_quiet` pref — check where call
sites gate on it (`grep -rn "buzzer_quiet" src/`) and leave that gating where it is.
**Expected benefit**: zero main-loop stalls from audio (~180 ms worst case removed from
the message-arrival path).
**Risk level**: Medium (timing-sensitive; new loop hook).
**Validation**

```bash
pio test -e native_test -f test_buzzer -v
pio test -e native_test -v
pio run -e SigurdOS_TDeck
```

Hardware: send the device a DM → short beep sounds as before; navigate during the beep →
no input/animation hitch.
**First PR or later?** Later PR (Phase 3).
**Depends on**: nothing (but merge after T11 to keep `main.cpp` churn serialized).

---

#### T13: Boot-delay measurement and reduction — **hardware-gated**

**Evidence**

- `src/main.cpp:35-37`: `delay(250)` ("Let WebSerial port close before claiming USB CDC
  endpoint") + `Serial.begin(115200)` + `delay(500)`.
- `src/hal/display.cpp:748`: `delay(50)` backlight pulse.
- `src/hal/sdcard.cpp:44-53`: up to 2 × `delay(500)` retrying `SD.begin()` when no card
  is present (3 attempts total).
- Worst case ≈ 1.8 s of fixed waiting before the UI is interactive.
- Relocation greps: `grep -n "delay(" src/main.cpp src/hal/sdcard.cpp | head`;
  `grep -n "delay(50)" src/hal/display.cpp`

**Problem**
Every boot pays the worst case; the SD retries cost 1 s on every cardless boot; the
serial delays also run on battery boots with no host attached.

**Proposed fix** (measurement first; do not skip step 1)

1. **Measure** (no merged code): timestamped debug boot logs, 10 boots per scenario:
   USB attached / battery only × SD present / absent, on both board revisions if
   available. Record in the issue.
2. **SD lazy retry** (only candidate safe to implement without answering OQ-2): change
   `sigurdos_sdcard_init()` (`src/hal/sdcard.cpp`) to attempt `SD.begin()` **once** at
   boot; on failure return `false` without retrying. Add
   `bool sigurdos_sdcard_retry();` (declared in `src/hal/sdcard.h`) that performs one
   additional attempt, and call it lazily from the SD consumers when unmounted — find
   them with `grep -rn "sigurdos_sdcard_mounted\|sigurdos_sdcard_init" src/ --include="*.cpp"`
   — the map tile path (`src/app/map_renderer.cpp`) is the main one. Cap total retries
   (e.g. 3) with a static counter to avoid unbounded re-probing of a broken card.
3. **Serial delays**: do not change until OQ-2 is answered with hardware evidence — the
   comment documents a real USB-CDC race.

**Behavior that must not change**: SD card present at boot → mounts exactly as today;
USB serial still enumerates reliably; `SIGURDOS_SD_MOUNTPOINT` VFS path and the 4 MHz
bus clock in `SD.begin(PIN_SD_CS, sd_spi, 4000000, ...)`; shared-SPI init order
(`sigurdos_shared_spi_begin` before `SD.begin`, `src/hal/sdcard.cpp:43`).
**Edge cases to preserve**: card inserted after boot was *never* auto-mounted (no
hot-plug today) — lazy retry may incidentally improve this; do not promise or rely on it.
**Expected benefit**: ~1 s faster boot for cardless devices; data to justify (or kill)
the serial-delay change.
**Risk level**: Medium (hardware races; board revisions differ on SD CS — see
`CLAUDE.md` hardware table note about pin 21 on v1.0).
**Validation**: native tests (`test_sdcard` exists); `pio run`; re-run the 4-scenario
hardware matrix from step 1 after the change.
**First PR or later?** Later PR; step 2 only after step 1's data is posted.
**Depends on**: OQ-2 for the serial delays.

> **Status (2026-06-11): ⛔ Blocked — OQ-2 + hardware-gated measurement** — no PR
> opened. Step 1 (10-boot timing matrix) needs a physical device; the serial-delay
> changes need the OQ-2 answer.

---

### Phase 4 — Architecture / module cleanup

> Do not start Phase 4 until T20 (PR firmware-build CI) is merged — these are the PRs
> most likely to break the firmware build, and CI must catch that.

---

#### T14: Extract `screens_common` from `screens.cpp`

**Evidence**

- `src/ui/screens.cpp` (7,829 lines) holds shared infrastructure used by every screen:
  `make_screen_full()` (line 167), the back-button static `s_back_btn` (line 71),
  the WiFi icon static `g_wifi_icon` (line 74) + `update_wifi_status()` (line 368),
  PIN-entry helpers `pin_grace_active()` / `pin_entry_show()` (declared lines 77-78).
- 88 file-static declarations total (`grep -c "^static " src/ui/screens.cpp`).
- Relocation greps: `grep -n "make_screen_full\|s_back_btn\|g_wifi_icon\|pin_entry_show" src/ui/screens.cpp | head -20`

**Problem**
Every per-screen split (T15+) needs these helpers; they must move first or every split
PR would re-touch them.

**Proposed fix**

1. Create `src/ui/screens_common.h` + `src/ui/screens_common.cpp` (namespace
   `sigurdos::ui`, same license header as `screens.cpp`).
2. Move **verbatim** (cut-paste, no edits beyond linkage): `make_screen_full()`,
   `update_wifi_status()` + `g_wifi_icon`, `s_back_btn` and any function that sets it,
   the PIN-entry implementation, and any small helper they directly call (chase
   compiler errors — move the minimal closure of dependencies).
3. Formerly-`static` symbols that other code in `screens.cpp` still uses become
   non-static, declared in `screens_common.h`. Keep names identical.
4. `#include "screens_common.h"` from `screens.cpp`. No `platformio.ini` change needed:
   `[env:SigurdOS_TDeck]` `build_src_filter` already globs `+<ui/*.cpp>`; `native_test`
   does not compile `screens.cpp` and must not gain `screens_common.cpp` unless a test
   needs it (it won't in this task).
5. Nothing else moves in this PR.

**Behavior that must not change**: every screen renders identically; the WiFi icon,
back button, and PIN gate behave identically; binary size within noise (same code, new
TU boundaries).
**Edge cases to preserve**: `LV_EVENT_DELETE` callbacks that null the moved globals must
move with them (search: `grep -n "g_wifi_icon = nullptr\|s_back_btn = nullptr" src/ui/screens.cpp`);
do not introduce static constructors — all moved globals are pointers initialized to
`nullptr`.
**Expected benefit**: unblocks T15–T18; first real dent in the monolith.
**Risk level**: Medium (mechanical, but wide blast radius if a symbol is missed —
the compiler will catch it).
**Validation**

```bash
pio test -e native_test -v
pio run -e SigurdOS_TDeck
```

Then remote-test smoke (with owner consent for the build, per Rule 10): `nav` to each of
the screens listed in `src/ui/navigation.cpp` `dispatch_screen()`, confirm `screen`
reports the expected name and the top/bottom bars render.
**First PR or later?** Later PR (first Phase 4 PR).
**Depends on**: T20 merged (CI safety net); T11 merged (so `update_wifi_status` usage is
stable before moving).

> **Status (2026-06-11): ✅ Complete** — merged as PR #580
> ("refactor: extract screens_common from screens.cpp", commit `11f2e7e`).
> `src/ui/screens_common.{h,cpp}` exist; `screens.cpp` is down to 84 lines.

---

#### T15–T18: Split `screens.cpp` one screen per PR

**Evidence**

- Screen entry points and their anchors in `src/ui/screens.cpp` (find each with
  `grep -n "make_screen_full(\"<Title>\")" src/ui/screens.cpp`):
  Packets (511), Contacts (576), Contact detail (1328), Finder (2021), Repeaters (2184),
  Signal (3010), Map (3219), Bluetooth (3906), Radio/Mesh settings (3965), GPS settings
  (4378), Display settings (4479), System settings (4583), Node Stats (5127), Settings
  (5333), Terminal (5425), Trace (5778), Channels (6001), Advertise (6195), Custom RF
  (6268), Radio Setup (6420), Telemetry (6764), WiFi (7080), Node Status (7125),
  Regions (7554).
- Public entry points are declared in `src/ui/screens.h` (`*_screen_show()` etc.) and
  dispatched from `src/ui/navigation.cpp` `dispatch_screen()`.

**Problem**
7,829 lines in one TU: merge conflicts, slow review, shared-static coupling.

**Proposed fix** (repeat per PR; suggested order = smallest/leaf first):

- T15: Advertise → `src/ui/screens/screen_advertise.cpp`
- T16: Packets → `src/ui/screens/screen_packets.cpp`
- T17: Signal → `src/ui/screens/screen_signal.cpp`
- T18: continue with the remaining screens in ascending size order, one per PR,
  Settings family last (they share state — verify with greps before choosing each
  PR's scope).

Per-PR steps:

1. Create `src/ui/screens/` on first use. Confirm the glob: `[env:SigurdOS_TDeck]`
   `build_src_filter` contains `+<ui/*.cpp>` which does **not** match subdirectories —
   add `+<ui/screens/*.cpp>` to the filter in the same PR that creates the directory
   (this is a `platformio.ini` edit, not a workflow edit — allowed).
2. Move the screen's `*_screen_show()` function and **only the statics and callbacks
   that no other screen references** (verify each moved symbol with
   `grep -n "<symbol>" src/ui/screens.cpp` — zero remaining references required).
3. Keep the declaration in `src/ui/screens.h` unchanged; the new file includes
   `screens.h`, `screens_common.h`, `theme.h`, `responsive.h` as needed.
4. No logic edits whatsoever in the same PR as a move.

**Behavior that must not change**: pixel-identical screen rendering; navigation routing
(`src/ui/navigation.cpp` untouched); timer/delete callbacks still fire
(e.g. Advertise's `g_advert_status_timer` cleanup at `screens.cpp:154-162` must move
with its screen).
**Edge cases to preserve**: `LV_EVENT_DELETE` handlers nulling moved statics;
`lv_timer_del` on screen delete; statics shared *between* screens stay in
`screens.cpp`/`screens_common.cpp` until all their consumers have moved.
**Expected benefit**: each subsequent UI PR touches a small file; parallel PRs stop
conflicting.
**Risk level**: Medium per PR (compiler catches missed symbols; the behavior risk is
deleted-callback lifecycle — hence the remote-test smoke per PR).
**Validation** per PR: full native suite, firmware build, remote-test `nav` to the moved
screen + `back` + re-enter (catches delete-callback regressions).
**First PR or later?** Later PRs, strictly after T14.
**Depends on**: T14; T20.

> **Status (2026-06-11): ✅ Complete** — merged as PRs #585 (Advertise, `55e3b49`),
> #586 (Packets, `3aeebe7`), #587 (Signal, `f7b282b`), and #588 (all remaining
> screens, `53a3f63`). `src/ui/screens/` now holds 22 per-screen files;
> `screens.cpp` retains only shared glue (84 lines).

---

#### T19: Extract persistence/boundaries from `mesh_wrapper.cpp` and `chat_screen.cpp` (outline)

**Evidence**: `src/mesh/mesh_wrapper.cpp` 2,314 lines (API surface + persistence +
packet log); `src/ui/chat_screen.cpp` 2,862 lines (channel state `dyn_channels` /
`ch_meta` / `ch_msgs` statics at lines 126-162 + rendering + search).
**Problem/fix**: T8a already extracts contact persistence; the channel/identity
persistence (`saveChannels`/`loadChannels`/`saveIdentity` — locate via
`grep -n "void saveChannels\|void loadChannels\|saveIdentity" src/mesh/mesh_wrapper.cpp`)
should follow the same `message_store.cpp` pattern; `chat_screen.cpp` splits into
state vs rendering only after a dedicated design note.
**Risk level**: Medium–High. **First PR or later?** Later (Phase 4 tail); requires a
short design note in the PR description and maintainer sign-off before implementation.
**Depends on**: T8a, T8b, T14 pattern established.

> **Status (2026-06-11): ⛔ Blocked — needs design note + maintainer sign-off** —
> no PR opened, per the task's own gate. Prerequisites are now in flight: T8a (#598)
> and T8b (#603) are open PRs; T14 is merged. Next step is a design note for the
> owner to approve before any implementation.

---

### Phase 5 — Build, CI, and release hardening

> Every task in this phase edits `.github/workflows/*` — **protected files**. Each is a
> dedicated PR requiring CODEOWNER review (`CONTRIBUTING.md` "Protected Files").

---

#### T20: Compile the firmware on every PR

**Evidence**

- `.github/workflows/pr-ci.yml` — single job `test` running `pio test -e native_test -v`.
- `[env:native_test]` `build_src_filter` (in `platformio.ini`) compiles only a subset of
  `src/` with mocked hardware — ESP32-only code is never compiled by PR CI.
- Cold firmware build ≈ 10.5 min, warm ≈ 1.5 min (`docs/AUDIT.md` evidence table).

**Problem**
A PR can pass CI and still break `pio run -e SigurdOS_TDeck`.

**Proposed fix**

1. Edit `.github/workflows/pr-ci.yml` only. Add a second job (copy the existing job's
   checkout/python/cache/install steps verbatim, then build):

   ```yaml
     build:
       name: Firmware build
       runs-on: ubuntu-latest
       steps:
         - name: Check out repository
           uses: actions/checkout@v4
           with:
             submodules: recursive
         - name: Set up Python
           uses: actions/setup-python@v5
           with:
             python-version: '3.12'
         - name: Cache PlatformIO packages
           uses: actions/cache@v4
           with:
             path: |
               ~/.platformio/.cache
               ~/.platformio/packages
               ~/.platformio/platforms
             key: ${{ runner.os }}-platformio-${{ hashFiles('platformio.ini') }}
             restore-keys: |
               ${{ runner.os }}-platformio-
         - name: Install PlatformIO
           run: python -m pip install --upgrade platformio
         - name: Build firmware
           run: pio run -e SigurdOS_TDeck
   ```

2. Keep the existing `test` job untouched; the two jobs run in parallel (do not add
   `needs:` — fast feedback on both).

**Behavior that must not change**: the `test` job's triggers, concurrency group, and
permissions block stay as-is.
**Edge cases to preserve**: `submodules: recursive` is mandatory (MeshCore submodule);
the cache key already hashes `platformio.ini`, so T1's pinning keeps caches coherent.
**Expected benefit**: firmware-breaking PRs caught pre-merge — the single
highest-leverage CI change available.
**Risk level**: Low (CI-only).
**Validation**: open a draft PR stacked on this change containing a deliberate compile
error in `src/main.cpp` → `build` job must fail while `test` passes; close the draft;
confirm the real PR is green.
**First PR or later?** Safe first PR (dedicated protected-file PR). **Recommended to be
the first CI PR merged overall** — Phases 2–4 rely on it.
**Depends on**: nothing.

> **Status (2026-06-11): ✅ Complete** — merged as PR #579
> ("ci: compile firmware on every PR", commit `0250fbf`). `pr-ci.yml` has the
> parallel `build` job exactly as specified.

---

#### T21: Align the two workflows (Python version + cache)

**Evidence**: `.github/workflows/pr-ci.yml` uses Python `3.12` and caches
`~/.platformio/{.cache,packages,platforms}`; `.github/workflows/build-release.yml` uses
Python `3.11` and caches all of `~/.platformio` with a different key prefix (`pio-`).
**Problem**: PR-green doesn't reliably predict release-green when toolchain setups differ.
**Proposed fix**: edit `build-release.yml` only — set `python-version: '3.12'` in both
its jobs and adopt pr-ci's cache `path`/`key` block verbatim.
**Behavior that must not change**: tag trigger, artifact names
(`sigurdos-tdeck-firmware`), release-body content, `permissions: contents: write`.
**Risk level**: Low. **Validation**: run `workflow_dispatch` on the branch; both jobs
green; artifacts present. **First PR or later?** Safe first PR (dedicated).
**Depends on**: T20 merged first (so the cache layout settles once).

**Implementation note (2026-06-11)**:

- status: complete
- files changed: `.github/workflows/build-release.yml`,
  `docs/CODEBASE_IMPROVEMENT_ROADMAP.md`
- validation results: `pio test -e native_test -v` passed
  (748 succeeded, 1 skipped); `pio run -e SigurdOS_TDeck` passed
  (RAM 40.7%, flash 39.2%); relocation grep confirmed both workflows now use Python
  `3.12` and the same PlatformIO cache paths/key prefix; `workflow_dispatch` passed on
  the branch with `sigurdos-tdeck-firmware` artifact present
- RAM/flash delta: not relevant; CI-only workflow change
- hardware status: not applicable; no firmware behavior change and no hardware flashed
- PR branch/name: `roadmap/T21-workflow-align` / `T21: Align the two workflows
  (Python version + cache)`

---

#### T22: Release checksums + version-consistency check

**Evidence**

- `.github/workflows/build-release.yml` release job uploads `firmware/firmware.bin` and
  `firmware/firmware-merged.bin` with no digest file.
- Version sources disagree by design today: the *binary's* displayed version comes from
  `git describe` (`scripts/build_metadata.py:75-93` overrides `SIGURDOS_VERSION`), while
  the *webflasher manifest* parses the fallback string out of `src/hal/tdeck_pins.h`
  (`scripts/merge_bin.py:124-132`; fallback currently `"beta-0.1.40"` at
  `tdeck_pins.h:160-162`). If the fallback isn't bumped at release time, the manifest
  reports a stale version.

**Problem**
No integrity artifact on releases; manifest version can silently disagree with the tag.

**Proposed fix**

1. Edit `.github/workflows/build-release.yml` release job: after the
   `Download firmware` step add:

   ```yaml
         - name: Generate checksums
           run: |
             cd firmware
             sha256sum *.bin > SHA256SUMS.txt
   ```

   and append `firmware/SHA256SUMS.txt` to the `files:` list.
2. Add a guard step to the **build** job (before "Build firmware"):

   ```yaml
         - name: Check version fallback matches tag
           if: startsWith(github.ref, 'refs/tags/')
           run: |
             TAG="${GITHUB_REF_NAME}"
             FALLBACK=$(grep -oP '#define SIGURDOS_VERSION\s+"\K[^"]+' src/hal/tdeck_pins.h)
             if [ "$TAG" != "$FALLBACK" ]; then
               echo "::error::Tag $TAG != tdeck_pins.h fallback $FALLBACK — bump SIGURDOS_VERSION before tagging"
               exit 1
             fi
   ```

**Behavior that must not change**: existing artifact names and release-body flash
instructions; non-tag `workflow_dispatch` runs must not fail the new guard (hence the
`if:` condition).
**Edge cases to preserve**: tags are zero-padded (`beta-0.1.09` style, per `CLAUDE.md`
versioning) — string equality is the correct comparison, no numeric parsing.
**Risk level**: Low. **Validation**: `workflow_dispatch` run (guard skipped, checksums
file produced in artifacts); next real tag exercises the guard.
**First PR or later?** Safe first PR (dedicated). **Depends on**: T21 (same file —
serialize edits).

> **Status (2026-06-11): ✅ Complete** — branch `roadmap/T22-release-checksums`.
> Added to `.github/workflows/build-release.yml`: (1) `Generate checksums` step in the
> release job (`sha256sum *.bin > SHA256SUMS.txt` over the downloaded artifacts) with
> `firmware/SHA256SUMS.txt` appended to the release `files:` list; (2) tag-gated
> `Check version fallback matches tag` guard in the build job (string equality between
> `GITHUB_REF_NAME` and the `SIGURDOS_VERSION` fallback in `src/hal/tdeck_pins.h` —
> grep expression verified locally, extracts `beta-0.1.40`). Guard is skipped on
> `workflow_dispatch` via the `if:` condition, so non-tag runs are unaffected.
> Artifact names, release body, triggers, and permissions unchanged. Hunks do not
> overlap T21's (Python/cache) edits — merge in either order. Validation: YAML parses;
> CI-only change (no firmware delta). Full `workflow_dispatch` run is owner-triggered.

---

#### T23: Static analysis + logging-policy grep (non-blocking at first)

**Evidence**: no `pio check` configuration or lint job exists
(`grep -rn "pio check" .github/ platformio.ini` → empty); logging policy from T9.
**Proposed fix**: new job in `pr-ci.yml` running `pio check -e SigurdOS_TDeck
--skip-packages` with `continue-on-error: true`, plus (after T9's pilot series has
migrated the relevant files) a grep step failing on raw `Serial.print` outside an
explicit whitelist (`src/diagnostics/`, `src/test/`, files not yet migrated — keep the
whitelist in the workflow, shrink it per migration PR).
**Behavior that must not change**: job is advisory (`continue-on-error`) until the
baseline is triaged; existing jobs untouched.
**Risk level**: Low. **First PR or later?** Later (after T9 pilot merges; dedicated
protected-file PR). **Depends on**: T9, T20.

> **Status (2026-06-11): ✅ Complete** — branch `roadmap/T23-static-analysis`.
> New advisory `lint` job in `pr-ci.yml` (`continue-on-error: true`): runs
> `pio check -e SigurdOS_TDeck --skip-packages`, then the logging-policy grep
> failing on raw `Serial.print` in `*.cpp` outside the whitelist. Whitelist lives in
> the workflow as two regexes: policy dirs (`src/diagnostics|test|validation/`) and
> the 13 not-yet-migrated files — shrink the second list per T9 migration PR (the
> T9 pilot migrates `hal/wifi_ota.cpp`; its entry becomes removable once that
> merges). Grep verified green against current `dev`. Existing `test`/`build` jobs,
> triggers, permissions, and concurrency group untouched.

#### T24: Nightly env smoke matrix

**Evidence**: `scripts/smoke_build_matrix.py` exists with profiles `release`, `debug`,
`roadmap` (builds `SigurdOS_TDeck`, `_telemetry`, `_remote_test`, `_remote_test_radio`)
— nothing in `.github/workflows/` invokes it.
**Problem**: debug/test envs can rot silently; even T20 only builds the release env on
PRs.
**Proposed fix**: new workflow `.github/workflows/nightly-smoke.yml` —
`on: schedule: - cron: '0 3 * * *'` + `workflow_dispatch`, one job, same
checkout/python/cache/install steps as T20, then
`python scripts/smoke_build_matrix.py --profile roadmap`.
**Behavior that must not change**: no PR-blocking effect (schedule-only).
**Risk level**: Low. **Validation**: `workflow_dispatch` run green.
**First PR or later?** Later (dedicated PR). **Depends on**: T20 (cache layout).

> **Status (2026-06-11): ✅ Complete** — branch `roadmap/T24-nightly-smoke`.
> New `.github/workflows/nightly-smoke.yml` exactly per spec: `schedule` cron
> `0 3 * * *` + `workflow_dispatch`, one job, `permissions: contents: read`, and the
> same checkout/python(3.12)/cache/install steps as `pr-ci.yml` (T20), then
> `python scripts/smoke_build_matrix.py --profile roadmap` (builds `SigurdOS_TDeck`,
> `_telemetry`, `_remote_test`, `_remote_test_radio` — these are CI compile checks
> only; no device is flashed). No existing workflow touched; no PR-blocking effect.
> The same matrix passed locally this session (4/4 PASS). Final validation
> (`workflow_dispatch` run green) is owner-triggered after merge.

---

#### T25: Doc-reference and size-regression guards (optional, later)

**Evidence**: doc drift documented in T5; no size tracking exists in CI.
**Proposed fix** (two independent later additions to `pr-ci.yml`):
(a) a step that extracts `src/...` paths cited in `docs/*.md` and fails if a cited file
doesn't exist (start advisory); (b) a step that parses `pio run` output
(`RAM:`/`Flash:` lines) into the job summary so reviewers see deltas per PR.
**Risk level**: Low. **First PR or later?** Later. **Depends on**: T20.

> **Status (2026-06-11): ✅ Complete** — branch `roadmap/T25-ci-guards` (stacked on
> `roadmap/T23-static-analysis`; merge T23's PR first — both edit `pr-ci.yml`).
> (a) `Doc reference check` step added to the advisory `lint` job: verifies cited
> `src/...` paths exist in the repo or `lib/meshcore/` (docs cite submodule-relative
> paths); excludes `CODEBASE_IMPROVEMENT_ROADMAP.md` (planning doc, cites
> not-yet-created files), `LAUNCHER_ROADMAP.md` (describes the external Launcher
> repo), and `TELEMETRY_ARCHITECTURE.md` (already flagged OUT OF DATE — re-include
> after rewrite); also skips composite `.h/.cpp` notation. Verified green on `dev`.
> (b) `Report firmware size` step in the `build` job: `pio run` output tees to
> `build.log` (with `set -o pipefail` so the build's exit code is preserved) and the
> `RAM:`/`Flash:` lines land in the job summary. Pure reporting — never fails.
> Existing jobs/triggers/permissions untouched beyond those two steps.

#### T26: Firmware-binary distribution decision — **Blocked on OQ-5 (owner)**

**Evidence**: `firmware/firmware-merged.bin` (1.99 MB), `firmware/sigurdos-tdeck.bin`
(1.01 MB), `firmware/sigurdos-tdeck-merged.bin` (1.18 MB) are git-tracked; `.git` is
already 59 MB and grows each release commit.
**Problem**: unbounded repo growth.
**Proposed fix**: owner decides between Git LFS vs releases-only distribution. Hard
constraint: must not break the web-flasher flow documented in `firmware/README.md`
(which references per-component binaries under `webflasher/` and `flasher.sigurdos.dev`).
No agent should attempt this without the OQ-5 answer.
**Risk level**: Medium. **First PR or later?** Later, owner-led. **Depends on**: OQ-5.

> **Status (2026-06-11): ⛔ Blocked on OQ-5 (owner-led)** — no PR opened, per the
> task's own rule ("No agent should attempt this without the OQ-5 answer").

---

### Phase 6 — Regression and hardware validation

#### T27: Post-roadmap regression pass + crash-handler follow-up

- Re-run the full Phase 0 baseline and diff: test count/pass rate, RAM/flash %, boot-log
  step sequence/timing, env smoke matrix.
- Remote-test smoke across all screens (`scripts/validation/remote_test_smoke.py`) —
  owner consent required for the build (Rule 10).
- Hardware gates already enumerated in `docs/ROADMAP.md` (RF interop, OTA
  positive/negative, SD/map, sleep/wake, soak) remain the release bar — not duplicated
  here.
- Crash-handler improvement: `src/diagnostics/telemetry_crash.cpp:86` carries
  `// FIXME: Replace with esp_panic_handler_register_with_id() to capture …` (see also
  commit `5bc7842`). Implement in a telemetry-build iteration; panic-path code must be
  minimal and IRAM-safe. **Risk: Medium–High; Later PR; depends on**: telemetry build
  hardware access.

> **Status (2026-06-11): ◑ Software regression pass complete; hardware items blocked.**
> Branch `roadmap/T27-final-sweep`. Regression re-run on `dev` (`53a3f63`) matches the
> Phase 0 baseline exactly:
>
> - Native tests: 749 cases — 748 passed, 1 skipped (baseline: identical).
> - Release build: RAM 40.7% (133,472 B), Flash 39.2% (2,569,853 B) — byte-identical
>   to baseline.
> - Env smoke matrix (`--profile roadmap`): 4/4 PASS (`SigurdOS_TDeck`, `_telemetry`,
>   `_remote_test`, `_remote_test_radio`).
>
> Remaining items are hardware-gated and **blocked on owner**: boot-log step
> sequence/timing diff (needs a flashed debug build), remote-test smoke across all
> screens (needs consent for the radio-disabling `remote_test` build, Rule 10), and
> the crash-handler FIXME (telemetry-build hardware iteration; no PR opened for it).
> This sweep also recorded final statuses on T6/T10/T13/T19/T26 (blocked, see each
> section) and T14/T15–T18/T20 (merged). Open roadmap PRs at sweep time:
> #589 (T1), #590 (T3), #591 (T4), #592 (T5), #593 (T2), #594 (T11), #596 (T21),
> #598 (T8a), #600 (T9), #601 (T22), #602 (T7), #603 (T8b, stacked on #598),
> #604 (T12), #605 (T24), #606 (T23), #607 (T25, stacked on #606).

---

## 5. PR Sequencing Plan

Recommended merge order. "Gate" = what must be true before starting.

| Seq | Task | Branch suggestion | Protected-file PR? | Gate |
| --- | --- | --- | --- | --- |
| 1 | T20 firmware build on PRs | `ci/T20-pr-firmware-build` | Yes | Phase 0 recorded |
| 2 | T1 pin deps | `chore/T1-pin-deps` | No | — |
| 3 | T3 deprecate blocking connect | `chore/T3-deprecate-connect` | No | — |
| 4 | T4 hygiene | `chore/T4-hygiene` | No | — |
| 5 | T5 doc refresh | `docs/T5-doc-refresh` | No | — |
| 6 | T2 clang-format config | `chore/T2-clang-format` | No | — |
| 7 | T11 wifi-status gate | `perf/T11-wifi-status-interval` | No | — |
| 8 | T6 untrack local ini | `chore/T6-untrack-local-ini` | No | OQ-1 answered |
| 9 | T21 workflow alignment | `ci/T21-workflow-align` | Yes | T20 merged |
| 10 | T22 checksums + version guard | `ci/T22-release-checksums` | Yes | T21 merged |
| 11 | T7 display-init recovery | `fix/T7-display-init-retry` | No | hardware check planned |
| 12 | T9 log macros + pilot | `refactor/T9-log-macros` | No | — |
| 13 | T8a contact store extraction | `roadmap/T8a-contact-store` | No | — |
| 14 | T8b versioned contacts format | `feat/T8b-contacts-format-v1` | No | T8a merged |
| 15 | T12 non-blocking buzzer | `perf/T12-buzzer-nonblocking` | No | T11 merged |
| 16 | T24 nightly smoke | `ci/T24-nightly-smoke` | Yes | T20 merged |
| 17 | T14 screens_common | `refactor/T14-screens-common` | No | T20, T11 merged |
| 18+ | T15–T18 screen splits | `refactor/T15-screen-advertise` … | No | T14 merged |
| — | T10, T13 | measurement-led | No | hardware access |
| — | T19, T23, T25, T26, T27 | as specified | varies | listed deps |

Parallelism: Seq 2–7 are independent of each other; everything in Phase 4 is serial
(one screen at a time).

---

## 6. Per-Task Checklist (copy into every task PR description)

```markdown
- [ ] Relocation greps run; evidence still matches this roadmap (else: stopped & flagged)
- [ ] Depends-on tasks merged; not blocked on an open question
- [ ] Baseline green BEFORE change: `pio test -e native_test` + `pio run -e SigurdOS_TDeck`
- [ ] Change limited to the files named in the task
- [ ] No protected files touched (or: this IS the dedicated protected-file PR)
- [ ] Tests added/updated per the task's Validation section
- [ ] `pio test -e native_test -v` passes after change
- [ ] `pio run -e SigurdOS_TDeck` builds after change; RAM/flash delta noted in PR
- [ ] "Behavior that must not change" list re-read and satisfied
- [ ] Hardware testing declared in PR body (or "Not applicable — no firmware change")
- [ ] `Fixes #<issue>` reference present
```

---

## 7. Regression Testing Checklist (per merged task; full pass per phase)

- [ ] `pio test -e native_test -v` — all suites, case count ≥ previous baseline
- [ ] `pio run -e SigurdOS_TDeck` — RAM/flash within expected delta
- [ ] `pio run -e SigurdOS_TDeck_debug` — debug env still builds
- [ ] `python scripts/smoke_build_matrix.py --profile roadmap` (phase-end)
- [ ] Existing feature spot-checks relevant to the change (e.g. T8x → contacts list;
      T12 → message beep; T11 → WiFi icon)
- [ ] No new compiler warnings vs baseline (`pio run 2>&1 | grep -ci warning` compare)

---

## 8. Firmware/Device Testing Checklist (maintainer; phase-end and for any task marked "hardware")

- [ ] Flash debug build; capture boot log; `[boot] step` sequence complete and in order
- [ ] First boot after flash reaches Home screen; mesh-init line present (per
      `CONTRIBUTING.md` boot-log checks)
- [ ] Reboot persistence: node name, radio prefs, contacts, channels survive a power
      cycle (`saveState`/NVS/SPIFFS paths)
- [ ] Display: wake/sleep (auto-off, release build), brightness restore
- [ ] Input: keyboard text entry, trackball nav, touch tap on at least 3 screens
- [ ] SD: with card → mounted (Map tiles load); without card → boot completes
- [ ] Radio: send/receive a channel message with a second MeshCore node
- [ ] BLE (if companion in scope for the change): pair + message round-trip
- [ ] OTA (if Phase 5 touched release tooling): one GitHub OTA cycle on a test device
- [ ] Factory reset: completes, reboots to onboarding, identity regenerated
- [ ] Remote-test smoke (`scripts/validation/remote_test_smoke.py`) — **owner consent
      required** before flashing the remote-test build (disables LoRa)

---

## 9. Rollback Plan

- Every task is a single squash-merged commit on `dev` (repo convention,
  `CONTRIBUTING.md` "Merging"). Rollback = `git revert <squash commit>` — each task PR
  is sized so the revert applies cleanly and independently.
- Order-sensitive reverts: T8b must be reverted **before** T8a if both must go;
  T15–T18 revert in reverse merge order; workflow tasks (T20–T24) revert independently.
- **T8b special case (persisted data)**: after T8b ships, devices write the new
  magic-headered `/contacts` file. Reverting the firmware (or downgrading) makes the old
  loader read the magic as a negative count and load **zero contacts** — by design, no
  garbage is ingested, but saved contacts are effectively lost on downgrade until
  re-discovered via mesh adverts. State this in the T8b PR description and release notes.
- T12 rollback note: revert removes `buzzer_loop()` from `main.cpp` and restores
  blocking beeps — no persisted state involved.
- If a hardware regression is found post-merge with no quick revert possible, the
  fallback is flashing the previous release artifact
  (`firmware/firmware-merged.bin` at the prior tag) per `firmware/README.md`.

---

## 10. Risk Matrix

| Task | Likelihood of harm today | Impact if it bites | Fix effort | Fix risk | Phase |
| --- | --- | --- | --- | --- | --- |
| T20 (CI gap) | High (every PR) | Broken dev builds | S | Low | 5* |
| T14–T18 (monolith) | High (every UI PR) | Conflicts, slow review | L (phased) | Medium | 4 |
| T5 (doc drift) | High (every agent session) | Misdirected work | S | Low | 1 |
| T7 (display hang) | Low | Dead field unit | S | Low | 2 |
| T1 (floating deps) | Medium | Irreproducible builds | S | Low | 1 |
| T6 (tracked local ini) | Medium | Ambiguous build flags | S | Low | 1 |
| T8a/T8b (contacts format) | Low–Medium | Silent data corruption | M | Medium | 2 |
| T9/T23 (logging policy) | Medium | Release log noise | M (phased) | Low | 2/5 |
| T12 (blocking buzzer) | Medium | ~180 ms UI/mesh stalls | S | Medium | 3 |
| T13 (boot delays) | Certain (~1.8 s) | Slow boot only | M | Medium (hw races) | 3 |
| T11 (per-loop label) | Certain | CPU/LVGL churn | S | Low | 3 |
| T3 (blocking connect) | Low | 15 s UI freeze | S | Low | 1 |
| T27 (crash backtrace) | Medium | Slow triage | M | Medium–High | 6 |
| T26 (binaries in git) | Certain (repo growth) | Clone size | M | Medium | 5 |

\* T20 is Phase 5 by category but sequence #1 chronologically.

---

## 11. Prioritized Implementation Roadmap (phase view)

- **Phase 0 — Baseline**: §3 commands recorded; hardware boot log archived.
- **Phase 1 — Low-risk cleanup**: T1, T2, T3, T4, T5, T6 (OQ-1), T11.
- **Phase 2 — Error handling & recovery**: T7, T8a, T8b, T9, T10 (measurement).
- **Phase 3 — Performance & memory**: T12, T13 (measurement-led, hardware-gated).
  Before/after measurement plan: telemetry-build loop timing for T11/T12; timestamped
  boot logs (10 boots × 4 scenarios) for T13.
- **Phase 4 — Architecture cleanup**: T14, then T15–T18 serially, then T19 (design note
  first). Gated on T20.
- **Phase 5 — Build/CI/release hardening**: T20 (first!), T21, T22, T23, T24, T25,
  T26 (OQ-5).
- **Phase 6 — Regression & hardware validation**: T27 + §7/§8 checklists at each phase
  boundary.

---

## 12. Open Questions

Answers should be recorded here (or in the linked issue) before dependent tasks start.

- **OQ-1** (blocks T6): Is `-D SIGURDOS_DEBUG_MESH=1` meant to be canonical for
  `SigurdOS_TDeck_remote_test_radio`, or genuinely local? The tracked
  `platformio.local.ini` and `platformio.ini` definitions disagree.
- **OQ-2** (blocks T13 serial-delay work): Can the 250 ms + 500 ms delays in
  `src/main.cpp:35-37` be reduced or USB-attach-gated without breaking WebSerial/CDC
  enumeration? Verify: 10+ boots per scenario on both T-Deck revisions.
- **OQ-3** (informs T10): Do `SPIFFS.format()` or OTA `Update.end(true)` approach any
  watchdog limit on `framework-arduinoespressif32 @ 3.20017.241212`? Measure, don't
  assume.
- **OQ-4**: Does ESP-IDF NVS in this core skip identical-value writes (making
  `prefs_save()`'s full-key rewrite in `src/hal/prefs.cpp:98` harmless)? Read the
  bundled `nvs_set_*` implementation; if writes are not deduped, file a dirty-flag task.
- **OQ-5** (blocks T26): Does the web-flasher hosting (`flasher.sigurdos.dev`, see
  `firmware/README.md`) fetch binaries from the git repo or from release assets?
- **OQ-6** (blocks deleting `wifi_sta::connect()` after T3): Is the blocking variant
  retained intentionally for any validation build? Verify by building the full env
  matrix with the function removed locally.
- **OQ-7**: Where does the `AGENT_GUIDE.md` auto-sync run (commit `23c4751` is tagged
  `[auto]` but no workflow in-repo produces it)? Should the sync job be documented or
  brought in-repo?
- **OQ-8**: Is there a CI time budget for the native suite (4.4–8.3 min observed across
  machines)? At what duration should suite sharding be introduced?
- **OQ-9**: Are 8 MB-flash T-Deck variants in scope? `boards/t-deck.json` hard-codes
  16 MB/`default_16MB.csv`; supporting 8 MB needs partition-table variants and OTA-slot
  sizing.
- **OQ-10**: `update_wifi_status()` is declared in both `src/ui/ui.h:40` and
  `src/ui/screens.h:57` (same namespace). Intentional convenience or accident? If
  accident, remove one declaration in a T4-style hygiene PR.
- **OQ-11**: `CLAUDE.md`'s hardware table says the buzzer is "active low" (GPIO 46), but
  `src/hal/buzzer.h` pattern tables treat `level_high == true` as "sounding" and
  `buzzer_init()` idles the pin LOW. Which matches the hardware? Affects T12's comments
  only (do not change polarity without hardware verification).
