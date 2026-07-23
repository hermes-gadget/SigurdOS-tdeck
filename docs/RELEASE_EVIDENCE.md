# Release evidence

Every release PR uses `.github/PULL_REQUEST_TEMPLATE/release.md`. The machine-readable inventory in `ci/release_evidence_requirements.json` is the source of truth; CI fails if a requirement disappears from the template.

## Publication gate

A tag is publishable only when `release-evidence/<tag>.json` exists at the tagged
commit and passes `scripts/verify_release_evidence.py`. This checked-in source
evidence names the tag, contains a fresh passing record for every requirement,
links to reviewed evidence, and identifies firmware and peer versions for
hardware checks.

The tag workflow then builds once, validates the complete artifact directory,
and generates `release-evidence.json` as a post-build attestation. That published
attestation adds the exact tagged commit, the SHA-256 of the checked-in source
evidence, and hashes of every release artifact. CI validates those hashes against
the actual bytes immediately before upload. Keeping commit and artifact hashes
out of the checked-in schema avoids an impossible self-reference: editing a
tracked evidence file changes the commit it would claim to identify. These
SHA-256 values prove byte consistency, not publisher identity: **checksums are
not signatures**. See [Security model](SECURITY_MODEL.md#firmware-update-trust).

Use this shape (repeat the requirement object for every ID in the requirements inventory):

```json
{
  "schema_version": 2,
  "tag": "<current-tag>",
  "generated_at": "2026-07-14T18:00:00Z",
  "requirements": [
    {
      "id": "INT-BLE",
      "outcome": "pass",
      "evidence_url": "<reviewed-evidence-url>",
      "tested_at": "2026-07-14",
      "firmware_version": "<current-tag>",
      "peer_version": "official-client-version"
    }
  ]
}
```

Validate checked-in source evidence before building:

```bash
python3 scripts/verify_release_evidence.py \
  --evidence release-evidence/<tag>.json \
  --tag "<tag>"
```

After flattening the release build into `artifacts/`, generate and immediately
recheck the byte-bound attestation:

```bash
python3 scripts/verify_release_evidence.py \
  --evidence release-evidence/<tag>.json \
  --commit "$(git rev-parse HEAD)" --tag "<tag>" \
  --artifacts-dir artifacts \
  --write-attestation artifacts/release-evidence.json
python3 scripts/verify_release_evidence.py \
  --attestation artifacts/release-evidence.json \
  --commit "$(git rev-parse HEAD)" --tag "<tag>" \
  --artifacts-dir artifacts
```

The attestation covers `firmware.bin`, both release-level merged/Launcher
images, `firmware-debug.bin`, the ESP Web Tools manifest, build metadata, and
all six web-flasher component/full/Launcher binaries. Legacy schema-1 evidence
is still parsed, but any declared hashes must cover and exactly match this full
set during post-build verification.

For tagged releases, the workflow also hashes every published input (including
the manifest, build metadata, and release evidence) into `SHA256SUMS.txt`, then
uses GitHub's OIDC identity to create `SHA256SUMS.sigstore.json`. Verify it with
Cosign before trusting the checksum file:

```bash
cosign verify-blob \
  --bundle SHA256SUMS.sigstore.json \
  --certificate-identity-regexp \
    '^https://github.com/hermes-gadget/SigurdOS-tdeck/.github/workflows/build-release.yml@refs/tags/' \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  SHA256SUMS.txt
sha256sum --check SHA256SUMS.txt
```

GitHub build-provenance attestations bind the same release files to the tagged
workflow run and can be checked with `gh attestation verify <file> --repo
hermes-gadget/SigurdOS-tdeck`. These publisher signatures do not enable ESP32-S3
secure boot or device-side signed OTA verification; those require a separate
device-key provisioning and rollback policy.

## Dependency pin policy

Every build dependency must resolve without local Git configuration:

- `lib/meshcore` is an exact gitlink. Its object must be publicly fetchable
  from the URL committed in `.gitmodules`; a maintainer-owned read-only mirror
  is acceptable, but `insteadOf` rules, unpublished fork objects, and deleted
  branches are not.
- PlatformIO application libraries use exact registry versions or immutable
  upstream commit archives. Branch names, version ranges, and mutable release
  downloads are not accepted as build pins.
- `ci/platformio-packages.lock` records the resolved package graph. A dependency
  PR reviews the declared pin, resolved graph, license/security inventory,
  affected firmware builds, and native tests together.

Normal build/test jobs key the shared immutable-download cache from the complete
declared and resolved dependency inputs. The dependency-refresh job is the one
exception: it restores a pre-update bootstrap cache before changing the selected
pin, then regenerates `ci/platformio-packages.lock`; the resulting PR is built
and tested by normal CI using the new complete hash.
The security inventory job also bypasses the package cache intentionally so its
scheduled dependency resolution observes the current registries and audit data;
it does not produce release build artifacts.

Before merging a gitlink change, initialize it in a clean clone rather than
relying on objects already present in a developer checkout:

```bash
clean_root="$(mktemp -d)"
git clone --recurse-submodules \
  https://github.com/hermes-gadget/SigurdOS-tdeck.git \
  "$clean_root/SigurdOS-tdeck"
git -C "$clean_root/SigurdOS-tdeck/lib/meshcore" rev-parse HEAD
```

The printed SHA must equal the superproject gitlink. Then run the companion
verification below and the affected build matrix. A submodule update is not a
hash-only maintenance change: reconcile local MeshCore patches and contact,
path, room-connection, transport-key, and persistence behaviour explicitly.

## Companion interop and golden frames

Run the official USB client matrix as documented in `scripts/official-meshcore-client-test/README.md` and the BLE scenarios in `docs/COMPANION_BLE_TEST_ENV.md`. Record firmware versions, date, pass/fail per case, and links to sanitized output. Do not record device IDs, public keys, contact names, message contents, credentials, or private keys.

The checked-in golden corpus is tied to the exact MeshCore submodule commit and SHA-256 of stock `examples/companion_radio/MyMesh.cpp`:

```bash
python3 scripts/verify_companion_golden_frames.py
```

The corpus also records a full public-upstream commit, its stock source digest,
and a canonical digest of all numeric `CMD_*`, `RESP_*`, `PUSH_*`, and
`ERR_*` values. To review a newly fetched public-upstream candidate:

```bash
git -C lib/meshcore fetch https://github.com/meshcore-dev/MeshCore.git main
python3 scripts/verify_companion_golden_frames.py \
  --candidate-ref FETCH_HEAD --json
```

The command resolves and prints the candidate's immutable full SHA. It fails if
the candidate changes any numeric protocol code and additionally verifies the
recorded source digest when the candidate is the reviewed-upstream commit.
New or changed frame layouts still require source-diff review and corresponding
golden/native assertions; numeric-code compatibility alone is not a layout
claim.

When either the submodule or reviewed-upstream baseline changes, review stock
protocol constants and frame layouts before updating the commit, source digest,
protocol digest, and corpus. A hash-only update is not review. Run both the
pinned and candidate commands plus:

```bash
pio test -e native_test -f test_companion_protocol -v
pio test -e native_sanitize -f test_companion_protocol -v
```

The protocol module includes deterministic stateful offline-sync coverage that
combines repeated app starts, bounded-page refill, write rejection, disconnect,
reconnect, retry, ordering, and durable delivered markers. This is the required
property/sequence gate for dependency refreshes; focused tests remain required
for any newly introduced state machine.

## Privacy-safe soak reports

Use a debug build at verbosity level 2 and capture at least 10 minutes (at least 120 `[stat]` samples spanning 600 seconds) for each scenario. Idle means the home screen with radio and configured transports running. Active means repeated screen navigation, message send/receive, map use, and companion reconnects representative of normal operation.

```bash
python3 scripts/analyze_soak_log.py idle.log \
  --scenario idle --json-out idle.json --markdown-out idle.md
python3 scripts/analyze_soak_log.py active.log \
  --scenario active --json-out active.json --markdown-out active.md
```

The reports contain only numeric memory summaries and a source-log digest. Attach the reports, not raw serial logs. Review raw logs locally for secrets before retaining them. Defaults fail on fewer than 120 samples, a span shorter than 600 seconds, non-monotonic timestamps, heap range over 16 KiB, or first-to-last heap drop over 8 KiB.

## OTA matrix

Test the final release artifact and record evidence for:

- successful authenticated upgrade with settings and stores preserved;
- no update available;
- missing and rejected credentials;
- offline, TLS, HTTP error, and truncated download paths;
- invalid image or write failure, followed by a clean reboot into a known-good image.
- an image with a lower security epoch, rejected before activation;
- the current and alternate maintained TLS roots, plus an expired/untrusted
  chain and the documented offline recovery path;
- an explicit firmware-signature result. Until device-side signature checking
  exists, record this as a known gap; a passing checksum is not a substitute.

Companion security evidence also covers BLE bond revocation and verifies that
factory reset prevents an old bond from silently reconnecting. USB evidence
must label the connected host as trusted because companion USB has no protocol
authentication. See [Security model](SECURITY_MODEL.md#release-evidence).

Use versioned release URLs. A negative test passes only when the error is bounded, the UI remains usable or reboots intentionally, and a bootable image is retained.

## Launcher matrix

Follow `docs/LAUNCHER.md` with the final Launcher artifact. Verify environment detection, update ownership/OTA gating, handoff back to Launcher, relaunch, and persistence of settings, contacts, channels, and messages. Record Launcher and SigurdOS versions without recording a device identifier.

## Release artifacts and warnings

The tag workflow validates source evidence before building, runs native
sanitizers, verifies the resolved PlatformIO graph, and validates the generated
manifest, image layouts, exact aliases, offsets, provenance, and SHA-256 values
before promotion. Confirm those results in the release PR. The first-party
warning gate parses only compiler warnings whose paths start with `src/`;
third-party warnings do not spend this budget. When a warning is fixed, reduce
`ci/first_party_warnings.json` in the same PR so the debt cannot return.
