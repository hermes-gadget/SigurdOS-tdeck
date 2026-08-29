# Dependency Update Report

Date: 2026-08-29

Branch: `dev`

Issue: [#1568 — Audit and safely update PlatformIO dependencies](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/1568)

## Outcome

No dependency versions were changed. The audit found two newer same-major release lines, but both touch hardware-critical display behavior or contain documented breaking changes. Under the task's stability-first rule, retaining the current pins is safer than taking either update without dedicated on-device display, touch, and shared-SPI validation.

No new dependencies or major-version updates were introduced.

## Manifest coverage

- `platformio.ini` is the repository's dependency manifest.
- No `library.json` exists anywhere in this repository (`rg --files -g 'library.json'` returned no files).
- PlatformIO Core used for the audit: 6.1.19.
- `pio pkg outdated -e SigurdOS_TDeck` reported `Everything is up-to-date!`. Because the project uses exact pins and an immutable URL, registry metadata was also checked with `pio pkg show` and upstream release/tag data was reviewed directly.

## Declared dependency inventory

| Dependency | Current declaration/resolution | Latest reviewed | Decision |
|---|---:|---:|---|
| `platformio/espressif32` | 7.0.1 | 7.0.1 | Current |
| `platformio/framework-arduinoespressif32` | `3.20017.241212+sha.dcc1105b` (Arduino-ESP32 2.0.17) | Same registry package | Current |
| `platformio/tool-esptoolpy` | 2.41100.0 (esptool 4.11.0) | 2.41100.0 | Current |
| `SPI`, `Wire` | Framework-managed, resolved as 2.0.0 | Framework-managed | Retained with the pinned Arduino framework |
| `jgromes/RadioLib` | 7.7.1 | 7.7.1 | Current |
| `rweather/Crypto` | 0.4.0 | 0.4.0 | Current |
| `lovyan03/LovyanGFX` | 1.2.21 | 1.2.28 | Skipped; see risk review below |
| `lvgl/lvgl` | 9.3.0 commit `c033a98afddd65aaafeebea625382a94020fe4a7` | 9.5.0 | Skipped; see risk review below |
| `adafruit/Adafruit BusIO` | 1.17.4 | 1.17.4 | Current |
| `adafruit/RTClib` | 2.1.4 | 2.1.4 | Current |
| `melopero/Melopero RV3028` | 1.2.0 | 1.2.0 | Current |
| `electroniccats/CayenneLPP` | 1.6.1 | 1.6.1 | Current |
| `bblanchon/ArduinoJson` | 7.4.3 | 7.4.3 | Current |
| `file://lib/WebServer` | Local security-patched overlay, resolved as 2.0.0 | Not registry-managed | Retained; verified by the build's security-patch check |
| `file://lib/meshcore` | MeshCore 1.10.0 at gitlink `c5787ee46124d540944ea238ff443f8d87ca0899` | Not registry-managed | Retained; separately pinned and CI-verified |
| `links2004/WebSockets` | 2.7.3 | 2.7.3 | Current |
| `google/googletest` (native targets) | 1.17.0 | 1.17.0 | Current |

## Skipped updates

### LovyanGFX 1.2.21 to 1.2.28

This is nominally a patch update, but it is not a narrow low-risk change for the T-Deck. The upstream comparison spans 149 commits and modifies ESP32 SPI/I2C platform code. Release 1.2.28 specifically changes SPI source-clock selection and divider calculation on ESP32-S3, where upstream notes the old result could be up to twice the requested clock. SigurdOS drives its ST7789 display through this path on a bus shared with LoRa and SD, so a successful compile would not establish hardware safety. The update was skipped pending focused physical display, touch, wake, and shared-bus testing.

Sources: [LovyanGFX 1.2.28 release](https://github.com/lovyan03/LovyanGFX/releases/tag/1.2.28), [1.2.21...1.2.28 comparison](https://github.com/lovyan03/LovyanGFX/compare/1.2.21...1.2.28), [PlatformIO registry](https://registry.platformio.org/libraries/lovyan03/LovyanGFX)

### LVGL 9.3.0 to 9.5.0

Although this remains within major version 9, LVGL 9.5.0 documents breaking changes and includes broad renderer, input, layout, theme, and widget changes. SigurdOS also intentionally pins the complete upstream 9.3.0 release commit because the PlatformIO 9.3.0 archive was previously served without `lv_sprintf` sources. A two-minor UI-framework jump is not conservative enough for this firmware without full native UI and physical navigation/display regression testing, so it was skipped.

Sources: [LVGL 9.5.0 release](https://github.com/lvgl/lvgl/releases/tag/v9.5.0), [LVGL 9.5.0 changelog](https://github.com/lvgl/lvgl/blob/v9.5.0/docs/src/CHANGELOG.rst), [pinned LVGL 9.3.0 commit](https://github.com/lvgl/lvgl/commit/c033a98afddd65aaafeebea625382a94020fe4a7)

## Build verification

Command:

```text
pio run -e SigurdOS_TDeck
```

Final result: **SUCCESS**

```text
RAM:   54.2% (177,736 / 327,680 bytes)
Flash: 42.8% (2,807,785 / 6,553,600 bytes)
Merged image: 2,873,744 bytes
Duration: 265.87 seconds
```

The first attempt failed because this worktree's `lib/meshcore` submodule had not been initialized, so MeshCore headers such as `helpers/RegionMap.h` were absent. Running `git submodule update --init lib/meshcore` checked out the repository's exact pinned gitlink, after which the unchanged dependency set built successfully. No candidate dependency change caused the failure, so there was nothing to revert.

The build emits existing warnings in pinned/vendor sources and two unused helper warnings in `src/app/map_renderer.cpp`. This report does not change dependency declarations, compiler flags, or source code, so it introduces no new build warnings.

## Recommendation

Keep the current dependency pins. Reconsider LovyanGFX only in a dedicated issue/branch with physical ST7789 output, touch input, display sleep/wake, SD access, and LoRa/shared-SPI evidence. Treat an LVGL upgrade as a separate UI migration with the full native suite and physical navigation/screenshots rather than a routine dependency bump.
