# Audit Fix Review — `dev` @ 300a22d1..6034d64e

**Scope:** 31 commits (26 numbered fixes covering issues #1466–#1490, one issue — #1480 — fixed across two commits; plus 4 merge commits and 1 docs commit), 68 files, +2026/-288. Four audit branches (`fix/audit-build-infra`, `fix/audit-security-auth`, `fix/audit-radio-map`, `fix/audit-concurrency`) plus 5 loose commits merged directly onto `dev`.

**Method:** Read AGENTS.md/CLAUDE.md for repo conventions. Read every commit's full diff plus the surrounding file at HEAD. Traced callers/entry points for security- and concurrency-relevant changes rather than trusting commit messages. Verified test coverage by reading the actual assertions, and in several cases by compiling/running isolated repros (ASan/UBSan on the UTF-8 truncation claim, a standalone repro of the `companion_adapter.cpp` namespace-qualification bug) rather than relying on descriptions. Read-only review — no source files were modified.

---

## Overall Release Recommendation

**Do not ship to hardware as-is. One RELEASE BLOCKER found: #1468.**

The diagnostic-writer synchronization fix (#1468) wraps `Serial.write()`/`Serial.availableForWrite()` calls inside a FreeRTOS `portENTER_CRITICAL()` section on the ESP32 build path. That disables interrupts on the calling core for the duration of a blocking USB-CDC driver call, which itself depends on interrupts/FreeRTOS primitives to make progress — a realistic hang/watchdog-reset risk on **every single log line**, not an edge case. This must be fixed (move the `Serial` I/O outside the critical section — copy the drainable range out under the lock, write unlocked) before this branch goes to a physical T-Deck.

Everything else is SAFE or SHOULD FIX (real but non-blocking robustness/coverage gaps — none of which are reachable in normal operation or represent a security bypass). Notably, one of the batch (#1475) closed a genuine, previously-shipping **PIN-bypass vulnerability** on contact/repeater detail routes — this alone makes merging the security-auth branch worthwhile once #1468 is resolved.

Two housekeeping items apply across the whole batch, not any one issue:
- **`SUMMARY.md`** was added at the repo root by branch work (touched by `2fc54936` and `c3fb6fde`) and is still present at HEAD. It isn't in CLAUDE.md's doc index and looks like leftover audit-branch scratch output — CLAUDE.md explicitly says not to create doc files unless asked. Recommend deleting or moving under `docs/`.
- **`cb2f4b7d` in isolation does not compile** (see #1480) — a bisect/cherry-pick hazard, not a problem for `dev` as merged.

### Verdict summary

| # | Issue | Classification |
|---|---|---|
| 1466 | verify factory reset storage erasure | SHOULD FIX |
| 1467 | propagate companion preference persistence failures | SAFE |
| 1468 | synchronize diagnostic ring writer | **RELEASE BLOCKER** |
| 1469 | marshal worker WiFi releases to loop | SAFE |
| 1470 | finalize local OTA reboot on main loop | SHOULD FIX |
| 1471 | keep GPS date and time coherent across midnight | SAFE |
| 1472 | reset keyboard shift state for decoded digits | SAFE |
| 1473 | size unread tracking for all conversations | SAFE |
| 1474 | restore source screen after PIN cancellation | SHOULD FIX (coverage gap only) |
| 1475 | gate contact and repeater detail routes | SAFE (closed a real PIN bypass) |
| 1476 | close map completion teardown race | SAFE |
| 1477 | bound UTF-8 truncation at the string terminator | SAFE (not a real fix — see below) |
| 1478 | stop GPS UART when demand is inactive | SAFE |
| 1479 | make keyed active regions transactional | SHOULD FIX (coverage gap only) |
| 1480 | qualify shared frequency bounds / enforce SX1262 bounds in remote RF tests | SAFE (bisect hazard noted) |
| 1481 | validate map tiles before resume and commit | SAFE |
| 1482 | enforce map downloader zoom limits | SAFE |
| 1483 | include nested PlatformIO dependencies in SBOM | SHOULD FIX |
| 1484 | correct and validate CODEOWNERS paths | SAFE |
| 1485 | clean up Pi hardware-test staging | SAFE |
| 1486 | keep companion matrix on one event loop | SAFE |
| 1487 | redact official matrix disconnect errors | SAFE |
| 1488 | guard repeater logout navigation by screen generation | SAFE |
| 1489 | honor room fetch request failures | SAFE |
| 1490 | scope OTA passwords to AP sessions | SAFE |

---

## RELEASE BLOCKER

### #1468 — synchronize diagnostic ring writer (`9aa2a17e`)

**Files:** `src/diagnostics/diagnostic_io.cpp/.h`, `test/test_log/main.cpp`

**Real bug being fixed:** `NonBlockingWriter` (aliased to `Serial`, used by `SIG_LOGE/W/D` and the telemetry protocol) had no synchronization at all across the tasks that write to it, despite being a ring buffer with wraparound index math (`append()`/`commit()`/`queue_head_`/`queue_tail_`). That's a genuine cross-task data race capable of corrupting the ring or overrunning bounds — worth fixing.

**Why the fix itself is dangerous:** All writer methods now wrap their body in a `LockGuard` around `portENTER_CRITICAL(&mux_)`/`portEXIT_CRITICAL(&mux_)`. Critically, `println()`/`println(const char*)`/`printf()` call `drain_locked()` **while holding that critical section**, and `drain_locked()` calls `Serial.availableForWrite()` and `Serial.write(...)` — i.e. the real Arduino/TinyUSB CDC driver is invoked from inside a section that has disabled interrupts on the calling core. `portENTER_CRITICAL` on ESP32 is documented as safe only for short, non-blocking work; the USB-CDC path can block and depends on interrupts/FreeRTOS primitives (semaphores, the USB task) to make forward progress. This is a textbook hang/watchdog-reset setup, and it fires on **every** log call, not just under contention.

**New bug introduced:** Yes — blocking driver I/O under a disabled-interrupt critical section.

**Test coverage:** `test/test_log/main.cpp`'s `DiagnosticWriterTest.ConcurrentProducersAndDrainerPreserveRecords` is a real multi-`std::thread` test, but it only exercises the **native** (`std::mutex`) build path — the `portENTER_CRITICAL` branch is ESP32-only and structurally invisible to `pio test -e native_test`. Nothing in the current test harness can catch this class of defect.

**Recommendation:** Re-scope the critical section to cover only the ring-buffer bookkeeping (append/commit/index math). Copy the drainable byte range out under the lock, release the lock, then call `Serial.write()` unlocked. This is a straightforward fix but must land before this branch is flashed to a physical T-Deck — do not treat this as a "should fix later" item.

---

## SHOULD FIX

Real, verified gaps that are not release-blocking — either because the failure mode requires a rare precondition (flash/SPIFFS fault, missing native test coverage for compiled-but-unexercised code) or because the defect is coverage-only with sound underlying logic.

### #1466 — verify factory reset storage erasure (`185ea9f1`)

The fix is correct where it matters: `eraseOwnedStorage()` now checks NVS/SPIFFS return codes and `mesh_wrapper.cpp`'s `factoryReset()` aborts (no reboot) on failure instead of blindly proceeding — closing the original bug (silent erase failure presenting a false "clean" first boot with contacts/identity/repeater passwords still on disk).

**Regression on the failure path:** `SPIFFS.end()` is called unconditionally *before* the erase sequence begins. On success this is fine (the device reboots right after). On failure, the function now returns early **without remounting SPIFFS**, leaving the device running with SPIFFS permanently unmounted for the rest of the session — message store, chat history, and contact store will silently fail until the user manually reboots, and the Settings UI error text ("Factory reset failed; BLE remains disabled") doesn't tell the user to reboot. `test_prefs.cpp`'s new tests cover the pure policy helper well but nothing exercises this SPIFFS-remount gap in `mesh_wrapper.cpp`.

### #1470 — finalize local OTA reboot on main loop (`54db3d3f`)

Correctly moves reboot finalization off the OTA worker task and onto the main loop, sequenced so `mesh::saveState()` runs before `SPIFFS.end()`/`ESP.restart()` — fixes real message/contact loss on every local OTA update. Sequencing between the worker's `stop_requested`/`active` flags and the main loop's `otaRebootMayFinalize()` check is sound (single cooperative main task, no TOCTOU).

**Unbounded retry risk:** if `mesh::saveState()` fails persistently (e.g. a corrupted SPIFFS at exactly the wrong moment), `loop()` re-arms `reboot_pending` and retries every tick forever — no backoff, no cap, no fallback to reboot-anyway. Because `system_reboot_allowed()` treats `isRebootPending()` the same as "OTA active," this also blocks the user's manual Settings → Reboot button, stranding the device on old firmware (the new image is already written/validated) until a hardware power-cycle. Narrow precondition, but worth a bounded retry before it's forgotten.

### #1474 — restore source screen after PIN cancellation (`4fb3eb9e`)

Root-cause-correct: PIN entry is a screen swap, not a history push, so the old `go_back()`-on-cancel over-shot one level past the true source screen (or no-op'd from Home). The fix explicitly re-dispatches `pending.source`, which is provably `== current` at cancel time for every navigation entry point. Logic generalizes correctly to non-Home sources and to `Back`/`Refresh`-type pending navigations.

**Coverage gap:** the only new test (`CancelClearsPendingRouteWithoutGrantingAccess`) exercises exactly `Home → SettingsGPS → cancel → Home` — the one case that happened to work "by accident" under the old code. No test exercises a non-Home source (e.g. `Chat → Settings → cancel`) or cancelling a `Back`/`Refresh`-type pending navigation, which are the cases this fix was actually needed for.

### #1479 — make keyed active regions transactional (`348bd2d4`)

The real fix (`src/mesh/regions.cpp`, `src/mesh/mesh_wrapper.cpp`) snapshots RegionMap/dirty-flag/NodePrefs/active-name state before mutating and genuinely rolls back all three stores (RAM region map, RAM transport-key store, NVS prefs) on any failure — this is a real three-store rollback, not a reorder.

**Coverage gap (structural, not incidental):** `native_test`'s `build_src_filter` does not compile `src/mesh/regions.cpp` or `src/mesh/mesh_wrapper.cpp` at all. All three new region tests link against `test/mocks/mock_mesh_wrapper.cpp`, which reimplements the same-named functions with **no snapshot/rollback logic whatsoever**. The tests validate the mock's simplified behavior, not the actual rollback code path this issue is about — that code is verified only by `pio run` compiling, never by `pio test` running it. A future refactor could silently break the rollback and nothing in CI would catch it.

### #1483 — include nested PlatformIO dependencies in SBOM (`6f04c534`)

The stated fix (nested-prefix regex tolerance for `├──`/`└──` lines) is correct and verified against the real lock file — `RTClib`/`RadioLib` are now captured. But running the generator against the actual `ci/platformio-packages.lock` shows `lvgl` — the UI framework — is **still completely absent** from the SBOM: the `PACKAGE` regex requires an `@`-versioned `required:` clause, which URL/local-path dependencies (including lvgl's archive URL) never have. `MeshCore`/`WebServer` only appear because of separate special-case code, not because of this fix. Since this SBOM feeds `security.yml`'s CVE-scan step, lvgl is never vulnerability-scanned, before or after this commit. The added test uses a synthetic fixture with a fabricated `@`-versioned requirement line that doesn't match the real lock format, so it doesn't expose the gap. The commit/docs summary ("transitive libraries are included") overclaims.

---

## SAFE

Fixes verified correct, with sufficient test coverage and no material regressions found. Minor notes are included where relevant but none block release.

- **#1467** propagate companion preference persistence failures — correct root-cause fix threading `prefs_set()`'s existing `bool` result up to a real BLE `ERR_CODE_FILE_IO_ERROR` response; includes good rollback-on-failure hardening for the enable/disable case. Tests directly exercise the failure path through `bridge.handleFrame`.
- **#1469** marshal worker WiFi releases to loop — real synchronization (atomic bitmask + `servicePendingReleases()` called once per loop iteration ahead of UI work), not cosmetic. Genuine multi-thread test of the atomic queue.
- **#1471** keep GPS date and time coherent across midnight — correctly separates a "coherent" time anchor (only advanced forward by GGA, re-anchored by RMC) from the raw fields, closing a real date/time-pairing bug around UTC midnight. Test reproduces the exact rollover scenario and would fail pre-fix.
- **#1472** reset keyboard shift state for decoded digits — correctly scopes shift-state reset to the only consumer (digit-vs-shifted-digit-symbol layout lookup); catch-all branch is safe given that scope. Tests are behavior-differentiating.
- **#1473** size unread tracking for all conversations — capacity now correctly matches the 32-conversation ceiling (16 channels + 16 DMs) instead of a stale hardcoded 16; test fills all 32 slots and confirms 33rd is still correctly rejected.
- **#1475** gate contact and repeater detail routes — **closes a genuine, previously-shipping PIN-bypass vulnerability.** `ContactDetail`/`RepeaterDetail` were absent from `is_pin_protected_route()`, so anyone with device access could view pubkeys and perform repeater login/logout/promote/demote without the PIN. Verified exhaustively that every call site (list taps, login-poll timers, promote/demote re-renders, map-screen callback, remote test controller) now routes through the gated `navigate_to_*` functions, and that refreshing an already-open detail screen is also now gated (closing a second, subtler bypass). Best test coverage of the whole batch. This alone justifies merging the security-auth branch.
- **#1476** close map completion teardown race — real TOCTOU fix (worker-task tile-completion check+enqueue vs. UI-thread teardown+drain) using a correctly-scoped `portMUX_TYPE` that, unlike #1468, contains only non-blocking queue operations — no interrupt-disabled driver I/O risk.
- **#1477** bound UTF-8 truncation at the string terminator — the claimed bug is not actually reachable: `utf8_sequence_is_valid()`'s continuation-byte loop necessarily hits the NUL terminator first (it never matches the continuation-byte pattern) and returns false before any out-of-bounds index is read, which was verified experimentally with an ASan/UBSan repro of the pre-fix logic against the exact malformed sequences added by the new test — no violation, identical output. Not a regression, just a mislabeled non-fix (a defensive refactor with no behavior change).
- **#1478** stop GPS UART when demand is inactive — a correct resource-lifecycle fix (not actually a concurrency fix — `sigurdos_gps_service()` has a single caller on the main loop, no race exists), correctly gated on the same demand inputs already feeding interval computation.
- **#1480** qualify shared frequency bounds / enforce SX1262 bounds in remote RF tests — bounds (150–960 MHz) are correct and match the pre-existing hardware-accurate `sx1262RadioConfigSupported()`. Note: `cb2f4b7d` in isolation does not compile (uses an unqualified `SX1262_MIN/MAX_FREQUENCY_KHZ` at global scope; the namespace those constants live in isn't opened until much later in the file) — fixed by the very next commit `46c4b741`, so `dev` as merged is fine, but this is a bisect/cherry-pick hazard. `companion_adapter.cpp`, where the break-then-fix happened, is outside `native_test`'s build filter, so this class of bug is caught only by a full firmware build.
- **#1481** validate map tiles before resume and commit — solid structural PNG validator (signature, chunk CRCs, IHDR/IDAT/IEND presence, exact 256×256 dims, zlib-decompressed size match, scanline filter-byte range) plus atomic tmp-file-then-`os.replace()` writes. Correctly scoped to corruption/truncation detection (not content correctness, which is out of scope). Firmware independently re-validates tiles at load time, so this is defense-in-depth. Tests exercise both the resume-redownload and non-PNG-200-not-committed cases.
- **#1482** enforce map downloader zoom limits — hard-rejects reversed/out-of-range zoom instead of silently swapping; firmware-matching bounds (0–18) verified against `map_renderer.h`. Note: the 0/18 constants are hand-duplicated between Python and C++ with no shared source of truth — low risk today, latent drift hazard.
- **#1484** correct and validate CODEOWNERS paths — fixes a real governance gap (`/KNOWN_ISSUES.md` vs actual `docs/KNOWN_ISSUES.md`, meaning the real file never required owner review); new `check_codeowners.py` logic is correct and is effectively enforced in CI via `pr-ci.yml`'s unittest-discovery step. Good test coverage.
- **#1485** clean up Pi hardware-test staging — real resource-leak fix (`/tmp/sigurdos-hw-*` directories on the Pi gateway were never cleaned up), well-guarded with a `created` flag and a `--keep-remote` opt-out. Minor notes: cleanup failure in the `finally` block can mask the original error's top-level message (diagnostic-quality only), and 1-second-resolution directory names remain a latent collision risk under concurrent jobs (pre-existing, made marginally worse now that cleanup actively deletes). This test suite (`scripts/hw_test/tests`) is not wired into any GitHub workflow.
- **#1486** keep companion matrix on one event loop — correctly diagnosed and fixed a real asyncio hazard (three separate `asyncio.run()` calls creating three different event loops that async client objects, e.g. bleak's BLE client, were passed between). Incidentally also fixes a second bug where disconnect could be skipped if the matrix raised partway through. Best test of the build-infra batch — loop-identity assertions genuinely exercise the fixed defect.
- **#1487** redact official matrix disconnect errors — correctly closes the one remaining unredacted `catch` block in `matrix.mjs`, verified via grep that no other unredacted error path remains. Test is a static source-regex match rather than a behavioral check (acceptable given `main()` does real hardware I/O), so it's coverage-by-proxy, not coverage-by-behavior.
- **#1488** guard repeater logout navigation by screen generation — closes a parity gap (contacts already had this guard, repeaters didn't): a bare untracked LVGL timer could fire `go_back()` against whatever screen the user had since navigated to. Layered with proactive timer cancellation on re-entry and on screen delete, so the generation check is genuine defense-in-depth rather than the sole guard. Pairs correctly with #1475 (still routes through `authorize_route`).
- **#1489** honor room fetch request failures — fixes a real "lying UI" bug (Fetch button showed success and navigated to Chat even when the mesh request never went out, e.g. `radioTxAllowed()` false or no pending-request slot). Traced the control flow and confirmed the dialog genuinely stays open on failure. Test coverage is weak/near-tautological (tests a two-line ternary directly, doesn't exercise the actual dialog-deletion-skip wiring), but the underlying fix is correct and low-risk.
- **#1490** scope OTA passwords to AP sessions — real security-hardening fix for a stale-password leak (`ap_password` was a static buffer never cleared between sessions; a prior AP-mode session's password could leak from `getAPPassword()` during a later STA-reuse session). Correctly uses an atomic flag plus `secureWipe()` at both `start()` entry and `cleanupServer()` teardown, so the fix survives restarts. The merge conflict resolution in `wifi_ota.cpp` (6034d64e) was checked directly against both parent diffs and correctly preserves both the `reboot_pending` refusal and the password-wipe/reset logic with no dropped hunks.

---

## Notes on the merge commits

- **26207e11** (build-infra) — clean merge, no conflicts to verify.
- **d116988f** (security-auth) — clean merge, no conflicts to verify.
- **68ccd409** (radio-map) — clean merge, no conflicts to verify.
- **6034d64e** (concurrency) — resolved a real conflict in `src/hal/wifi_ota.cpp` between #1490's password-wipe/reset addition and a `reboot_pending` refusal block already on `dev` from a different branch. Verified line-by-line against `git show c3fb6fde -- src/hal/wifi_ota.cpp`: both sides are present and correctly ordered (the reboot-pending refusal runs before any credential-state mutation, which is arguably better than either side had alone). No hunks dropped.

---

## Action items before merge/release

1. **Blocking:** Fix #1468 — remove `Serial` I/O from inside `portENTER_CRITICAL` in `diagnostic_io.cpp`'s `drain_locked()` path.
2. Delete or relocate the stray `SUMMARY.md` at the repo root.
3. File follow-up issues for the SHOULD FIX items above (SPIFFS-remount-on-failure in #1466, saveState-retry-forever in #1470, PIN-cancel test coverage in #1474, region-rollback test coverage in #1479, lvgl missing from SBOM in #1483) — none block this merge, but they're real gaps worth tracking.
4. Consider adding `src/mesh/regions.cpp` and `src/mesh/mesh_wrapper.cpp` (or targeted fault-injection mocks) to `native_test`'s build filter so #1479's rollback logic — and similar future region/mesh changes — get real automated coverage instead of only compiling.
