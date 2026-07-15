## Release

Version/tag: <!-- exact tag; must match SIGURDOS_VERSION -->

Evidence bundle: <!-- link to issue comment, CI artifact, or release-evidence directory -->

Use `N/A — reason` only when the corresponding transport or update path is not shipped by this release. Hardware claims need a result link, firmware version, peer firmware version, and date; never include device IDs, public keys, contact names, message text, WiFi credentials, or private keys.

## Companion interop

- [ ] `INT-BLE` — official app scenarios A-J pass over BLE, including bond/reconnect, sync, messaging, time, login/CLI, identity, and malformed frames; or N/A with reason. Evidence: <!-- link -->
- [ ] `INT-USB` — official `meshcore.js` companion USB matrix passes against both pinned stock peer and this release. Evidence: <!-- link -->
- [ ] `INT-GOLDEN` — CI verified the pinned stock companion golden-frame corpus.

## Soak

- [ ] `SOAK-IDLE` — ≥10-minute idle capture passes the checked-in analyzer. Report: <!-- link -->
- [ ] `SOAK-ACTIVE` — ≥10-minute active navigation/chat/companion capture passes the checked-in analyzer. Report: <!-- link -->

## OTA matrix

- [ ] `OTA-UPGRADE` — authenticated update installs, reboots, reports the new version, and preserves settings/stores. Evidence: <!-- link -->
- [ ] `OTA-NO-UPDATE` — no-new-release result does not mutate flash or state. Evidence: <!-- link -->
- [ ] `OTA-AUTH-FAIL` — missing/rejected credentials produce a bounded error and leave the device bootable. Evidence: <!-- link -->
- [ ] `OTA-DOWNLOAD-FAIL` — offline/TLS/404/truncated transfer produces a bounded error and leaves the device bootable. Evidence: <!-- link -->
- [ ] `OTA-IMAGE-FAIL` — invalid image/write failure does not replace the bootable image. Evidence: <!-- link -->

## Launcher matrix

- [ ] `LAUNCH-DETECT` — Launcher installation is detected and displayed. Evidence: <!-- link -->
- [ ] `LAUNCH-HANDOFF` — exit/relaunch hands control back to Launcher without a reset loop. Evidence: <!-- link -->
- [ ] `LAUNCH-OTA-GATE` — firmware self-OTA is unavailable while Launcher owns updates. Evidence: <!-- link -->
- [ ] `LAUNCH-STATE` — settings, contacts, channels, and messages survive relaunch. Evidence: <!-- link -->

## Release artifacts

- [ ] `REL-ARTIFACTS` — app, merged, debug, Launcher, and web-flasher files exist; manifest offsets and SHA-256 sums were checked.
- [ ] `REL-WARNINGS` — no first-party compiler-warning fingerprint exceeds its checked-in budget.
- [ ] Native suite, ASan/UBSan suites, release build, debug build, companion USB build, and BLE validation build pass.
- [ ] Hardware navigation/keyboard/radio smoke test passes for the final artifact.

## Exceptions and residual risk

<!-- Link follow-up issues for every failure or N/A. Do not waive a P0 failure in prose. -->
