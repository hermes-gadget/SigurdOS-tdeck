# Release evidence

Every release PR uses `.github/PULL_REQUEST_TEMPLATE/release.md`. The machine-readable inventory in `ci/release_evidence_requirements.json` is the source of truth; CI fails if a requirement disappears from the template.

## Publication gate

A tag is publishable only when `release-evidence/<tag>.json` exists at the tagged commit and passes `scripts/verify_release_evidence.py`. The bundle must name the exact full commit SHA and tag, contain a fresh passing record for every requirement, link to the reviewed evidence, identify firmware and peer versions for hardware checks, and record SHA-256 digests for the release artifact set. The tag workflow validates this bundle before building, carries it through the immutable workflow artifact, and publishes it with the prerelease.

Use this shape (repeat the requirement object for every ID in the requirements inventory):

```json
{
  "schema_version": 1,
  "commit": "0123456789abcdef0123456789abcdef01234567",
  "tag": "beta-0.1.45",
  "generated_at": "2026-07-13T18:00:00Z",
  "artifacts": {
    "firmware.bin": "<lowercase SHA-256>",
    "firmware-merged.bin": "<lowercase SHA-256>",
    "SigurdOS-tdeck-launcher.bin": "<lowercase SHA-256>",
    "manifest.json": "<lowercase SHA-256>"
  },
  "requirements": [
    {
      "id": "INT-BLE",
      "outcome": "pass",
      "evidence_url": "https://github.com/hermes-gadget/SigurdOS-tdeck/issues/0000",
      "tested_at": "2026-07-13",
      "firmware_version": "beta-0.1.45",
      "peer_version": "official-client-version"
    }
  ]
}
```

Validate a completed bundle locally with the full commit SHA:

```bash
python3 scripts/verify_release_evidence.py \
  --evidence release-evidence/<tag>.json \
  --commit "$(git rev-parse HEAD)" --tag "<tag>"
```

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

The tag workflow validates the evidence contracts before building. Confirm manifest offsets, artifact names, version strings, and SHA-256 sums in the release PR. The first-party warning gate parses only compiler warnings whose paths start with `src/`; third-party warnings do not spend this budget. When a warning is fixed, reduce `ci/first_party_warnings.json` in the same PR so the debt cannot return.
