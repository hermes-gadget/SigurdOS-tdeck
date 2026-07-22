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
tracked evidence file changes the commit it would claim to identify.

Use this shape (repeat the requirement object for every ID in the requirements inventory):

```json
{
  "schema_version": 2,
  "tag": "beta-0.1.44-RC6",
  "generated_at": "2026-07-14T18:00:00Z",
  "requirements": [
    {
      "id": "INT-BLE",
      "outcome": "pass",
      "evidence_url": "https://github.com/hermes-gadget/SigurdOS-tdeck/issues/0000",
      "tested_at": "2026-07-14",
      "firmware_version": "beta-0.1.44-RC6",
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

## Companion interop and golden frames

Run the official USB client matrix as documented in `scripts/official-meshcore-client-test/README.md` and the BLE scenarios in `docs/COMPANION_BLE_TEST_ENV.md`. Record firmware versions, date, pass/fail per case, and links to sanitized output. Do not record device IDs, public keys, contact names, message contents, credentials, or private keys.

The checked-in golden corpus is tied to the exact MeshCore submodule commit and SHA-256 of stock `examples/companion_radio/MyMesh.cpp`:

```bash
python3 scripts/verify_companion_golden_frames.py
```

When the submodule changes, review stock protocol constants and frame layouts before updating both the digest and corpus. A hash-only update is not review.

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
