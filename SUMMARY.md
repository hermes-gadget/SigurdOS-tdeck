# Build-infrastructure fixes

Branch: `fix/audit-build-infra`

## Fixes

- **#1483 — PlatformIO SBOM:** Parse dependency-tree entries with arbitrary nested `│` prefixes so transitive libraries are included in the generated inventory.
- **#1484 — CODEOWNERS:** Point the known-issues ownership rule at `/docs/KNOWN_ISSUES.md` and add a check for missing literal CODEOWNERS paths.
- **#1485 — Hardware-test staging:** Remove generated Pi staging directories in cleanup paths after flashing and evidence retrieval. `--keep-remote` is available as an explicit debugging opt-in.
- **#1486 — Companion harness:** Keep BLE/USB discovery, connection, protocol execution, and disconnect inside one `asyncio.run` lifecycle.
- **#1487 — Release evidence privacy:** Redact official-client disconnect errors before they are written to matrix evidence.

## Files changed

- `.github/CODEOWNERS`
- `scripts/check_codeowners.py`
- `scripts/generate_platformio_sbom.py`
- `scripts/hw_test/README.md`
- `scripts/hw_test/hw_flash.py`
- `scripts/hw_test/hw_test_runner.py`
- `scripts/hw_test/tests/test_hw_test.py`
- `scripts/meshcore_ble_companion_test.py`
- `scripts/official-meshcore-client-test/matrix.mjs`
- `scripts/official-meshcore-client-test/privacy.test.mjs`
- `scripts/tests/test_codeowners.py`
- `scripts/tests/test_meshcore_companion_harness.py`
- `scripts/tests/test_platformio_sbom.py`

## Tests added

- Nested dependency fixture coverage for the PlatformIO SBOM parser.
- Literal-path and missing-path coverage for the CODEOWNERS validator.
- Hardware-test cleanup and `--keep-remote` coverage for Pi flashing and worker failures.
- Same-event-loop coverage for USB and BLE companion protocol runs.
- Static privacy coverage ensuring disconnect evidence uses redaction.

## Validation

- `python3 -m unittest discover -s scripts/tests -p 'test_*.py'` — 149 passed.
- `npm run test:privacy` — 5 passed.
- `pio test -e native_test -v` — 1,561 passed, 1 skipped.
- `pio run -e SigurdOS_TDeck` — passed; firmware and web-flasher artifacts generated.
