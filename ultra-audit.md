# SigurdOS T-Deck ultra audit

**Status (2026-08-14):** Findings from this audit were tracked through GitHub issues and the
[`fix/1.0-readiness`](https://github.com/hermes-gadget/SigurdOS-tdeck/tree/fix/1.0-readiness)
campaign. Resolved items are closed on the issue tracker (e.g. #1547, #1548, #1549, #1554, #1555,
#1556, #1561); remaining open items are listed in [issues](https://github.com/hermes-gadget/SigurdOS-tdeck/issues).

**Audit date:** 2026-08-13

**Repository:** `hermes-gadget/SigurdOS-tdeck`

**Audited commit:** `4532aec9502d4387f588a85842f21a978d7ea16a`

**MeshCore gitlink:** `c5787ee46124d540944ea238ff443f8d87ca0899`

**Tracking issue opened for this audit:** [#1558](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/1558)

## Executive verdict

This revision is **not ready to be treated as a stable release**. It has a large and often thoughtful test suite, strong artifact-layout validation, pinned CI actions, bounded data structures in many critical paths, and successful production builds. Those strengths do not offset the following release-blocking facts:

1. The required PR/release gates are red on the audited `dev` commit. The hosted run fails both the native-test job and static-analysis job, and a local run of the mandatory Python tests has nine failures and one error.
2. Release evidence can be fabricated with arbitrary HTTPS URLs, and the machine-enforced stable-release checklist omits several requirements that the project itself declares mandatory.
3. The companion golden-frame gate validates an opcode, not the rest of a frame. Mutating every non-opcode byte of a fixture can still pass.
4. Several runtime paths have unsafe lifecycle or concurrency behavior: the hardware-reproduced local-OTA/diagnostic crash, the background SPIFFS warm task across resets and unmounts, map discovery shared state, display wake on shared SPI, and synchronous SD copying.
5. Several paths violate their promised durable or per-client outcome: contact imports, per-client message replay, radio send/history persistence, first-boot BLE PIN generation, and GitHub OTA state checkpointing.
6. The security model omits the shipped unauthenticated plaintext TCP and WebSocket administrative transports, while another project document says the TCP mode is absent.

A stable release should be held until blocker and high-severity findings are fixed or explicitly/publicly accepted, their tests run in CI, and the stable-release contract is made internally consistent. Section 11 defines the stricter baseline and open-beta-blocker criteria for any RC/tag. Medium findings include real data-integrity, framing, resource, and user-expectation defects; they should not be dismissed as polish.

## Scope, method, and limitations

This was a read-only product audit except for creating this report, initializing the pinned submodule, installing build/test dependencies, and producing ignored build artifacts. The review covered:

- all tracked first-party source, test, workflow, script, release, security, and architecture areas;
- the pinned MeshCore call sites needed to reason about SigurdOS ownership and lifecycle behavior;
- boot, watchdog, display, shared SPI, SD, SPIFFS, NVS, sleep, OTA, diagnostics, companion BLE/TCP/WS, mesh persistence, chat/map/QR navigation, release publication, SBOM generation, and evidence validation;
- native, integration, production-build, coverage, dependency, font-generation, and repository-contract checks;
- open and recently closed upstream issues relevant to apparent defects, to separate known work from regressions and incomplete remediations.

The repository contains 746 tracked files: 307 under `src/`, 246 under `test/`, 88 under `scripts/`, and 43 under `docs/`. Review was systematic, but this is not a mathematical proof that every path is defect-free. In particular:

- no new physical T-Deck testing was performed during this audit. Timing-sensitive findings are source/test-gap confirmed unless stated otherwise; **RUN-01** additionally relies on the existing 4/4 hardware reproductions and decoded coredump in #1557;
- no destructive OTA, eFuse, power-cut, SD hot-removal, or long-duration battery experiment was performed;
- third-party libraries were reviewed at trust boundaries and relevant call sites, not line by line in their entirety;
- local sanitizer execution was blocked by the host's broken ASan runtime linkage, but the hosted sanitizer job for this exact commit passed;
- local coverage numbers differ slightly from hosted Ubuntu because this host uses GCC 16.1.1; both numbers and the reason for not treating the local delta as a repository failure are recorded below.

### Severity and confidence

| Severity | Meaning in this report |
|---|---|
| **Blocker** | The release/PR pipeline cannot complete, or a reproduced active crash prevents a safe release. |
| **High** | Credible loss/corruption, security-boundary misrepresentation, deterministic resource failure, cross-client correctness failure, or a release control that can be bypassed. |
| **Medium** | Material correctness, durability, availability, resource, or operator-expectation defect that needs scheduled remediation. |
| **Low** | Maintainability, consistency, portability, or incomplete-product behavior with contained immediate impact. |

Unless explicitly marked as an observation, every finding below is **confirmed from a concrete source path and failure sequence**. Dynamic reproductions are called out separately. Claims that did not survive validation were omitted; for example, SD hot-removal was downgraded after checking ESP-IDF's VFS teardown behavior rather than being reported as a speculative use-after-free.

## Verification record

### Successful checks

| Check | Result |
|---|---|
| `pio test -e native_test -v` | **Pass:** 124 test targets, 1,685 cases; 1,683 passed and 2 intentionally skipped. |
| `pio test -e native_mesh_integration -v` | **Pass:** 5/5 cases. |
| Pinned MeshCore contact-index native target | **Pass:** 2 GoogleTest cases. PlatformIO's aggregate incorrectly printed `SKIPPED`/zero despite the test binary's pass, a reporting quirk rather than a failed test. |
| Focused companion/transports/BLE/message-store/failover targets | **Pass:** 200/200 cases. |
| `pio test -e native_tdeck_sleep -v` | **Pass:** 7/7 cases. |
| `python3 -m unittest discover -s scripts/hw_test/tests -p 'test_*.py' -v` | **Pass:** 40/40 cases. |
| Official client `npm run test:privacy` | **Pass:** 5/5 cases. |
| `npm audit` for official client and `scripts/font-tools` | **Pass:** zero currently reported vulnerabilities in both lockfiles. |
| Regenerate emoji, Latin Extended, keyboard-layout, and Montserrat-8 fonts | **Pass:** generated assets were byte-identical to tracked assets. |
| `scripts/check_security_patches.py` | **Pass.** |
| `scripts/check_codeowners.py` | **Pass.** |
| `scripts/check_meshcore_pin.py` | **Pass.** |
| `scripts/check_release_contracts.py` | **Pass:** the requirements/template ID sets agree. This does not validate the semantic completeness of those IDs. |
| `scripts/check_native_coverage_inventory.py` | **Pass:** 37 directly linked production translation units and 75 reviewed exclusions. |
| `scripts/coverage_by_risk_domain.py` | **Pass:** reports 112 production translation units, 75 excluded (67.0%). |
| Canonical `SigurdOS_TDeck` build under pinned Python 3.12 tooling | **Pass:** RAM 177,632/327,680 bytes (54.2%); flash 2,806,929/6,553,600 bytes (42.8%); merged image 2,872,880 bytes. |
| `SigurdOS_TDeck_remote_test_radio` build | **Pass:** RAM 55.0%, flash 48.1%, merged image 3,220,336 bytes. |
| Six additional CI firmware profiles | **Pass:** BLE agent, debug, telemetry, USCA radio, USCA receive-only, and companion USB. |
| Hosted firmware, coverage, client, sanitizer jobs at audited commit | **Pass:** [workflow run 31453062505](https://github.com/hermes-gadget/SigurdOS-tdeck/actions/runs/31453062505). |
| Hosted full firmware validation matrix at audited commit | **Pass:** [workflow run 31453062512](https://github.com/hermes-gadget/SigurdOS-tdeck/actions/runs/31453062512). |
| Hosted security scan at audited commit | **Pass:** [workflow run 31453062508](https://github.com/hermes-gadget/SigurdOS-tdeck/actions/runs/31453062508). A later scheduled security run and nightly smoke also passed, but are not substitutes for the source findings below. |

### Failing required checks

| Check | Result |
|---|---|
| `python3 scripts/check_test_inventory.py` | **Fail:** undocumented native target `test_spi_shared_arbiter`. |
| `python3 scripts/check_doc_links.py` | **Fail:** stale `ROADMAP.md` anchor in `docs/RELEASE_EVIDENCE.md`. |
| `python3 -m unittest discover -s scripts/tests -p 'test_*.py' -v` | **Fail:** 182 run, 9 failures, 1 error. Release fixtures omit the newly mandatory SBOM, a security mutation test targets a string no longer present, and the test inventory is stale. |
| Hosted required workflow | **Fail:** native-tests job stops at inventory; static-analysis job fails doc links. Firmware, coverage, sanitizer, and client jobs pass. |

### Coverage result and scope

The hosted job at the exact audited commit reports:

- 90.2% lines (10,539/11,678);
- 95.9% functions (1,343/1,400);
- 69.9% branches (8,309/11,883), which passes the integer `70` gcovr threshold under gcovr's comparison/rounding behavior.

The same suite on this GCC 16 host reports 89.6% lines and 69.4% branches because the compiler emits a different set of coverable regions. The hosted job is authoritative for the gate, so the local variance is not classified as a failing repository control. The more important limitation is structural: the headline covers only compiled objects. The repository's own inventory says 75 of 112 production translation units (67.0%) are excluded, including main/boot, display, OTA, storage/SD, mesh/radio, companion, and 37 UI-screen files. This is finding **ASSURE-01**.

### Environment-only observations

- The system Python is 3.14, outside PlatformIO's supported 3.10–3.13 range; production builds succeeded in a clean Python 3.12 environment as the repository specifies.
- A local post-build `platformio_lock.py --check` wanted to add `tool-mkfatfs` and `tool-mkspiffs`, while the same lock check passed in hosted CI at the audited commit. This appears sensitive to local PlatformIO package resolution/state. It merits a reproducibility test on a second clean runner, but it is not presented as a confirmed clean-checkout failure.
- `native_sanitize` could not link locally because the host GCC 16 installation references a missing `/usr/lib64/libasan.so.8.0.0`. Hosted ASan/UBSan/LSan passed at the exact commit.

## Findings index

**Total:** 48 findings — 2 blocker, 15 high, 16 medium, and 15 low.

| ID | Severity | Finding | Tracking |
|---|---:|---|---|
| GATE-01 | Blocker | Required CI is red on merged `dev` | New audit regression; umbrella #1558 |
| RUN-01 | Blocker | Local OTA/diagnostic serialization still reproduces the active task-WDT crash | Existing #1557 |
| REL-01 | High | Arbitrary HTTPS URLs pass as completed release evidence | New |
| REL-02 | High | Machine stable-release requirements are weaker than the declared ship gate | New; adjacent #1554/#1556 |
| REL-03 | High | Golden-frame verifier checks only the opcode byte | New |
| REL-04 | High | SBOM components can carry a stale, contradictory PURL version | Residual of #1555 |
| REL-05 | High | Published SBOM is not a closed inventory of compiled dependencies | Residual of #1555 |
| REL-06 | Medium | The only release workflow publishes every tag as a prerelease | New |
| ASSURE-01 | Medium | Coverage headline excludes 67% of production translation units | Existing #1556 |
| SEC-01 | High | Security documentation omits shipped plaintext TCP/WS administration | New; adjacent #1548 |
| COMMS-01 | High | BLE initialization regresses MTU below valid maximum-frame capacity | New |
| COMMS-02 | High | Concurrent imports overwrite one MeshCore pending pointer and leak a packet | New |
| COMMS-03 | High | Advertised per-client durable replay is live-session RAM plus one global sent bit | New |
| DUR-01 | High | First-boot BLE PIN can be enabled after its persistence failure is ignored | New |
| HAL-01 | High | Background SPIFFS warm task is not quiesced across multiple terminal paths | Residual of closed #1546 |
| HAL-02 | Medium | GitHub OTA reboots without checkpointing recent mesh-driven contact state | New |
| HAL-03 | High | Synchronous whole-file SD copy can exceed the task watchdog while holding shared SPI | Residual of #1549 |
| HAL-04 | High | Display wake touches shared SPI without the outer arbiter | Residual of #1549 |
| UI-01 | High | Map discovery shares mutable state/resources across UI and worker tasks | New |
| UI-02 | High | Live and durable DM conversation identities are incompatible | New; incomplete #1191/#1196 |
| REL-07 | Low | Evidence `outcome` conflates “status recorded” with control implementation | New |
| COMMS-04 | Medium | Partial TCP writes are retried from byte zero, corrupting framing | New |
| COMMS-05 | Medium | TCP I/O can monopolize the loop and retain all slots indefinitely | New |
| COMMS-06 | Medium | Registry transport sessions start as already version-negotiated | New |
| COMMS-07 | Medium | One session reconnect cancels every session's binary requests | New |
| DUR-02 | Medium | Radio transmission precedes history persistence but failures are conflated | Residual of closed #1550 |
| DUR-03 | Medium | Contact import reports success before application and durable commit | New |
| HAL-05 | Medium | SD media loss invalidates still-live cleanup before operation boundaries | Residual of closed #1550 |
| HAL-06 | Medium | Preference snapshots can tear across three independently committed stores | New |
| HAL-07 | Medium | Repeater deletion can cross-associate a password after a torn update | New |
| HAL-08 | Medium | User shutdown sleeps even when the durable checkpoint fails | New |
| HAL-09 | Low | “Shut down” actually schedules an undisclosed full boot in 15 minutes | New |
| HAL-10 | Low | Wi-Fi scan has no absolute deadline if the driver remains Running | Residual resilience gap after closed #1187 |
| UI-03 | Low | Canonical DM routing scopes cannot fit in their persistence schema | New |
| UI-04 | Medium | QR display deletes the routed root without updating navigation state | New |
| UI-05 | Medium | Changing history capacity eagerly allocates nearly 1 MiB | New |
| UI-06 | Low | The documented message-detail feature is disconnected from production | Regression of closed #865 / PR #894 |
| UI-08 | Low | DM quick menu offers a channel-removal action that cannot succeed | New |
| UI-09 | Low | Hardcoded UI colors bypass the runtime theme contract | New |
| DOC-01 | Medium | Launcher/partition documentation describes the wrong canonical table | New |
| PRIV-01 | Low | A hardware MAC address is published despite the evidence privacy rule | New |
| TEST-01 | Low | “Data protection” test proves partition labels, not at-rest encryption | New |
| ASSURE-02 | Low | Both GPS fuzz entry points are nonfunctional and absent from CI | New |
| SUPPLY-01 | Low | Executed font-tool npm dependencies are outside audit/update coverage | New |
| UI-07 | Low | The seven-language picker produces a mostly English UI | Known limitation, not tracked exactly |
| CFG-01 | Low | Validation matrix uses Python 3.11 against a lock generated for 3.12 | New |
| DOC-02 | Low | Current-state documentation has test, locale, file, and anchor drift | New |
| MAINT-01 | Low | Build warning/source-contract debt hides portability and refactor failures | New |

## 1. Release, CI, and assurance findings

### GATE-01 — Required CI is red on merged `dev`

**Severity:** Blocker

**Confidence:** Reproduced locally and confirmed by hosted CI at the audited SHA

The repository's own mandatory gates do not pass:

- [`scripts/check_test_inventory.py`](scripts/check_test_inventory.py) discovers `test_spi_shared_arbiter`, but [`test/README.md`](test/README.md#L125) omits it. The checker requires exact set equality and fails before native execution in [`pr-ci.yml`](.github/workflows/pr-ci.yml#L47).
- [`docs/RELEASE_EVIDENCE.md`](docs/RELEASE_EVIDENCE.md#L229) links to `ROADMAP.md#14-how-to-ship--phase-a-reliability-p1-do-first`; the actual heading is section 4 at [`docs/ROADMAP.md`](docs/ROADMAP.md#L76).
- The production artifact contract now requires `sbom.cdx.json` in [`release_artifact_contract.py`](scripts/release_artifact_contract.py#L13), but the supposedly complete artifact fixture in [`test_release_artifacts.py`](scripts/tests/test_release_artifacts.py#L95) never creates it. The evidence fixtures in [`test_release_evidence.py`](scripts/tests/test_release_evidence.py#L11) likewise omit its digest. Eight otherwise targeted tests then fail early on “missing SBOM” instead of reaching their intended assertion.
- The multipart-boundary production check is now a combined predicate in [`check_security_patches.py`](scripts/check_security_patches.py#L23) and [`Parsing.cpp`](lib/WebServer/src/Parsing.cpp#L438), while [`test_security_patch_verifier.py`](scripts/tests/test_security_patch_verifier.py#L60) tries to delete a standalone source string that no longer exists. The mutation is empty, yet the test assumes it happened.

This is not merely stale developer tooling. The Python-suite regressions block both PR and tag/release workflows; the inventory check independently blocks PR CI and also fails inside that Python suite; the broken documentation link blocks PR CI. No clean change can traverse the complete declared pipeline until the baseline is repaired.

**Remediation:**

1. Add `test_spi_shared_arbiter` to the native catalog.
2. Correct the release-evidence Roadmap anchor to `#4-how-to-ship--phase-a-reliability-p1-do-first`.
3. Make one shared test fixture derive its required file set from the production artifact contract and emit a structurally valid minimal SBOM. Avoid duplicated hand-maintained artifact lists.
4. Mutate an exact token/clause that exists in the WebServer overlay and assert the replacement count before invoking the verifier.
5. Add a baseline-health job or branch protection that prevents merging when any mandatory job is already red.

**Acceptance test:** all repository Python tests, inventory, doc-link, native, and release-contract checks pass from a clean clone before any feature delta is applied.

### REL-01 — Arbitrary HTTPS URLs pass as completed release evidence

**Severity:** High

**Confidence:** Directly reproduced

[`verify_release_evidence.py`](scripts/verify_release_evidence.py#L67) accepts a URL when it has an HTTPS scheme and a network location. Its stronger binding logic at [lines 76–96](scripts/verify_release_evidence.py#L76) applies only when `REL-ARTIFACTS` already uses this repository's exact GitHub-release shape. Requirements validation at [lines 123–166](scripts/verify_release_evidence.py#L123) otherwise checks `outcome`, date, version, and a nonempty peer—not whether evidence exists, is immutable, was produced by CI, belongs to the tag, or says what the record claims.

A schema-2 document containing all 21 requirement IDs and `https://example.invalid/fabricated` for every `evidence_url` was accepted as “completed release evidence OK: 21 requirements.” The tests normalize this weakness by using `example.invalid` as passing evidence in [`test_release_evidence.py`](scripts/tests/test_release_evidence.py#L83) and [`test_release_artifacts.py`](scripts/tests/test_release_artifacts.py#L188).

[`build-release.yml`](.github/workflows/build-release.yml#L106) then binds the accepted claims into attestation inputs and automatically publishes at [lines 232–301](.github/workflows/build-release.yml#L232). A tag can therefore claim hardware, interop, soak, or privacy verification that never occurred.

**Remediation:** define an evidence trust model rather than a URL-format rule. Accept only allowlisted immutable GitHub Actions artifacts/runs, release assets, or structured issue evidence in the expected repository; resolve them through the GitHub API; verify repository, immutable object ID, conclusion, actor/approval, tag and commit; hash downloaded reports into the release attestation. Hardware-only claims should additionally require an approved GitHub Environment/manual reviewer whose identity is recorded.

**Regression test:** reject `example.invalid`, mutable branch/blob URLs, cross-repository runs, missing artifacts, successful unrelated runs, wrong-tag evidence, and changed report bytes. Pass only a locally mocked, fully bound evidence object.

### REL-02 — Machine stable-release requirements are weaker than the declared ship gate

**Severity:** High

The machine inventory in [`ci/release_evidence_requirements.json`](ci/release_evidence_requirements.json#L3) contains 21 checks centered on BLE, USB, a protocol corpus, and 10-minute soaks. It omits requirements that the project calls necessary in [`docs/ROADMAP.md`](docs/ROADMAP.md#L105), [transport/reliability sections](docs/ROADMAP.md#L165), and the stable target at [lines 203–218](docs/ROADMAP.md#L203):

- TCP and WebSocket interop;
- multi-client delivery/dedup/reconnect behavior;
- the greater-than-two-week battery result;
- a completed 12-hour release soak;
- Home, Settings, and Chat UI golden screenshots;
- final on-hardware navigation, keyboard, radio, and transport smoke.

The release PR template mentions final hardware smoke in prose, but [`verify_release_evidence.py`](scripts/verify_release_evidence.py#L41) only checks requirement tokens. There is also a semantic name collision: Roadmap “golden frames” are UI captures, while `INT-GOLDEN` is a companion protocol-byte corpus.

The result is a machine-valid “stable” release whose declared stable criteria remain unperformed.

**Remediation:** create separate release-candidate and stable requirement sets. Add explicit IDs and schemas for TCP, WS, multi-client, battery, 12-hour soak, UI screenshots, and final hardware smoke. Give UI screenshots and protocol frames unambiguous names. Make the tag class select the required policy, and require every Roadmap ship gate to map to exactly one machine ID or be explicitly retired.

### REL-03 — Golden-frame verifier checks only the opcode byte

**Severity:** High

**Confidence:** Mutation reproduced

The verifier in [`verify_companion_golden_frames.py`](scripts/verify_companion_golden_frames.py#L111) decodes each fixture and checks its symbolic command and `payload[0]`. It never validates field order, width, endianness, length, or any byte after the opcode. Source and code hashes record the files' identities, but do not independently derive the expected payload tail.

Mutating the valid `set_device_time` fixture from `0604030201` to `06deadbeef` still returns success for all 19 frames. This is especially misleading because the machine release contract calls the corpus interop evidence, while [`docs/RELEASE_EVIDENCE.md`](docs/RELEASE_EVIDENCE.md#L153) concedes that numeric codes alone are not a layout claim.

**Remediation:** generate each expected frame using an independent trusted implementation or execute the stock MeshCore client harness against the firmware codec. Decode frames back to typed fields and compare semantic values. Mutation-test every non-opcode byte, truncation, extension, endian swap, and length boundary.

### REL-04 — SBOM components can carry a stale, contradictory PURL version

**Severity:** High

**Tracking:** Residual defect in #1555 remediation

[`generate_platformio_sbom.py`](scripts/generate_platformio_sbom.py#L27) hardcodes upstream PURLs with current versions. `component()` at [lines 55–69](scripts/generate_platformio_sbom.py#L55) accepts a newly resolved component version but reuses the hardcoded PURL without comparing them. The supported dependency-refresh workflow updates `platformio.ini` and the package lock, not this mapping.

Direct reproduction: `component('RadioLib', '9.9.9', 'jgromes/RadioLib @ 9.9.9')` emits component version `9.9.9` alongside `pkg:github/jgromes/RadioLib@7.7.1`. Security scanning consumes the PURL in [`security.yml`](.github/workflows/security.yml#L89), so it may scan a release other than the code that shipped.

**Remediation:** derive PURL versions from resolved versions with explicit tag-normalization rules, or fail if the two representations disagree. Add a schema invariant that the version field equals the PURL version and a dependency-refresh test that changes every mapped dependency.

### REL-05 — Published SBOM is not a closed inventory of compiled dependencies

**Severity:** High

**Tracking:** Residual defect in #1555 remediation

The generator inventories PlatformIO's lock plus a special WebServer overlay in [`generate_platformio_sbom.py`](scripts/generate_platformio_sbom.py#L72). It omits compiled vendored code, including:

- LodePNG in [`lib/lodepng/lodepng.cpp`](lib/lodepng/lodepng.cpp#L1), used by [`map_renderer.cpp`](src/app/map_renderer.cpp#L37);
- QRCode in [`lib/qrcode/qrcode.c`](lib/qrcode/qrcode.c#L1), compiled by [`platformio.ini`](platformio.ini#L127) and used by [`qr_show.cpp`](src/app/qr_show.cpp#L27);
- the header-only Base64 shim in [`lib/base64/base64.hpp`](lib/base64/base64.hpp#L1), placed on the production include path by [`platformio.ini`](platformio.ini#L129) and included by pinned MeshCore; other vendored/header-only source should likewise be enumerated rather than implicitly assumed absent.

MeshCore is emitted under an upstream coordinate while the actual gitlink is fetched from the hermes-gadget mirror declared in [`.gitmodules`](.gitmodules#L1), and the pin carries project-specific fixes documented in [`KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md#L28). An upstream canonical PURL can be useful, but the fork/mirror, immutable commit, and local provenance must also be represented.

The release workflow describes the SBOM as a firmware dependency inventory, while [`release_artifact_contract.py`](scripts/release_artifact_contract.py#L109) checks only document shape, lock hash, and a nonempty component list—not closure over what was linked.

**Remediation:** maintain a generated manifest of every compiled vendored/PlatformIO component with canonical origin, actual fetch provenance, immutable version/commit, hashes, and license. Compare that manifest against build inputs and fail for unattributed compiled source. Include patched/fork metadata as CycloneDX external references/properties, not by pretending the fork is pristine upstream.

### REL-06 — The only release workflow publishes every tag as a prerelease

**Severity:** Medium

[`build-release.yml`](.github/workflows/build-release.yml#L3) triggers for every tag, and the GitHub release step hardcodes `prerelease: true` at [line 282](.github/workflows/build-release.yml#L282). The Roadmap defines the target as stable, non-RC `0.1.0` in [`docs/ROADMAP.md`](docs/ROADMAP.md#L203), while [`firmware/README.md`](firmware/README.md#L134) explains that prereleases do not satisfy GitHub's stable/latest alias behavior.

Even a fully validated `0.1.0` tag therefore cannot become the declared stable/latest release through the repository's only publisher.

**Remediation:** strictly parse and validate the tag as SemVer; derive prerelease from the suffix; reject malformed or version-mismatched tags; select the corresponding RC/stable evidence policy; and test the workflow logic for stable, RC, beta, and invalid tags.

### REL-07 — Evidence `outcome` conflates “status recorded” with control implementation

**Severity:** Low

The security posture explicitly says there is no device publisher-signature root of trust in [`SECURITY_MODEL.md`](docs/SECURITY_MODEL.md#L57) and [`PRODUCTION_ROOT_OF_TRUST.md`](docs/PRODUCTION_ROOT_OF_TRUST.md#L6). The `OTA-SIGNATURE` requirement is carefully worded as “publisher-signature status is recorded,” so the checked-in RC9 record's `outcome: pass` in [`beta-0.1.47-RC9.json`](release-evidence/beta-0.1.47-RC9.json#L101) can correctly mean that this recording requirement passed—not that a signature control exists. This is **not** evidence that the project falsely claims to implement signatures.

The residual schema weakness is ambiguity for downstream tooling and readers: [`verify_release_evidence.py`](scripts/verify_release_evidence.py#L147) permits only one generic `pass` outcome and the record has no structured `implemented`/`known_gap` field. A rendered summary that displays only ID plus outcome can make “the known gap was recorded” indistinguishable from “the control was implemented and verified.” The release template's N/A rule is correctly limited to unshipped paths and is not a substitute for known-gap status.

**Remediation:** separate `control_status` (`implemented`, `known_gap`, `not_applicable`) from `evidence_status` (`verified`, `failed`, `missing`). Let the tag policy decide which statuses are acceptable and render the distinction prominently in release notes. This is a clarity/auditability improvement, not a claim that the current requirement text is false.

### ASSURE-01 — Coverage headline excludes 67% of production translation units

**Severity:** Medium

**Tracking:** Existing #1556

The gcovr gate in [`pr-ci.yml`](.github/workflows/pr-ci.yml#L246) is real and useful for the production objects it compiles. It is not whole-firmware coverage. [`ci/native_coverage_policy.json`](ci/native_coverage_policy.json#L4) and [`coverage_by_risk_domain.py`](scripts/coverage_by_risk_domain.py#L73) disclose 112 production translation units, of which 75 (67.0%) are reviewed exclusions:

- 3 main/boot;
- 4 display;
- 4 OTA;
- 3 storage/SD;
- 4 mesh/radio;
- 2 companion;
- 37 UI screens;
- 18 other exclusions.

Many tests for these paths inspect source text or isolated policies rather than execute production objects—for example [`test_main_loop.cpp`](test/test_main_loop/test_main_loop.cpp#L18), [`test_tdeck_board.cpp`](test/test_tdeck_board/test_tdeck_board.cpp#L35), and [`test_mesh_session_lifecycle.py`](scripts/tests/test_mesh_session_lifecycle.py#L8). That approach catches accidental textual regressions but can remain green while runtime ordering, task ownership, or a refactor changes semantics. Several findings in this report occupy exactly those excluded areas. The 67% translation-unit exclusion is not equivalent to 67% of behavior being wholly untested: shared headers/policies are exercised elsewhere, and the repository now honestly publishes the denominator. This is a material assurance limitation, not a hidden coverage-control bypass.

**Remediation:** keep the honest inventory, but publish both object coverage and total-source inclusion prominently. Move HAL/runtime behavior behind injectable interfaces, compile more production units under host fakes, add ESP32 component/integration execution for task and driver lifecycles, and reserve hardware evidence for behavior that cannot be represented faithfully on host. Do not raise percentage thresholds as a substitute for expanding the denominator.

## 2. Security and companion-transport findings

### SEC-01 — Security documentation omits shipped plaintext TCP/WS administration

**Severity:** High

**Tracking:** New; related but broader than #1548

[`docs/KNOWN_ISSUES.md`](docs/KNOWN_ISSUES.md#L7) says Wi-Fi TCP companion mode is absent because there is no LAN authentication. [`docs/COMPANION_SUPPORT.md`](docs/COMPANION_SUPPORT.md#L60), however, documents production opt-in plaintext TCP on port 5000 and WebSocket on port 8765, persisted across reboot, supporting up to four clients, with no additional transport authentication. [`platformio.ini`](platformio.ini#L132) compiles the communications and WebSocket implementations into production. [`docs/SECURITY_MODEL.md`](docs/SECURITY_MODEL.md#L6) lists BLE, USB, and PIN boundaries but omits both LAN listeners; its sensitive-command discussion at [lines 27–31](docs/SECURITY_MODEL.md#L27) consequently frames companion administration as BLE/USB trust only.

These are not read-only telemetry endpoints. Companion protocol commands can mutate identity, contacts, channels, radio-related state, and other privileged state. Anyone able to connect on the reachable LAN is effectively an administrator under the current design.

The immediate defect is not that a deliberately plaintext trusted-LAN mode exists; it is that the authoritative security model and known-issues document tell operators contradictory things. #1548 addresses OTA AP isolation, but the same trust decision applies to ordinary STA/LAN deployments.

**Remediation:**

- centralize a four-transport trust-boundary table covering BLE, USB, TCP, and WS;
- call TCP/WS “plaintext administrative endpoints,” not merely companion connectivity;
- require a trusted isolated LAN, client isolation/firewall policy, and explicit opt-in warning;
- remove the claim that TCP is absent;
- document binding interface, discovery exposure, persistence, credential/non-credential behavior, and which commands are permitted;
- longer-term, add authenticated session establishment and channel confidentiality or deliberately scope the feature to physically controlled networks.

**Regression control:** a docs/source contract should enumerate every compiled/listening transport and require a security-model entry for it.

### COMMS-01 — BLE initialization regresses MTU below valid maximum-frame capacity

**Severity:** High

[`ObservedSerialBLEInterface::initializeConfigured()`](src/comms/observed_ble_interface.cpp#L94) calls the pinned upstream `SerialBLEInterface::begin()`, which requests ATT MTU 517 in [`SerialBLEInterface.cpp`](lib/meshcore/src/helpers/esp32/SerialBLEInterface.cpp#L27). SigurdOS then calls `BLEDevice::setMTU(MAX_FRAME_SIZE)`, where `MAX_FRAME_SIZE` is 176, and treats that as the required initialized state.

ATT notification payload capacity is MTU minus three bytes. [`BleFramePolicy`](lib/meshcore/src/helpers/esp32/BleFramePolicy.h#L9) therefore calculates only 173 payload bytes at MTU 176. The existing boundary test in [`test_ble_frame_queue.cpp`](test/test_ble_frame_queue/test_ble_frame_queue.cpp#L32) explicitly proves that 174- and 176-byte frames do not fit. Meanwhile the bridge accepts/builds frames through the 176-byte maximum in [`companion_bridge.cpp`](src/comms/companion_bridge.cpp#L1060) and [lines 1174–1206](src/comms/companion_bridge.cpp#L1174).

The second MTU call converts a compatible upstream initialization into one that rejects the firmware's own valid maximum-sized responses and pushes.

**Remediation:** retain the upstream 517 request, or request at least 179 and validate `negotiated_mtu - 3 >= MAX_FRAME_SIZE`. The queue should react to the negotiated peer MTU, not confuse frame size with ATT MTU.

**Regression test:** execute the real initializer behind a BLE-device fake, assert no smaller post-`begin()` MTU request, and transmit payload sizes 173, 174, and 176 under negotiated MTUs 176 and 179.

### COMMS-02 — Concurrent imports overwrite one pending MeshCore pointer and leak a packet

**Severity:** High

The companion bridge returns `RESP_CODE_OK` as soon as [`companion_bridge.cpp`](src/comms/companion_bridge.cpp#L1770) calls the adapter. The adapter forwards to `BaseChatMesh::importContact()` in [`companion_adapter.cpp`](src/mesh/companion_adapter.cpp#L697). Pinned MeshCore allocates a packet and blindly assigns it to the single `_pendingLoopback` pointer in [`BaseChatMesh.cpp`](lib/meshcore/src/helpers/BaseChatMesh.cpp#L557). Only the pointer that remains is processed and released in `BaseChatMesh::loop()` at [lines 957–970](lib/meshcore/src/helpers/BaseChatMesh.cpp#L957).

SigurdOS can dispatch one BLE, one TCP, and one WS command in the same `transports_loop()` pass in [`transports.cpp`](src/comms/transports.cpp#L172), called from [`main.cpp`](src/main.cpp#L514), before `mesh::loop()` runs at [line 551](src/main.cpp#L551). Two valid same-pass imports therefore produce this sequence:

1. import A allocates packet A, stores `_pendingLoopback = A`, and tells client A success;
2. import B allocates packet B, overwrites `_pendingLoopback = B`, and tells client B success;
3. MeshCore processes/releases B only;
4. A is neither applied nor released.

Repeated collisions can exhaust the bounded packet pool while clients have received success acknowledgements for lost contacts.

**Remediation:** make `importContact()` return `BUSY` without allocation when a loopback is pending, or replace the pointer with an owned bounded queue whose enqueue result is propagated to the caller. A stronger design combines this with **DUR-03** so success means both application and durability.

**Regression test:** inject simultaneous valid BLE/TCP/WS imports before one mesh loop, verify every accepted contact is applied exactly once, every packet is released, and overflow returns an explicit error.

### COMMS-03 — “Per-client durable replay” is volatile session fan-out plus one global sent bit

**Severity:** High

[`companion_bridge.cpp`](src/comms/companion_bridge.cpp#L745) copies unsent store records into the queues of clients that are connected at that moment. When one session advances synchronization, [lines 1334–1355](src/comms/companion_bridge.cpp#L1334) call `messageStoreMarkCompanionSent()`. [`StoredMessage`](src/mesh/message_store.h#L49) has only one `companion_sent` bit, and [`message_store.cpp`](src/mesh/message_store.cpp#L789) excludes globally sent records from later refills.

Failure sequence:

1. A and B are connected; an unsent record is copied to both volatile session queues.
2. A consumes/advances it, setting the one persistent sent bit.
3. B disconnects before consuming its volatile copy.
4. B reconnects; its queue is reset, and refill skips the globally sent record.

This contradicts the per-client reconnect/delivery semantics promised in [`COMPANION_SUPPORT.md`](docs/COMPANION_SUPPORT.md#L89). The two-client test in [`test_companion_protocol.cpp`](test/test_companion_protocol/test_companion_protocol.cpp#L1059) never reconnects B, and the reconnect test at [lines 1196–1225](test/test_companion_protocol/test_companion_protocol.cpp#L1196) has only one client.

**Remediation:** persist an acknowledged cursor/set per durable client identity, including a defined identity-retention/eviction policy, or explicitly redefine the feature as single-consumer/global delivery. Do not advertise per-client durable replay while durable state is global.

### COMMS-04 — Partial TCP writes are retried from byte zero, corrupting framing

**Severity:** Medium

[`TcpCompanionTransport::writeToClient`](src/comms/transport_tcp.cpp#L121) may successfully write a prefix, spin until its 120 ms deadline, and return false without disconnecting or returning the completed offset. Its send wrapper reports zero on false at [lines 193–215](src/comms/transport_tcp.cpp#L193). The bridge then retains/retries the complete frame from byte zero in [`companion_bridge.cpp`](src/comms/companion_bridge.cpp#L445).

The stream becomes `partial(frame) + full(frame)`, which a length-framed peer interprets as corrupted content and/or a bogus next header. Current transport fakes are all-or-nothing, so the native suite cannot express the failure.

**Remediation:** keep per-client transmit buffers and offsets, resume exactly at the unsent byte, and place an upper bound on queued memory. A simpler safe fallback is to disconnect immediately after any partial-write failure so a corrupted stream is never reused.

**Regression test:** fake write sequences such as 3 bytes, zero/backpressure, then remainder; assert the received stream contains one exact frame and that retry/disconnect behavior is deterministic.

### COMMS-05 — TCP I/O can monopolize the loop and retain all slots indefinitely

**Severity:** Medium

There are two related scheduling/resource failures in [`transport_tcp.cpp`](src/comms/transport_tcp.cpp):

- A write can spin/delay for 120 ms at [lines 121–140](src/comms/transport_tcp.cpp#L121). Broadcast serially visits four clients at [lines 217–229](src/comms/transport_tcp.cpp#L217), allowing roughly 480 ms of synchronous delay. Unsolicited mesh events broadcast before returning in [`companion_bridge.cpp`](src/comms/companion_bridge.cpp#L469), and transport service runs before mesh/radio service in [`main.cpp`](src/main.cpp#L513).
- Four accepted slots are reclaimed only after disconnection at [`transport_tcp.cpp`](src/comms/transport_tcp.cpp#L88). Receive drains all currently available input with no byte/time budget at [lines 159–176](src/comms/transport_tcp.cpp#L159), while [`TransportFrameParser`](src/comms/transport_frame_parser.h#L23) retains an incomplete frame forever. Four stale partial clients can occupy all capacity; one continuously readable client can dominate a pass.

**Impact:** radio latency, UI jitter, watchdog pressure, starvation of other clients, and listener denial of service on an already unauthenticated LAN endpoint.

**Remediation:** use bounded nonblocking per-client RX/TX queues; cap bytes and microseconds per loop; round-robin clients; track last progress, frame-start, and idle time; expire incomplete/idle sessions; surface drop/backpressure counters.

### COMMS-06 — Registry transport sessions start as already version-negotiated

**Severity:** Medium

The normal connection reset correctly sets `version_negotiated = false` in [`companion_bridge.cpp`](src/comms/companion_bridge.cpp#L430), and synchronization gates on it at [lines 1334–1339](src/comms/companion_bridge.cpp#L1334). But `TransportSession`'s default/reset state sets it true in [`companion_bridge.h`](src/comms/companion_bridge.h#L519) and [lines 548–570](src/comms/companion_bridge.h#L548). Network generation changes use that latter reset in [`companion_bridge.cpp`](src/comms/companion_bridge.cpp#L185).

`serviceTransportSessions()` materializes BLE, TCP, and WS through the same session/reset path in [`companion_bridge.cpp`](src/comms/companion_bridge.cpp#L307). A fresh or reconnected registry session on any of those transports can therefore start replay/synchronization before `CMD_DEVICE_QUERY`, contrary to [`COMPANION_SUPPORT.md`](docs/COMPANION_SUPPORT.md#L164). This can send data under unestablished version/capability assumptions.

**Remediation:** initialize every new generation as unnegotiated and share one reset routine across BLE/TCP/WS. Add fresh-session and generation-change tests for all three registry transports.

### COMMS-07 — One session reconnect cancels every session's binary requests

**Severity:** Medium

A transport generation change calls the global `_host->cancelBinaryReqs()` in [`companion_bridge.cpp`](src/comms/companion_bridge.cpp#L185) and [lines 512–528](src/comms/companion_bridge.cpp#L512). [`companion_adapter.cpp`](src/mesh/companion_adapter.cpp#L902) forwards to a global cancellation that clears all companion requests in [`sigurd_mesh_v2.cpp`](src/mesh/sigurd_mesh_v2.cpp#L611). Yet request ownership and response routing are explicitly per session in [`companion_bridge.cpp`](src/comms/companion_bridge.cpp#L1094).

An unrelated client loses a valid pending response whenever another client reconnects. This turns routine reconnect churn into cross-client denial of service and can make responses disappear without an error on the owning session.

**Remediation:** tag lower-layer requests with the owning transport/session generation and cancel only matching tags. Test two clients with interleaved requests while a third reconnects.

### DUR-01 — First-boot BLE PIN can be enabled after persistence failure is ignored

**Severity:** High

When no pairing PIN exists, [`companion_adapter.cpp`](src/mesh/companion_adapter.cpp#L118) generates one and calls `prefs_set()`, but ignores its return value and continues into initialization at [lines 1287–1318](src/mesh/companion_adapter.cpp#L1287). [`prefs_set()`](src/hal/prefs.cpp#L457) updates the cached `NodePrefs` object only after a successful durable save. On failure, `blePin()` reads the unchanged zero value and BLE is configured from it.

This violates the repository's otherwise explicit commit-before-side-effect policy. The transport can advertise with a predictable, non-durable zero PIN instead of the intended random six-digit credential, creating inconsistent and weaker pairing behavior. A static passkey value of `000000` is representable, so the defect is predictability/persistence—not that zero is necessarily rejected by the BLE stack.

**Remediation:** treat a non-durable generated credential as fatal for BLE activation. Persist and read back the PIN, then start the transport. Surface a repairable error to the UI and never substitute zero.

**Regression test:** inject NVS failure on first boot, assert BLE remains disabled, no zero PIN reaches the interface, and a later successful retry enables exactly the persisted PIN.

### DUR-02 — Radio transmission precedes history persistence but failures are conflated

**Severity:** Medium

**Tracking:** Residual of closed #1550

Direct-message send in [`companion_adapter.cpp`](src/mesh/companion_adapter.cpp#L315) transmits at lines 338–345, then stores history at lines 350–355 and returns false when the store ID is zero. Channel send follows the same ordering at [lines 368–397](src/mesh/companion_adapter.cpp#L368). The bridge maps false to a generic send error in [`companion_bridge.cpp`](src/comms/companion_bridge.cpp#L1385). Companion paths still enqueue a volatile UI record with `store_id == 0`, but no durable history record exists.

The direct UI API has the opposite caller-visible semantics for the same state: [`mesh_wrapper.cpp`](src/mesh/mesh_wrapper.cpp#L1523) and [lines 1543–1557](src/mesh/mesh_wrapper.cpp#L1543) transmit, call `meshStoreOutgoingMessage()`, ignore a zero/failure result, and return RF success. A companion caller may retry an on-air message that was reported failed, creating a duplicate; a UI caller is told success despite absent durable history. #1550 improved store-backend failover but did not make the radio/history outcome atomic or consistent across callers.

**Remediation:** use a durable outbox and idempotency/message key, or return a structured result such as `transmitted_not_persisted` that prevents blind retry and triggers local repair. Define which state is authoritative under storage loss.

### DUR-03 — Contact import reports success before application and durable commit

**Severity:** Medium

Even without the overwrite in **COMMS-02**, explicit contact import is acknowledged when a loopback packet is merely queued. The actual mutation happens later in MeshCore loopback processing, and contact persistence is deliberately debounced for up to 30 seconds in [`mesh_wrapper.h`](src/mesh/mesh_wrapper.h#L202). Power loss/reset, storage failure, or later auto-add/hop/capacity policy rejection in [`BaseChatMesh::onAdvertRecv()`](lib/meshcore/src/helpers/BaseChatMesh.cpp#L120) can therefore follow a success response without a durable contact. Initial packet-pool exhaustion itself returns false before OK; the pool leak/overwrite is the separate **COMMS-02** path.

The stock MeshCore companion example also returns immediate OK after `importContact()` in [`MyMesh.cpp`](lib/meshcore/examples/companion_radio/MyMesh.cpp#L1351), so SigurdOS inherited a protocol behavior rather than inventing it. It remains a SigurdOS product-contract mismatch: [`CONTACT_STORE.md`](docs/CONTACT_STORE.md#L23) says user-visible contact mutations commit before reporting success, while mesh-driven changes use the explicitly described deferred path. The protocol response does not tell the user which semantic applies.

**Remediation:** define a completion callback from loopback processing, synchronously or transactionally commit the resulting contact, and only then return success. If protocol latency requires an async model, return an operation ID/accepted status followed by an explicit completed/failed event. Combine tests with same-pass multi-transport imports and power-cut/failure injection.

## 3. HAL, storage, OTA, power, and runtime findings

### RUN-01 — Local OTA/diagnostic serialization still reproduces the active task-WDT crash

**Severity:** Blocker

**Tracking:** Existing #1557

**Evidence:** 4/4 hardware reproductions and coredumps recorded in the issue at this exact revision

The Settings UI invokes `ota::start()` synchronously from its LVGL callback in [`screen_settings_system.cpp`](src/ui/screens/screen_settings_system.cpp#L939). Before any OTA worker is created, `start()` tears down/initializes Wi-Fi/AP services and includes fixed 100 ms, 100 ms, and 250 ms delays in [`wifi_ota.cpp`](src/hal/wifi_ota.cpp#L228) and [lines 681–692](src/hal/wifi_ota.cpp#L681).

Diagnostics make this worse. Producer calls synchronously invoke the drain in [`diagnostic_io.cpp`](src/diagnostics/diagnostic_io.cpp#L70) and [lines 92–108](src/diagnostics/diagnostic_io.cpp#L92). The drain checks CDC capacity, releases its lock, and only later calls potentially blocking `Serial.write()` at [lines 120–157](src/diagnostics/diagnostic_io.cpp#L120). OTA workers also write raw serial output, so capacity/ownership can change between the check and write. The runtime heartbeat is serviced only later in [`main.cpp`](src/main.cpp#L567).

#1557 records 4/4 failed activations; its decoded coredump terminates in `drain_diagnostic_output -> Serial.write -> HWCDC::write`, and the OTA worker had not yet been created. This is an active, reproduced bootstrapping crash, not a theoretical watchdog concern.

**Remediation:** make OTA startup a single-flight state machine owned by a worker. The UI should enqueue a request and render state only. Diagnostic producers must enqueue complete records without draining; one serial-consumer task owns every `Serial` call and uses a bounded nonblocking policy. Remove direct serial writes from OTA tasks. Keep the runtime heartbeat independent of CDC availability.

**Acceptance test:** repeat AP activation, cancellation, retry, and concurrent diagnostic saturation on hardware hundreds of times with task-WDT and crash telemetry enabled; zero resets, bounded callback time, and deterministic progress are required.

### HAL-01 — Background SPIFFS warm task is not quiesced across terminal paths

**Severity:** High

**Tracking:** Incomplete remediation of closed #1546

Boot starts `storage-warm` in [`main.cpp`](src/main.cpp#L312). The worker delays 12 seconds, then its first SPIFFS write/garbage collection can take a measured 10–90 seconds in [`storage.cpp`](src/hal/storage.cpp#L34) and [lines 58–108](src/hal/storage.cpp#L58). The API contract in [`storage.h`](src/hal/storage.h#L45) says callers must not proceed with teardown when `storage_stop_warm()` fails.

The required join is missing from multiple terminal paths:

- local OTA unmount/restart in [`wifi_ota.cpp`](src/hal/wifi_ota.cpp#L726);
- Settings reboot in [`screen_settings_system.cpp`](src/ui/screens/screen_settings_system.cpp#L1481);
- companion reboot in [`companion_adapter.cpp`](src/mesh/companion_adapter.cpp#L712);
- BLE bond-rotation restart at [lines 1241–1252](src/mesh/companion_adapter.cpp#L1241);
- ordinary shutdown/deep sleep in [`mesh_wrapper.cpp`](src/mesh/mesh_wrapper.cpp#L2244) and [`tdeck_sleep_orchestrator.h`](src/hal/tdeck_sleep_orchestrator.h#L65).
- factory-reset terminal-failure paths call `ESP.restart()` directly in [`mesh_wrapper.cpp`](src/mesh/mesh_wrapper.cpp#L2325), including the branch reached precisely when `storage_stop_warm()` reports the worker did not stop at [lines 2413–2415](src/mesh/mesh_wrapper.cpp#L2413).

GitHub OTA, onboarding restart, radio restart, and the successful factory-reset path do join the task and serve as positive controls. The factory-reset failure path does not. The missing paths can unmount SPIFFS underneath a live task or reset/power-transition during filesystem metadata/GC activity, risking a crash or persistent filesystem damage.

**Remediation:** centralize all reboot, unmount, and sleep transitions behind one terminal preflight. Ordering matters because today's `storage_stop_warm()` also marks storage unavailable: quiesce new mutations, run checked persistence while storage is available, join/cancel warm and defer if it cannot stop, then unmount and reset/sleep. Alternatively split “cancel and join worker” from “mark filesystem unavailable.” No direct `esp_restart()`, `SPIFFS.end()`, or board sleep should remain outside the coordinator.

**Regression tests:** a host task fake must block in warm-write/GC while every terminal action is requested; assert the transition waits or aborts. Hardware fault tests should reset at each phase and verify filesystem mount/recovery and durable state.

### HAL-02 — GitHub OTA reboots without checkpointing recent mesh-driven contact state

**Severity:** Medium

The GitHub OTA worker marks download success/reboot pending in [`github_ota.cpp`](src/hal/github_ota.cpp#L606). Finalization at [lines 707–720](src/hal/github_ota.cpp#L707) joins warm storage, calls `SPIFFS.end()`, and restarts, but never calls `mesh::saveState()`.

[`mesh::saveState()`](src/mesh/mesh_wrapper.cpp#L2078) persists preferences, channels, identity, contacts, and regions. Mesh-driven contact changes are intentionally dirty/debounced for 30 seconds in [`mesh_wrapper.h`](src/mesh/mesh_wrapper.h#L202), with routine discovery/path mutations in [`sigurd_mesh_v2.cpp`](src/mesh/sigurd_mesh_v2.cpp#L746) and [line 1101](src/mesh/sigurd_mesh_v2.cpp#L1101); [`ui::loop()`](src/ui/ui.cpp#L149) services that checkpoint. A discovered/path/imported contact change during the download's final 30 seconds is therefore lost on an otherwise successful update. Normal region CRUD is synchronously transactional and is not part of this concrete trigger.

Local OTA does attempt a save, making the omission more likely accidental than policy. Explicit user contact/channel/identity/region mutations generally use synchronous commit-before-success paths, so this finding is deliberately scoped to recent debounced mesh-driven contact/path/import state and already-dirty retry state—not every kind of mesh configuration.

**Remediation:** after blocking new mutations and before marking storage unavailable, perform a checked mesh checkpoint. On failure, do not silently reboot; preserve the completed image, report a recoverable activation error, and allow retry/explicit force under a documented policy.

### HAL-03 — Whole-file SD copy can exceed the task watchdog while holding shared SPI

**Severity:** High

**Tracking:** Residual of open #1549

The file-browser Copy callback invokes `sigurdos_sdcard_copy_file()` inline on the UI/loop task in [`screen_file_browser.cpp`](src/ui/screens/screen_file_browser.cpp#L163). The HAL acquires the shared-SPI guard at [`sdcard.cpp`](src/hal/sdcard.cpp#L689) and holds it across the entire 1 KiB read/write loop, sync, and both closes through line 768. `delay(0)` at line 748 yields to scheduling but does not return from Arduino `loop()`.

The SD bus is configured at 4 MHz at [`sdcard.cpp`](src/hal/sdcard.cpp#L382). Even ignoring filesystem overhead, a 5 MiB copy requires about 20 seconds of aggregate read-plus-write wire time. Arduino feeds the task WDT outside the user's `loop()` in the framework main loop, while [`boot_watchdog.h`](src/hal/boot_watchdog.h#L15) configures a 10-second runtime timeout.

Framework and watchdog source establish a sufficiently-large-file failure threshold: the operation cannot return to the Arduino wrapper's feed point within ten seconds, leaving task-WDT reset, frozen UI, no mesh service, and no opportunity for radio to obtain the shared bus. This threshold is source-derived and still needs the report's large-file hardware acceptance test; it was not newly reproduced on a device during this audit.

**Remediation:** perform copy as an asynchronous/cooperative job. Bound each slice by bytes and elapsed time, release SPI between slices, service mesh/watchdog/UI, expose progress/cancel, fsync/rename atomically, and define cleanup after removal or cancellation. Never hold a shared peripheral lock across an unbounded user file.

### HAL-04 — Display wake bypasses the mandatory shared-SPI arbiter

**Severity:** High

**Tracking:** Residual of open #1549

The contract in [`spi_shared.h`](src/hal/spi_shared.h#L34) requires every display, SD, and radio bus touch to take the bounded outer guard. LVGL flush does so in [`display.cpp`](src/hal/display.cpp#L628). Runtime wake restore, however, calls `tft.setRotation()` and `tft.fillScreen()` directly at [lines 547–555](src/hal/display.cpp#L547), reached via pending wake at [lines 1097–1100 and 1130–1141](src/hal/display.cpp#L1097).

Wake during an SD/map/radio transaction can interleave independent LovyanGFX and SPIClass state, corrupt a transaction, or block beyond the display budget. Initialization-time direct TFT access is safe because other clients have not started; the defect is specifically runtime wake.

**Remediation:** acquire one display-owned outer guard for the full restore. If the bounded acquire fails, keep `wake_refresh_pending` set and retry later. Prefer LVGL invalidation to an unconditional full-screen bus fill when possible.

### HAL-05 — SD media loss invalidates live cleanup before operation boundaries

**Severity:** Medium

**Tracking:** Residual of closed #1550

Any relevant I/O error calls `SD.end()` immediately in [`sdcard.cpp`](src/hal/sdcard.cpp#L256). This occurs before the current operation closes/syncs its `FILE*` or `DIR*` in read, list, copy, write-at, and append paths at [lines 595–605](src/hal/sdcard.cpp#L595), [624–678](src/hal/sdcard.cpp#L624), [726–768](src/hal/sdcard.cpp#L726), [888–904](src/hal/sdcard.cpp#L888), and [923–937](src/hal/sdcard.cpp#L923).

Arduino `SDFS::end()` unregisters and frees the FAT/VFS context. In the pinned ESP-IDF, `esp_vfs_unregister_with_id()` nulls matching VFS references, and later VFS `close`/`closedir` rejects the missing registration rather than invoking normal FATFS cleanup. This does not establish a use-after-free. The ordering remains wrong: directory-owned storage cannot be released via normal `closedir`, sync/close cleanup cannot complete, and partial-file recovery semantics are lost.

**Remediation:** mark media loss pending, stop new opens, let the outer operation close every owned handle, then unmount at the operation boundary. Use a mount generation/refcount so stale operations fail cleanly without tearing down resources they still own.

### HAL-06 — Preference snapshots can tear across three independently committed stores

**Severity:** Medium

[`prefs_save()`](src/hal/prefs.cpp#L304) chains three separately committed operations:

- bulk preference fields at [lines 84–114](src/hal/prefs.cpp#L84);
- companion transport fields at [lines 120–136](src/hal/prefs.cpp#L120);
- language through another `Preferences` transaction at [lines 141–151](src/hal/prefs.cpp#L141).

[`prefs_set()`](src/hal/prefs.cpp#L457) updates the in-memory snapshot only if all three calls succeed. If bulk commit succeeds and the transport or language commit fails, the caller receives failure and RAM remains old, but reboot loads the new bulk values with stale additive values. A setting that visibly “failed” can silently apply after reboot as part of a torn snapshot.

**Remediation:** use one raw-NVS handle and one commit for the complete versioned snapshot, or write a complete inactive generation and atomically flip an activation key. Recovery must select only a generation with a valid checksum/schema and complete component set.

**Regression test:** inject failure/power loss at every NVS operation and reboot the loader; it must produce either the complete old or complete new snapshot, never a mix.

### HAL-07 — Repeater deletion can cross-associate a password after a torn update

**Severity:** Medium

**Security property:** credential integrity/isolation

[`removeRepeaterPassword()`](src/hal/prefs.cpp#L683) compacts a middle slot by copying the last repeater's name and password, changing count, and removing keys through separate Arduino `Preferences` operations. Each `putString()`/remove commits independently. A power cut after the moved name commits but before its password commits leaves the new name paired with the deleted slot's old password. The loader at [lines 659–677](src/hal/prefs.cpp#L659) accepts that pair and may send one repeater's secret to another.

**Remediation:** store each name/password pair in one authenticated/versioned blob, and update compaction plus count through one transaction or generation switch. Prefer stable slot IDs/tombstones over move-last compaction. Fault-inject every commit boundary and assert that no name can load with a password from another record.

### HAL-08 — User shutdown sleeps even when the durable checkpoint fails

**Severity:** Medium

[`mesh_wrapper.cpp`](src/mesh/mesh_wrapper.cpp#L2244) logs a `saveState()` failure but returns into [`coordinateShutdown()`](src/mesh/mesh_init_lifecycle.h#L27), which invokes sleep regardless of persistence success. [`test_mesh_wrapper_internal.cpp`](test/test_mesh_wrapper_internal/test_mesh_wrapper_internal.cpp#L211) explicitly codifies sleep-on-failure. The Settings UI promises “Save state and power off” in [`screen_settings_system.cpp`](src/ui/screens/screen_settings_system.cpp#L1291).

For a user-requested shutdown, that promise is false: unsaved preferences, channels, identity, contacts, or dirty region state can be lost. Message-store appends use a separate synchronous store and are not part of this checkpoint claim. A critical-battery shutdown may reasonably be best-effort, but that is a different policy. The watchdog is also stopped before synchronous persistence, reducing recovery if the filesystem hangs.

**Remediation:** make shutdown reason explicit. User shutdown should abort/retry and display the checkpoint error, with a separate confirmed force-off option. Critical-battery shutdown should use a bounded best-effort deadline. Keep a recovery watchdog until storage work completes.

### HAL-09 — “Shut down” schedules an undisclosed full boot in 15 minutes

**Severity:** Low

The UI action at [`screen_settings_system.cpp`](src/ui/screens/screen_settings_system.cpp#L1291) calls the default `mesh::shutdown(0)`. [`tdeck_sleep_orchestrator.h`](src/hal/tdeck_sleep_orchestrator.h#L15) converts zero to a 900-second timer and always enables that wake source. Only a still-critical battery timer wake re-sleeps early in [`main.cpp`](src/main.cpp#L246); a healthy device performs a full boot.

The behavior is intentionally documented in [`HARDWARE.md`](docs/HARDWARE.md#L711), but the control label does not disclose it. A user expecting powered-off storage can find the device active 15 minutes later.

**Remediation:** where hardware permits, distinguish indefinite user-off from recovery sleep. Otherwise label the action “Sleep for 15 minutes,” show the scheduled wake, and reserve “Shut down” for behavior matching ordinary expectations.

### HAL-10 — Wi-Fi scan has no absolute deadline if the driver remains Running

**Severity:** Low

**Tracking:** Residual gap after closed #1187

The scan acquires the shared Wi-Fi coordinator lease in [`wifi_ota.cpp`](src/hal/wifi_ota.cpp#L811). When the driver returns `WIFI_SCAN_RUNNING`, polling at [lines 862–871](src/hal/wifi_ota.cpp#L862) can return Running forever. Release occurs only on completion, error, or explicit cancellation at [lines 791–807 and 898–910](src/hal/wifi_ota.cpp#L791). The Wi-Fi Networks screen polls indefinitely in [`screen_wifi_networks.cpp`](src/ui/screens/screen_wifi_networks.cpp#L367).

If the driver wedges in `WIFI_SCAN_RUNNING`, the screen remains “Scanning” and the global lease is retained for the screen's lifetime, blocking STA, local OTA, and GitHub OTA. Leaving/deleting the screen calls cancellation and recovers the lease, so this is a missing resilience deadline and test—not a demonstrated permanent leak or known driver failure.

**Remediation:** set a wrap-safe absolute deadline when the lease/driver operation starts. On expiry, call `scanDelete()`, release ownership, publish a typed timeout error, and allow retry. Test a fake that returns `WIFI_SCAN_RUNNING` forever, including millis wraparound.

## 4. UI, application, and data-model findings

### UI-01 — Map discovery shares mutable state/resources across UI and worker tasks

**Severity:** High

Map discovery state, coverage, and cleanup flags are ordinary non-atomic mutable objects in [`map_renderer.cpp`](src/app/map_renderer.cpp#L253) and [lines 687–713](src/app/map_renderer.cpp#L687). A FreeRTOS worker reads and mutates them throughout [lines 433–478 and 1021–1325](src/app/map_renderer.cpp#L433), while the LVGL/UI task starts, polls, cancels, tears down, and requests xcache cleanup at [lines 1004–1018, 1331–1361, and 1389–1394](src/app/map_renderer.cpp#L1004). [`screen_map.cpp`](src/ui/screens/screen_map.cpp#L345) drives the polling/cancellation timer.

The atomic generation check at [`map_renderer.cpp`](src/app/map_renderer.cpp#L1084) happens only after several unprotected accesses, and cancellation does not advance the discovery generation. The generation therefore cannot establish a C++ happens-before relationship or reliably invalidate a canceled worker.

Rapid Map enter/start/cancel/exit/re-enter can cause:

- C++ data-race undefined behavior on phase, progress, pending scan, coverage, and result;
- stale coverage/progress from an old generation appearing in a new view;
- UI cleanup racing the worker's `DIR*` and xcache request lifecycle;
- inconsistent completion/cancellation and potential crash/resource misuse.

Existing map tests in [`test_map_renderer.cpp`](test/test_map_renderer/test_map_renderer.cpp#L341) check policies and source ordering, not concurrent production state.

**Remediation:** make the worker the sole owner of discovery state and resources. Send start/cancel commands with monotonically increasing generations through a queue. Publish immutable progress/result snapshots through a mutex-protected copy or atomic pointer handoff. Require a worker cancellation acknowledgement before deinit/reuse and make resource destruction occur on the owning task.

**Regression test:** deterministic two-task tests should pause at every discovery phase while cancel, screen destruction, and restart are injected. Run under ThreadSanitizer in a hostable extraction and under repeated hardware navigation with heap/resource telemetry.

### UI-02 — Live and durable DM conversation identities are incompatible

**Severity:** High

**Tracking:** Incomplete outcome of closed #1191/#1196

Live UI and inbound queue paths use a canonical stable identity `DM: <64-hex contact ID>` in [`chat_screen_open_dm()`](src/ui/chat_screen.cpp#L2724) and [`mesh_wrapper.cpp`](src/mesh/mesh_wrapper.cpp#L357). Durable records provide only `char conversation[32]` in [`message_store.h`](src/mesh/message_store.h#L12) and are written as `DM: <display name>` by incoming, outgoing, and ACK paths in [`companion_adapter.cpp`](src/mesh/companion_adapter.cpp#L149), [lines 350–355](src/mesh/companion_adapter.cpp#L350), and [`mesh_wrapper.cpp`](src/mesh/mesh_wrapper.cpp#L769) / [1523–1538](src/mesh/mesh_wrapper.cpp#L1523). Boot restoration trusts that stored name-key in [`chat_screen.cpp`](src/ui/chat_screen.cpp#L852).

One logical message is therefore shown live under a stable public-key identity but persisted under a mutable, non-unique presentation name. Consequences include:

- reboot or rename splits one contact's history into different conversations;
- two contacts with the same display name coalesce durable messages, creating attribution errors;
- distinct long names that share the same first 27 bytes also collide because [`formatDmConversation()`](src/mesh/mesh_wrapper_internal.h#L78) deliberately truncates `DM: ` plus the name into the 32-byte field;
- a restored ambiguous name-key may be unsendable through [`chat_screen.cpp`](src/ui/chat_screen.cpp#L2763) and [`contact_store.cpp`](src/mesh/contact_store.cpp#L472);

This is a data-model defect, not a cosmetic key-format inconsistency. In a private messaging UI, attributing durable history to the wrong peer is security-relevant integrity failure.

**Remediation:** version the message schema and store a full raw public key/contact ID as identity; keep display name as presentation metadata only. Migrate a legacy `DM: name` only when the name resolves uniquely. Preserve ambiguous orphan records visibly without silently assigning them. Make all live, persisted, unread, ACK, search, scope, and signal paths consume one typed conversation identity.

**Regression tests:** reboot after inbound/outbound DM; rename before/after reboot; two contacts with identical names; distinct names sharing 27-byte prefixes; contact deletion/reimport; legacy unique and ambiguous migrations; ACK/search/unread behavior across every case.

### UI-03 — Canonical DM routing scopes cannot fit in their persistence schema

**Severity:** Low

Named routing scopes are offered for any nonempty conversation, including DMs, in [`channel_menu.cpp`](src/ui/channel_menu.cpp#L39) and [lines 126–145](src/ui/channel_menu.cpp#L126). A canonical DM key requires 68 visible bytes (`DM: ` plus a 64-hex ID) and 69 including NUL. [`ChatScopePreference::conversation`](src/hal/prefs.h#L273) is only 38 bytes, and [`prefs.cpp`](src/hal/prefs.cpp#L531) rejects longer keys.

The UI correctly rolls back and shows failure in [`chat_screen.cpp`](src/ui/chat_screen.cpp#L2523), so this fails closed rather than silently transmitting without the requested scope. Nevertheless, every stable-ID DM presents an action that cannot succeed, and old short name-key scope records become orphaned when the same contact opens under its canonical ID.

Tests in [`test_channel_menu.cpp`](test/test_channel_menu/test_channel_menu.cpp#L65) use only `DM: bob`, and [`mock_prefs.cpp`](test/mocks/mock_prefs.cpp#L116) makes persistence always succeed.

**Remediation:** persist a typed raw contact ID/fixed binary key rather than a formatted conversation string. Version and migrate unique legacy records, and make the mock enforce production length/failure behavior.

### UI-04 — QR display deletes the routed root without updating navigation state

**Severity:** Medium

QR success and failure paths call low-level `show_screen()` directly in [`qr_show.cpp`](src/app/qr_show.cpp#L37), [lines 50–70](src/app/qr_show.cpp#L50), and [line 237](src/app/qr_show.cpp#L237). Callers include Channels and Contact Detail in [`screen_channels.cpp`](src/ui/screens/screen_channels.cpp#L302) and [`screen_contacts.cpp`](src/ui/screens/screen_contacts.cpp#L1631).

[`show_screen()`](src/ui/screen_loader.cpp#L8) synchronously deletes the outgoing root. Only `navigate_to()` updates router current/history state in [`navigation.cpp`](src/ui/navigation.cpp#L221). QR Back invokes ordinary `go_back()` whose stack pop is at [lines 314–323](src/ui/navigation.cpp#L314).

Concrete reproduction: Contacts → Contact Detail → QR → Back skips the deleted Contact Detail and returns to its predecessor. While QR is visible, `current_screen()`, refresh, lock, and trackball dispatch still identify the deleted origin.

**Remediation:** represent QR as a router-owned transient/parameterized route or modal. Record its exact origin, update current state, and restore the origin deterministically on Back for both encoding success and failure. Ban direct root replacement outside navigation except boot/onboarding paths with explicit contracts.

### UI-05 — Changing history capacity eagerly allocates nearly 1 MiB

**Severity:** Medium

[`trim_channel_history()`](src/ui/chat_screen.cpp#L299) unconditionally calls `ensure_channel_buffer()`. The settings setter applies it to all 24 possible conversation slots at [lines 3106–3117](src/ui/chat_screen.cpp#L3106), even slots that have never existed. [`ChatMessageBuffer::ensure()`](src/ui/chat_message_buffer.cpp#L15) always allocates 200 records regardless of the newly selected cap.

`ChannelMessage` is approximately 204 bytes from [`chat_message_buffer.h`](src/ui/chat_message_buffer.h#L14). Pressing Set—even for cap 8—therefore requests roughly `24 × 200 × 204 = 979,200` bytes of PSRAM. If PSRAM allocation fails, an eight-record fallback can consume about 39 KiB of internal DRAM, increasing pressure on LVGL, map, QR, networking, and radio tasks.

**Remediation:** trimming must never allocate. Iterate only buffers that already exist; allocate the configured capacity on first append; use an explicit grow/reallocation path if the setting increases; report aggregate memory impact in the dialog.

**Regression test:** expose allocator counters, set caps with zero/live/sparse conversations, and assert no allocation for nonexistent slots and bounded allocation for selected caps under PSRAM failure.

### UI-06 — The documented message-detail feature is disconnected from production

**Severity:** Low

**Tracking:** Regression/incomplete integration after closed #865 and merged PR #894

[`chat_screen_add_stored_msg()`](src/ui/chat_screen.h#L91) is declared but has no production definition. The durable-first inbound queue carries `store_id` in [`mesh_wrapper.h`](src/mesh/mesh_wrapper.h#L88) and [`mesh_wrapper.cpp`](src/mesh/mesh_wrapper.cpp#L360), but [`ui.cpp`](src/ui/ui.cpp#L153) forwards only through `chat_screen_add_msg_at()` and drops the ID. Bubble creation in [`chat_screen.cpp`](src/ui/chat_screen.cpp#L1415) has no long-press/detail callback. The buffer can retain IDs in [`chat_message_buffer.cpp`](src/ui/chat_message_buffer.cpp#L48), but ordinary append supplies the default zero.

The result is that the documented long-press detail view (route, RSSI, ACK, type) in [`CHAT_SCREEN.md`](docs/CHAT_SCREEN.md#L587) is unreachable for normal live messages, and exact durable-record lookup cannot occur.

**Remediation:** restore one production ingress that requires/propagates `store_id`; bind long press using stable buffer record identity rather than transient widget position; fetch via `messageStoreGetById()`; define behavior for non-durable/system messages; add a navigation/lifetime integration test.

### UI-07 — The seven-language picker produces a mostly English UI

**Severity:** Low

**Classification:** Documented limitation, but product-facing scope mismatch

Settings exposes seven selectable languages in [`screen_settings_display.cpp`](src/ui/screens/screen_settings_display.cpp#L660), while [`StringId`](src/i18n/i18n.h#L26) covers Home, Chat, and the language label only. Representative Map and Advertise content remains hardcoded English in [`screen_map.cpp`](src/ui/screens/screen_map.cpp#L94) and [`screen_advertise.cpp`](src/ui/screens/screen_advertise.cpp#L45). [`I18N.md`](docs/I18N.md#L17) accurately admits the immediate effect is limited to Home and Chat.

This is not a broken lookup or UTF-8 implementation; those tests are sound. It is an incomplete user-facing feature: selecting French, German, Spanish, Italian, Portuguese, or Dutch produces mixed-language navigation almost immediately.

**Remediation:** either label the selector “Home/Chat language (preview)” until coverage is broader, or complete typed IDs screen-by-screen. Add a no-new-hardcoded-UI-literal lint with exclusions for protocol/user data and render smoke tests for each locale, including truncation.

### UI-08 — DM quick menu offers a channel-removal action that cannot succeed

**Severity:** Low

[`channel_menu_build()`](src/ui/channel_menu.cpp#L126) adds “Leave channel” for every non-public conversation, including canonical DMs. The test in [`test_channel_menu.cpp`](test/test_channel_menu/test_channel_menu.cpp#L89) explicitly requires that DM behavior. On selection, [`chat_screen.cpp`](src/ui/chat_screen.cpp#L2389) passes the synthetic conversation-list index to [`removeChannel()`](src/ui/channel_menu.cpp#L150), which forwards it to the mesh channel table. DMs are appended after real channels in [`chat_screen.cpp`](src/ui/chat_screen.cpp#L489), are not mesh channel entries, and removal therefore fails and renders `ChatLeaveFailed`.

This is contained UX failure rather than data loss, but it presents a destructive-looking action that predictably cannot perform the operation the label implies.

**Remediation:** identify conversation kind explicitly. Omit Leave for DMs, or offer a separately defined “Close/delete local DM history” action with clear persistence semantics and confirmation. Change the test to reject channel-only actions for DM identities.

### UI-09 — Hardcoded UI colors bypass the runtime theme contract

**Severity:** Low

The repository's UI contract requires runtime theme variables/helpers and says not to hardcode colors. Production still contains literal colors in at least nine UI/application files. Examples include:

- pressed Home tiles in [`home_screen.cpp`](src/ui/home_screen.cpp#L305);
- shutdown/reset destructive buttons in [`screen_settings_system.cpp`](src/ui/screens/screen_settings_system.cpp#L1293);
- contact action buttons in [`screen_contacts.cpp`](src/ui/screens/screen_contacts.cpp#L1141);
- avatar color in [`chat_screen.cpp`](src/ui/chat_screen.cpp#L603);
- warning/destructive colors in Radio Setup/Node Stats;
- several Map canvas/route/missing-tile colors in [`map_renderer.cpp`](src/app/map_renderer.cpp#L1636).

Some cartographic colors may intentionally be semantic rather than theme surfaces, but they still need named palette roles and contrast review. Literal theme-surface/action colors do not change with the six runtime presets and can produce visually inconsistent or low-contrast states.

**Remediation:** add named runtime semantic roles (destructive background, warning, map water/land/route/missing, avatar, pressed surface), use existing helpers where applicable, and lint `lv_color_hex(0x...)` outside `theme.*`/an explicit allowlist. Render representative screens under every preset and check contrast.

## 5. Documentation, privacy, test-contract, and supply-chain findings

### DOC-01 — Launcher/partition documentation describes the wrong canonical table

**Severity:** Medium

The canonical production environment selects [`partitions_sigurdos_16MB.csv`](partitions_sigurdos_16MB.csv#L1) in [`platformio.ini`](platformio.ini#L99). The actual layout has a 0x4000-byte NVS partition at 0x9000 and a separate 0x1000-byte `nvs_keys` partition at 0xD000, followed by `otadata` at 0xE000.

[`LAUNCHER.md`](docs/LAUNCHER.md#L27), however, repeatedly says standalone SigurdOS uses stock `default_16MB.csv`; its standalone table at [lines 150–167](docs/LAUNCHER.md#L150) combines the span into 0x5000 NVS and omits `nvs_keys`. [`FEATURES_OVERVIEW.md`](docs/FEATURES_OVERVIEW.md#L262) repeats the stock filename.

Launcher detection may still rely on distinctions that happen to remain true, but flashing, recovery, offset, NVS-key, and security reasoning is being performed against a noncanonical layout. Documentation about partition geometry must be byte-exact because a mistaken offset can erase state or flash the wrong region.

**Remediation:** generate the standalone partition table in docs from the canonical CSV, include `nvs_keys` and every actual offset/size/flag, and retain only launcher contrasts that are true for the real table. Add a doc-contract test that parses Markdown and CSV into the same normalized rows.

### PRIV-01 — A hardware MAC address is published despite the evidence privacy rule

**Severity:** Low

[`firmware/README.md`](firmware/README.md#L45) embeds the T-Deck address `44:1b:f6:91:4f:0c` in on-device evidence. The release template says not to include device IDs, and [`RELEASE_EVIDENCE.md`](docs/RELEASE_EVIDENCE.md#L133) says never record them.

A hardware address is a persistent identifier that can link logs, screenshots, BLE/Wi-Fi observations, and issue reports if it belongs to a real contributor device or is reused elsewhere. Source alone does not establish that ownership/reuse, so this finding is a confirmed policy violation with conditional privacy impact. Publishing it also teaches contributors that the repository's privacy rule is optional.

**Remediation:** replace it with an anonymous fixture/device label, purge it from future evidence, and add a documentation lint for MAC/BSSID/device-ID patterns with explicit synthetic-test allowlists. If the address belongs to a real contributor device, consider history/redaction implications under the project's privacy policy.

### TEST-01 — “Data protection” test proves partition labels, not at-rest encryption

**Severity:** Low

[`test_storage_security_contract.py`](scripts/tests/test_storage_security_contract.py#L27) names its assertion `test_canonical_partition_table_protects_secret_bearing_data`, but it only searches CSV text for `encrypted` on `nvs_keys` and SPIFFS. [`sdkconfig.defaults`](sdkconfig.defaults#L7) explicitly disables flash encryption, and [`PRODUCTION_ROOT_OF_TRUST.md`](docs/PRODUCTION_ROOT_OF_TRUST.md#L6) / [`SECURITY_MODEL.md`](docs/SECURITY_MODEL.md#L57) correctly state that artifacts, flash, and NVS are unencrypted.

Partition flags without the required runtime/root-of-trust configuration do not establish confidentiality. A green test with “protects secret-bearing data” in its name can be cited as assurance for a control the project correctly says does not exist.

**Remediation:** rename it to a partition-metadata/layout contract and explicitly assert that it confers no current at-rest guarantee. If at-rest security becomes a requirement, test the generated production sdkconfig, bootloader/partition encryption state, NVS encryption initialization/key generation, device fuse posture, and recovery behavior—not just CSV text.

The first line of [`sdkconfig.defaults`](sdkconfig.defaults#L1) also references nonexistent `docs/EFUSE_AUDIT.md`; remove the dead reference or restore the intended document through the normal issue-first process.

### ASSURE-02 — Both GPS fuzz entry points are nonfunctional and absent from CI

**Severity:** Low

**Confidence:** Compile failure reproduced; dataflow inspected

The repository has two GPS/NMEA fuzz entry points, but neither currently exercises the parser:

1. [`fuzz_gps_nmea.cpp`](fuzz/fuzz_gps_nmea.cpp#L1) documents a standalone sanitizer compile command. That command fails because line 29 calls removed `arduino_mock::push_gps_byte`; the current mock exposes `Serial1.mock_queue_rx_bytes()` in [`test/mocks/Arduino.h`](test/mocks/Arduino.h#L190). Even after replacing the stale API, the harness calls `sigurdos_gps_service(false, 60)`. [`gps.cpp`](src/hal/gps.cpp#L748) computes an effective interval of zero for disabled background/no explicit demand, stops UART, and returns before `drain_gps_uart()`.
2. [`fuzz_nmea.py`](fuzz/fuzz_nmea.py#L1) pipes generated bytes to `.pio/build/native_sanitize/program --gtest_filter=GPS*`. Those GoogleTests do not read stdin or hand it to `Serial1`, so random input never reaches production parsing. The script also hardcodes `/home/ben/SigurdOS-tdeck`, making it workstation-specific, and treats a missing relevant test list as a warning before continuing.

No workflow invokes either harness. Both dormant developer utilities are stale/nonfunctional; sanitizer-green CI does not exercise this fuzz dataflow, and interface drift is undetected. No release document was found that claims fuzzing currently runs, so this is an assurance/maintenance gap rather than a production control bypass.

**Remediation:** keep one maintained libFuzzer/AFL++-style target or deterministic stdin driver that feeds bytes through the production parser abstraction, forces acquisition active, and asserts bounded completion. Derive paths from the script location, fail when the intended target is absent, seed with valid/malformed corpora, persist crashing inputs, and run a bounded sanitizer fuzz smoke in CI plus longer scheduled fuzzing.

**Regression test:** include a parser-side byte-consumption counter or known sentence that changes a fix snapshot; the harness must prove nonempty input was consumed before declaring an iteration successful. Compile and execute the exact documented command in CI.

### SUPPLY-01 — Executed font-tool npm dependencies are outside audit/update coverage

**Severity:** Low

The security workflow installs and executes dependencies from `scripts/font-tools` to regenerate checked-in C assets in [`security.yml`](.github/workflows/security.yml#L58). It audits and emits an SBOM only for `scripts/official-meshcore-client-test` at [lines 44–56](.github/workflows/security.yml#L44). [`.github/dependabot.yml`](.github/dependabot.yml#L1) covers pip and GitHub Actions, but neither npm directory.

The font outputs are compared byte-for-byte, which is a useful integrity check, and both npm audits were clean during this audit. The residual risk is lifecycle coverage: vulnerable or compromised transitive tooling can be installed/executed indefinitely without automated advisory review or update PRs, and the CI supply-chain SBOM omits that executed graph.

**Remediation:** add Dependabot npm entries for both directories; run `npm audit` for font-tools at the selected policy; generate/store its CI SBOM; keep `npm ci --ignore-scripts`, lockfile integrity, minimal permissions, and byte comparison. Where practical, pin/download the font source inputs by digest too.

### CFG-01 — Validation matrix uses Python 3.11 against a lock generated for 3.12

**Severity:** Low

[`build-validation-matrix.yml`](.github/workflows/build-validation-matrix.yml#L59) selects Python 3.11.13, while [`ci/requirements-platformio.in`](ci/requirements-platformio.in#L1) says the hashed lock is compiled for Python 3.12 and every other primary workflow selects 3.12.11.

The current wheels/markers happen to work, so this is not the present red gate. A future lock refresh can select hashes or markers valid for 3.12 but not 3.11, causing the full validation matrix to fail only after dependency change or merge.

**Remediation:** standardize the matrix on 3.12.11, or deliberately compile and test a multi-Python lock policy. Add a contract check that workflow Python versions belong to the lock's declared set.

### DOC-02 — Current-state documentation has test, locale, file, and anchor drift

**Severity:** Low, except the required broken anchor already covered by **GATE-01**

Examples found in current-facing material:

- [`ROADMAP.md`](docs/ROADMAP.md#L25) advertises 1,622 tests in several places; the audited suite has 1,685 cases.
- Its “Only 4 locales” item at [line 63](docs/ROADMAP.md#L63) conflicts with the same document's completed seven-language work and production's seven selectable languages.
- [`README.md`](README.md#L351) points to nonexistent `src/app/lodepng_psram.cpp`; the allocator implementation is [`src/app/lodepng_alloc.cpp`](src/app/lodepng_alloc.cpp).
- [`FEATURES_OVERVIEW.md`](docs/FEATURES_OVERVIEW.md#L266) names the wrong partition table, part of **DOC-01**.
- [`PROJECT_HISTORY.md`](docs/PROJECT_HISTORY.md#L80) contains 1,622/four-locale state in a historical narrative. That can remain if prominently marked as a dated snapshot; its “Where We Are Now” wording otherwise reads as current.
- [`RELEASE_EVIDENCE.md`](docs/RELEASE_EVIDENCE.md#L229) contains the gate-breaking Roadmap anchor documented in **GATE-01**.

**Remediation:** avoid volatile exact counts in narrative docs where a generated badge/table can be used. Add file/link/heading checks to the same mandatory gate, distinguish historical snapshots from current state, and generate partition/test/locale facts from their canonical sources.

### MAINT-01 — Build warning and source-contract debt hide portability/refactor failures

**Severity:** Low

The production builds pass, but observed warnings and the current gate failure expose maintainability debt:

- production does not explicitly select a C++ standard in the common embedded flags, while host targets use C++17 in [`platformio.ini`](platformio.ini#L441); the pinned MeshCore build emits extension warnings for C++17 inline variables under the embedded toolchain's older default;
- host coverage warns that [`TransportSession::reset()`](src/comms/companion_bridge.h#L548) uses `memset` on a non-trivial `PendingBinaryRequest`, making future field constructors/defaults easy to bypass;
- first-party/third-party warning volume includes sign, reorder, unused, pragma, and standard-version noise, reducing the chance that a new actionable warning is noticed;
- several Python/C++ “tests” assert literal source text or ordering. The security mutation failure in **GATE-01** demonstrates that they can fail due to harmless refactoring—or worse, perform no mutation while claiming to test one.

**Remediation:** explicitly align embedded and host language standards after verifying framework compatibility; value-initialize typed arrays instead of raw `memset`; classify/suppress reviewed third-party warnings at the dependency boundary; make first-party code warning-clean and enforce it; replace source-text contracts with executable behavior or AST/structured config checks. Every mutation test must assert that exactly the intended mutation occurred before evaluating the gate.

## 6. Existing upstream blockers and residual-risk mapping

The audit checked open issues before recommending code changes. These issues were already open at the audited SHA and remain part of the release decision even where this report found an adjacent or residual defect:

| Issue | Existing scope | Audit relationship |
|---|---|---|
| [#1547](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/1547) | OTA multipart pre-auth heap exhaustion and cross-task lifetime | Still an explicit beta blocker. This audit did not duplicate the issue; **RUN-01** and **HAL-01** are distinct startup/teardown paths. |
| [#1548](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/1548) | OTA AP isolation while optional TCP/WS listeners remain reachable | Still an explicit beta blocker. **SEC-01** is broader: normal LAN security documentation also omits those administrative listeners. |
| [#1549](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/1549) | Shared-SPI fault containment including radio | **HAL-03** and **HAL-04** are concrete remaining callers/lifetimes under the same open architecture gap. |
| [#1554](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/1554) | Release docs contradict MeshCore pin and soak evidence | **REL-02**, **DOC-01**, and **DOC-02** show additional semantic/document drift beyond the original contradiction. |
| [#1555](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/1555) | SBOM inclusion in release artifacts | The artifact is now required, but **REL-04** and **REL-05** show that component identity and inventory closure remain unsound. The test-fixture regression contributes to **GATE-01**. |
| [#1556](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/1556) | Honest coverage by risk domain | The inventory/reporting remediation passes and is valuable. **ASSURE-01** records the remaining 67% exclusion; several runtime/UI findings demonstrate why expansion is needed. |
| [#1557](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/1557) | OTA activation task-WDT crash | Reproduced active blocker, detailed as **RUN-01**. |
| [#1558](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/1558) | Whole-codebase audit report | Umbrella tracking for this document. Split actionable findings into focused issues before implementation. |

Recently closed issues were also used as regression context:

- closed #1546 introduced the storage-warm lifecycle contract, but **HAL-01** finds terminal callers that still bypass it;
- closed #1550 improved SD/message-store failover, but **HAL-05** and **DUR-02** remain at resource and outcome boundaries;
- closed #1191/#1196 standardized live DM identity, but **UI-02** finds the durable schema left behind;
- closed #865 / PR #894 introduced message detail, but **UI-06** finds the production event path absent;
- closed #1187 improved Wi-Fi scan lifecycle, but **HAL-10** lacks an absolute driver deadline.

No remediation should be merged under a vague “audit cleanup” PR. Per [`CONTRIBUTING.md`](CONTRIBUTING.md), each coherent change needs an issue, focused tests, and a reviewable scope. #1558 is appropriate as the report umbrella, not as a substitute for defect-specific acceptance criteria.

## 7. Cross-cutting root causes

The findings are not 48 unrelated mistakes. Five recurring design patterns explain most of them.

### 7.1 Success is overloaded

Several APIs return a Boolean or generic `pass`/protocol OK that can mean “queued,” “transmitted,” “stored,” “applied,” “durably committed,” or “status successfully recorded,” depending on the path:

- contact import: queued vs applied vs persisted (**COMMS-02**, **DUR-03**);
- radio send: transmitted vs recorded (**DUR-02**);
- shutdown/update: image/state save attempted vs durable (**HAL-02**, **HAL-08**);
- evidence: absence/status successfully recorded vs technical control implemented (**REL-07**).

**Design correction:** define explicit outcome types and state machines. Name states such as `Accepted`, `Applied`, `Persisted`, `TransmittedNotPersisted`, `CheckpointFailed`, and `KnownGap`. Only return protocol success for the level promised to callers.

### 7.2 Ownership is implicit at task/session boundaries

Map resources, serial drain, TCP streams, MeshCore requests, pending loopback, and replay state cross task/session boundaries without a single clear owner. Locks or generation counters are present in places, but they do not establish full lifecycle ownership.

**Design correction:** for every async resource, document one owner, one queue, cancellation acknowledgement, generation/identity, bounded capacity, deadline, and destruction task. A generation check is not synchronization; a mutex is not durable identity; a global cancellation function is not session ownership.

### 7.3 Terminal actions are scattered

Reboot, sleep, OTA activation, unmount, factory reset, and bond rotation each hand-roll some combination of persistence, warm-task joining, filesystem teardown, watchdog changes, and restart. Positive controls exist, but missing one step in another caller creates **HAL-01**, **HAL-02**, and **HAL-08**.

**Design correction:** one terminal-transition coordinator should accept a reason/policy and enforce:

1. single-flight transition and mutation quiescence;
2. reason-appropriate durable checkpoint;
3. worker cancellation/join;
4. filesystem/transport close;
5. watchdog policy;
6. reset, rollback, or sleep only after prerequisites succeed.

### 7.4 Contracts duplicate canonical facts

Artifact lists, SBOM versions, partition layouts, test inventories, locale/test counts, tag class, and release requirements are copied into multiple fixtures/docs/scripts. Drift already makes CI red and release semantics weaker than the Roadmap.

**Design correction:** parse or generate from one typed canonical source. Tests should consume production contract data rather than restate it. Documentation tables that govern flashing/release security should be generated or structurally compared.

### 7.5 Green tests sometimes prove syntax instead of behavior

The suite is broad, but source-string tests can prove that a token exists without executing timing, ownership, durability, or failure recovery. The security mutation test even failed to mutate while assuming it had. The partition “protection” test proves labels, not encryption.

**Design correction:** retain simple structural checks for constants/layout, but add executable state-machine, fault-injection, concurrency, and hardware tests at the boundaries being claimed. Test names must state exactly the property established.

## 8. Prioritized remediation plan

### Phase 0 — Restore trustworthy development gates

Do this before feature work:

1. Fix all four baseline causes in **GATE-01**.
2. Require the complete PR workflow to pass on `dev` and on a clean no-op branch.
3. Add a baseline/branch-protection alarm so mandatory jobs cannot remain red unnoticed.
4. Keep the actually open beta blockers #1547–#1549 and #1554–#1557 visibly release-blocking; regression-check the closed #1550–#1553 remediations rather than describing closed issues as unfinished.

**Exit criteria:** clean clone, all mandatory checks green; hosted and local required suites agree on pass/fail; no fixture silently omits a production-required artifact.

### Phase 1 — Remove active crash/corruption and release-control failures

Work can proceed in parallel by owner, but each track needs its own issue and tests:

- **OTA/runtime:** fix **RUN-01** and **HAL-01**, plus open #1547/#1548. Schedule the medium **HAL-02** checkpoint fix in Phase 2.
- **Shared resources:** fix **HAL-03**, **HAL-04**, and **UI-01** with explicit task/bus ownership.
- **Release trust:** fix **REL-01**, **REL-02**, **REL-03**, **REL-04**, and **REL-05** before stable publication.
- **Companion correctness:** fix **COMMS-01**, **COMMS-02**, and **COMMS-03**.
- **Identity/durability:** fix **DUR-01** and **UI-02**, including versioned migrations.
- **Security truthfulness:** fix **SEC-01** immediately even if authenticated LAN transport is scheduled later; users need the correct boundary now.

**Exit criteria:** no blocker/high finding remains without an explicit, reviewed risk acceptance; release evidence is independently resolvable and bound; hardware stress tests complete without resets; migration tests preserve/segregate DM history correctly.

### Phase 2 — Make failure behavior explicit and bounded

Address medium findings in clusters rather than one-off guards:

- per-client nonblocking TCP engine (**COMMS-04**, **COMMS-05**);
- typed session ownership/cancellation (**COMMS-06**, **COMMS-07**);
- durable operation-result model (**DUR-02**, **DUR-03**, **HAL-08**);
- GitHub OTA contact checkpointing (**HAL-02**);
- stable/prerelease tag semantics (**REL-06**);
- versioned atomic preferences/credential records (**HAL-06**, **HAL-07**);
- operation-boundary cleanup (**HAL-05**);
- router-owned QR navigation (**UI-04**);
- memory-aware chat allocation (**UI-05**);
- accurate canonical partition documentation (**DOC-01**).

### Phase 3 — Reduce recurring drift

Complete **REL-07**, **HAL-09**, **HAL-10**, **UI-03**, **UI-06**, **UI-07**, **UI-08**, **UI-09**, **PRIV-01**, **TEST-01**, **ASSURE-02**, **SUPPLY-01**, **CFG-01**, **DOC-02**, and **MAINT-01**. Generate volatile facts, restore fuzz dataflow, align toolchain standards, shrink warning noise, and replace source-text assertions with behavior tests. These are low severity individually but directly reduce the probability of another red baseline or misleading assurance claim.

## 9. Required regression and hardware matrix

The following matrix is the minimum credible verification for the proposed fixes.

| Domain | Required automated/fault test | Required hardware evidence |
|---|---|---|
| Release evidence | Fake/mutable/cross-repo/wrong-tag URL rejection; fetched artifact digest and approver binding | Reviewer-approved reports for every hardware-only stable ID |
| Golden frames | Mutate every payload byte/length/endian field; independent stock encoder/decoder comparison | BLE/USB/TCP/WS stock-client corpus, not opcode-only |
| SBOM | Resolved-version/PURL invariant; compiled-source closure; fork provenance | Compare release SBOM to map/link inputs from canonical build |
| OTA startup | State-machine and serial-owner scheduling under saturated queues | Repeated AP start/cancel/retry with task WDT, USB connected/disconnected |
| Terminal lifecycle | Block warm worker/save/unmount at every phase; assert abort/order | Power-cycle/restart/deep-sleep during SPIFFS GC and immediately after updates |
| Shared SPI | Forced contention and bounded retry for display/SD/radio | Large copy plus continuous receive/transmit plus screen sleep/wake |
| SD removal | Remove after each open/read/write/sync/close boundary | Physical hot-remove during read/list/copy/write; remount and partial-file checks |
| Preferences | NVS failure and reboot after every commit boundary | Controlled power cuts during settings/repeater mutation |
| Companion import | Same-pass BLE/TCP/WS, pool accounting, async apply/commit failures | Three-client repeated imports and reconnects |
| Replay | A/B disconnect/reconnect/ACK permutations across reboot | Multiple real clients over BLE/TCP/WS with durable identity retention |
| TCP stream | Short writes, zero progress, slowloris, partial frames, slot expiry, loop budget | Four slow/stalled clients while radio latency is measured |
| DM migration | Duplicate names, rename, reboot, delete/reimport, legacy ambiguity | Upgrade an image with real legacy history and verify attribution/search/ACK |
| Map discovery | Deterministic worker pause/cancel/restart; ThreadSanitizer extraction | Rapid enter/start/cancel/exit loops with SD/xcache and heap telemetry |
| Navigation | Contact Detail/Channel → QR success/error → Back/lock/refresh | Touch/keyboard/trackball route smoke |
| Memory | Allocator accounting for every history cap/PSRAM failure | Repeated history/map/QR use under heap-pressure telemetry |
| Wi-Fi scan | Driver returns Running forever, cancel, wraparound | Stuck/slow scan while attempting STA/local OTA/GitHub OTA |
| GPS fuzz | Compile the documented harness; prove every generated input reaches the production parser under ASan/UBSan; retain crashing corpus | Replay minimized corpus against UART acquisition on device |

Long-duration release evidence must be machine-associated with the exact commit and artifact digest. A test log pasted into a URL field is not sufficient.

## 10. What is working well

The severity of this report should not obscure the repository's good engineering controls. The following areas were reviewed and did not yield an additional independent finding:

- all external GitHub Actions are pinned to commit SHAs, and workflow permissions are scoped;
- release artifacts have strong filename/layout/offset/size/hash checks, checksum signing, and provenance attestation once inputs are accepted;
- the MeshCore gitlink, mirror, package lock, and declared pin agree;
- the patched WebServer overlay and CODEOWNERS literal paths pass their checkers;
- production, debug, telemetry, remote-test radio, BLE-agent, and USB profiles build successfully;
- native tests are numerous and fast, and the coverage inventory now honestly names excluded risk domains;
- atomic-file ready/temp recovery, bounded GPS parsing, QR size/capacity checks, and many fixed-capacity policies are thoughtfully designed;
- BLE secure-connections/MITM configuration, auth throttling/watchdog structure, parser bounds, companion structural validation/recovery, response routing, and identity commit-before-mutation paths outside the first-PIN exception did not expose another defect;
- tile request/completion ownership is generation-guarded; the map finding is specifically the separate discovery state;
- sampled LVGL timers in Map, Wi-Fi, Transports, Trace, Packets, Node Status, Telemetry, and Dashboard generally use the screen-lifetime/timer-owner machinery correctly;
- boot ordering/stage watchdog, battery ADC bounds, display allocation/retry policy, ordinary I2C single-task usage, keyboard/touch/trackball, GPS demand, OTA rollback/launcher gating, diagnostics record bounds, screen-sleep policy, and normal Wi-Fi coordinator transitions did not reveal another confirmed defect;
- GPIO45 intentionally cannot be used for ESP32-S3 RTC wake, ADC zero intentionally fails open, and initialization-time TFT access precedes other SPI clients—these were checked and not misreported as bugs;
- current official-client and font-tool npm dependency graphs audit clean, and regenerated font assets are reproducible.

These controls are a strong base. The main need is to make runtime ownership and release semantics as rigorous as the repository's best bounded-policy and artifact-layout code.

## 11. Release decision

**Decision: NO-GO for stable, and NO-GO for a release tag while required CI or an issue explicitly classified as a beta blocker is open.**

At minimum, resolve **GATE-01** and every currently open beta blocker—#1547–#1549 and #1554–#1557, including **RUN-01**—before any RC/tag. Regression-check the closed #1550–#1553 remediations. Before a stable release, additionally resolve or explicitly and publicly accept every remaining high finding, enforce the true stable evidence inventory, validate evidence objects rather than URL syntax, and run the hardware matrix above against the exact release artifact.

An RC may be reasonable only after that baseline and beta-blocker set is green/closed and its remaining known-risk label is honest. It must not be described as stable, must not use fabricated/unresolved evidence, and must not imply confidentiality, publisher signature, per-client durability, or test coverage that the implementation does not provide.

## Appendix A — Exact mandatory Python-suite failures

Local command:

```text
python3 -m unittest discover -s scripts/tests -p 'test_*.py' -v
Ran 182 tests
FAILED (failures=9, errors=1)
```

The failing cases and first causal error were:

| Test | Observed cause |
|---|---|
| `test_release_artifacts.ReleaseArtifactTests.test_complete_release_directory_passes` | Error: `missing release artifacts: sbom.cdx.json`. |
| `...test_cli_rejects_wrong_commit` | Expected commit mismatch, but validation stopped first on missing SBOM. |
| `...test_legacy_invented_hashes_cannot_pass_post_build_gate` | Expected produced-byte hash mismatch, but stopped on missing SBOM. |
| `...test_metadata_hash_and_full_alias_mismatch_fail` | Expected digest/size mismatch, but stopped on missing SBOM. |
| `...test_missing_file_and_string_offset_fail` | Offset subcase expected numeric-integer validation, but stopped on missing SBOM. |
| `...test_post_build_attestation_is_bound_to_every_artifact` | Attestation creation returned 1 due to missing SBOM. |
| `test_release_evidence.ReleaseEvidenceTests.test_completed_evidence_is_bound_to_tag_and_commit` | Fixture is missing artifact hash for `sbom.cdx.json`. |
| `...test_rel_artifacts_github_release_url_is_bound_to_expected_tag` | Same missing SBOM artifact hash. |
| `test_security_patch_verifier.SecurityPatchVerifierTests.test_each_security_line_is_mandatory` | Mutation subtest for `if (boundary.length() > 70)` changed nothing because production now uses a combined predicate. |
| `test_test_inventory.TestInventoryTests.test_repository_catalog_matches_test_directories` | Repository has undocumented `test_spi_shared_arbiter`. |

Fixing the fixture's first error must be followed by rerunning the entire suite; the early-failing cases may then reveal their intended assertion failures or pass.

## Appendix B — Finding ownership suggestions

| Area | Suggested primary owner/reviewer |
|---|---|
| Release/evidence/SBOM | CI/release maintainer plus security reviewer |
| Companion protocol/transports | Companion maintainer plus MeshCore pin owner |
| OTA/diagnostics/terminal coordinator | HAL/runtime owner plus on-device test operator |
| SPI/SD/display | Hardware HAL owner plus radio owner |
| Preferences/message durability | Persistence owner plus migration reviewer |
| Map/QR/chat/navigation | UI owner plus FreeRTOS/lifetime reviewer |
| Security/privacy documentation | Security owner plus release-documentation reviewer |

For high-risk fixes, the reviewer should not be the sole author of the test oracle. In particular, release evidence, golden frames, SBOM identity, DM migration, and per-client replay need an independent implementation or reviewer to avoid reproducing the same assumption in code and test.

## Appendix C — Minimal reproduction commands

Run these from the repository root at the audited commit with the pinned submodule initialized.

### Required clean-tree failures

```bash
python3 scripts/check_test_inventory.py
python3 scripts/check_doc_links.py
python3 -m unittest discover -s scripts/tests -p 'test_*.py' -v
```

Expected at this revision: the first reports undocumented `test_spi_shared_arbiter`; the second reports the stale Roadmap anchor; the suite reports 182 tests with nine failures and one error.

### Native baseline

```bash
pio test -e native_test -v
pio test -e native_mesh_integration -v
pio test --project-dir lib/meshcore -e native -f test_contact_index -v
```

Expected: 1,683 pass plus two intentional skips in the first command, 5/5 in the second, and two passing GoogleTest cases in the third. Inspect the test binary output for the third because PlatformIO may aggregate it as skipped.

### Golden-frame tail mutation

```bash
audit_corpus=$(mktemp)
jq '(.frames[] | select(.name == "set_device_time") | .hex) = "06deadbeef"' \
  test/fixtures/companion_golden_frames.json > "$audit_corpus"
python3 scripts/verify_companion_golden_frames.py --corpus "$audit_corpus" --json
rm -f "$audit_corpus"
```

Expected at this revision: exit zero and 19 accepted frames, despite replacing the valid tail `04030201`.

### SBOM version/PURL contradiction

```bash
PYTHONPATH=scripts python3 - <<'PY'
from generate_platformio_sbom import component
print(component('RadioLib', '9.9.9', 'jgromes/RadioLib @ 9.9.9'))
PY
```

Expected: the object says version `9.9.9` but contains a PURL ending in `@7.7.1`.

### Native coverage and denominator

```bash
audit_cov_root=$(mktemp -d)
python3.12 -m venv "$audit_cov_root/venv"
"$audit_cov_root/venv/bin/pip" install --require-hashes -r ci/requirements-coverage.txt
"$audit_cov_root/venv/bin/python" scripts/check_native_coverage_inventory.py
"$audit_cov_root/venv/bin/pio" test -e native_coverage -v
"$audit_cov_root/venv/bin/gcovr" \
  --root . \
  --filter '^src/' \
  --exclude-throw-branches \
  --fail-under-line 90 \
  --fail-under-branch 70 \
  --txt coverage.txt \
  --txt-summary
"$audit_cov_root/venv/bin/python" scripts/coverage_by_risk_domain.py
```

Compiler-dependent gcov region counts can move the headline slightly. The stable denominator result at this revision is 112 production translation units with 75 reviewed exclusions (67.0%).

### Broken GPS fuzz harness

```bash
g++ -std=c++17 -fsanitize=address,undefined \
  -I src -I src/hal -I test/mocks \
  fuzz/fuzz_gps_nmea.cpp src/hal/gps.cpp test/mocks/mock_arduino.cpp \
  -o /tmp/sigurdos-audit-fuzz-gps
```

Expected at this revision: compile failure stating that `push_gps_byte` is not a member of `arduino_mock`. After repairing that API call, **ASSURE-02** explains the separate disabled-service dataflow defect that must also be tested.

---

This report intentionally contains no product-code remediation. It establishes the baseline, evidence, and acceptance criteria so fixes can follow the repository's issue-first contribution process without mixing unrelated behavior changes into the audit artifact.
