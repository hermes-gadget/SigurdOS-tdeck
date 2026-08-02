# SigurdOS T-Deck Audit Fixes

## #1479 — keyed active-region activation

Implemented the declared `setActiveRegionWithKey()` API and made the lower-level keyed helper create/update private regions, install the transport key, persist the region snapshot, and activate the runtime scope transactionally. Invalid or keyless private activations fail without leaving a persisted name or key behind.

## #1480 — remote RF frequency bounds

Remote `setrf` parsing now shares the SX1262 operating limits and accepts only 150–960 MHz, including the production companion validator. Boundary and out-of-range tests cover rejection before persistence.

## #1481 — map tile validation and resume repair

The downloader now validates complete firmware-compatible PNG tiles before treating them as resumable or adding them to `index.json`. HTTP 200 responses are written to an fsynced temporary file, validated, and atomically replaced; invalid or interrupted files are retried instead of being permanently accepted.

## #1482 — map zoom validation

Downloader zoom ranges must be ordered, nonnegative, within the selected server’s advertised maximum, and within the firmware’s 0–18 range. Tests cover reversed, negative, firmware-overflow, and server-overflow ranges.

## Files changed

- Region activation: `src/mesh/mesh_wrapper.cpp`, `src/mesh/regions.cpp`, `src/mesh/regions.h`, `test/mocks/mock_mesh_wrapper.cpp`, `test/test_regions/test_regions.cpp`
- RF bounds: `src/mesh/companion_adapter.cpp`, `src/mesh/radio_config_policy.h`, `src/test/test_controller.cpp`, `src/test/test_controller.h`, `test/test_controller/test_controller.cpp`
- Map downloader and tests: `scripts/download_maps.py`, `scripts/tests/test_download_maps.py`

## Verification and tests

- `pio test -e native_test -v` — 1,565 passed, 1 skipped
- `pio run -e SigurdOS_TDeck` — successful firmware build
- `python3 -m unittest scripts.tests.test_download_maps -v` — 8 passed
- `python3 -m py_compile scripts/download_maps.py` — passed

Issue #1465 was intentionally left untouched; no EU 868 preset or radio-power code was changed.
