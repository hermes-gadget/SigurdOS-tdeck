# Launcher Compatibility Roadmap

**Status:** All code-level items (C1–C7, O3) are implemented and merged. LauncherHub listing (O1) and return-to-Launcher (O2) remain external/hardware-gated. This roadmap is in maintenance-only mode — no further code changes are expected.
**Tracking issues:** [#567](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/567), [#610](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/610), [#612](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/612), [#614](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/614), [#615](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/615), [#616](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/616)
**External project:** [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) (analyzed at v2.7.2, June 2026)
**Related:** `docs/KNOWN_ISSUES.md` → "SigurdOS Launcher compatibility", `docs/MISSING_FEATURES.md` → "Launcher compatibility — M"

---

## Executive Summary

Launcher is resident "app store" firmware for ESP32 devices with an explicit LilyGo T-Deck port. It keeps itself in a small protected app partition, **rewrites the device's partition table at runtime** to carve out an OTA app partition for each firmware it installs, and chain-boots installed firmware via `otadata` + software reset. Users return to Launcher at power-on (Launcher's custom bootloader boots its resident partition first).

SigurdOS today is **standalone-only firmware**: it assumes it owns the whole 16 MB flash with the stock `default_16MB.csv` layout, persists mesh identity/contacts in a SPIFFS partition, and ships self-OTA features (WiFi AP upload + GitHub release download) that assume the dual `ota_0`/`ota_1` slot layout.

The key findings of this analysis:

1. **Launcher can already structurally install SigurdOS.** Our existing `firmware-merged.bin` contains a partition table at file offset 0x8000, which Launcher parses: it extracts the app image, creates a fresh SPIFFS partition for it, keeps its own bootloader, and boots SigurdOS from a dynamically allocated OTA partition. ESP32-S3 app images are MMU-mapped per partition, so running from a non-0x10000 offset is not itself a problem.
2. **App-only `firmware.bin` installs lose persistence.** Launcher's app-only install path creates *no* SPIFFS partition, so `SPIFFS.begin(true)` fails and the mesh identity regenerates on every boot — contacts and channels never persist. The merged/Launcher artifact is the viable install source, and `firmware/README.md` now documents that path.
3. **Self-OTA is broken or dangerous under Launcher unless gated.** With Launcher's single-app layout, `Update.begin()` fails (feature dead but graceful). With multiple installed apps, Arduino `Update` would flash SigurdOS *over another installed firmware's partition* and desync Launcher's app registry. Current firmware gates WiFi and GitHub self-OTA when Launcher is detected; PR #609 tightens the detection signal.
4. **The reported breakage ("keyboard doesn't work, many things break") is a warm-handoff problem.** Launcher initializes I2C, GT911 touch, trackball interrupts, the display, and the keyboard backlight before chaining to the installed app via `ESP.restart()` — a software reset that does **not** reset external peripherals (ST7789 has no reset line on the T-Deck; the keyboard is a separate always-running ESP32-C3; GT911 latches its I2C address at its own reset). The exact failure mechanism is unverified and requires physical-hardware debugging (remote-test mode cannot exercise the physical layer — see `CLAUDE.md` / `AGENTS.md`).
5. **No catalog presence.** Launcher's OTA menu is fed by LauncherHub (`api.launcherhub.net`) filtered by board tag `t-deck`. SigurdOS is not listed; users would have to sideload via SD card or a direct GitHub URL (which works — GitHub release assets support HTTP range requests, which Launcher's online installer requires).

Compatibility is achievable **without changing any standalone behavior**. The packaging, documentation, self-OTA gate, persistence diagnostic, and keyboard hardening are in place; the remaining work is tightening the runtime detection confirmation signal, physical hardware validation of the warm handoff, and external LauncherHub catalog coordination. Standalone installs remain byte-identical and behaviorally unchanged.

---

## Current Compatibility Status

| Aspect | Status | Evidence |
|---|---|---|
| Launcher has a T-Deck port | ✅ Yes | `boards/lilygo-t-deck/` in Launcher repo; release asset `Launcher-lilygo-t-deck.bin` (v2.7.2) |
| SigurdOS chip/flash matches Launcher's T-Deck build | ✅ Yes | Both esp32s3 / 16 MB / OPI PSRAM; SigurdOS overrides its board JSON default to DIO flash for boot compatibility (`boards/t-deck.json` here; `boards/_jsonfiles/lilygo-t-deck.json` there) |
| `firmware-merged.bin` parseable by Launcher's installer | ✅ Yes (by source inspection, untested on hardware) | Launcher `src/sd_functions.cpp::updateFromSD` detects the partition table at 0x8000 and extracts app + SPIFFS spec |
| `firmware.bin` (app-only) install preserves identity | ❌ No — no SPIFFS partition is created | Launcher `updateFromSD` app-only path passes `spiffs=false`; `src/main.cpp:50-52` here |
| Self-OTA (WiFi AP / GitHub) under Launcher | ✅ Gated off with explanation | `src/hal/wifi_ota.cpp`, `src/hal/github_ota.cpp`, and Settings System refuse OTA when `sigurdos_is_under_launcher()` fires |
| Boot-to-UI after Launcher handoff | ⚠️ Reported broken (keyboard + more) | `docs/KNOWN_ISSUES.md` — user report, root cause unverified |
| Listed in Launcher catalog (LauncherHub) | ❌ No | `api.launcherhub.net/firmwares?category=t-deck` |
| Documented install path for Launcher users | ✅ Yes | `firmware/README.md` documents `SigurdOS-tdeck-launcher.bin`, app-only caveats, OTA gating, and mode-switch reset behavior |

## Implementation Progress

All code-level items (C1–C7, O3, O4) are implemented and merged. Only external/blocked items remain.

| Roadmap Item | Status | Notes |
| ------------ | ------ | ----- |
| C1 — Launcher-install release artifact | ✅ Complete | `.github/workflows/build-release.yml` uploads `SigurdOS-tdeck-launcher.bin`; `scripts/merge_bin.py` also emits a local Launcher-named copy/manifest entry. |
| C2 — Launcher install documentation | ✅ Complete | `firmware/README.md` documents SD/WebUI/direct-URL Launcher installs and warns against app-only `firmware.bin` for Launcher persistence. |
| C3 — Runtime Launcher detection | ✅ Complete | PR #609 (merged) added the `otadata @ 0xD000` confirmation signal and native false-positive tests. |
| C4 — Gate self-OTA under Launcher | ✅ Complete | WiFi OTA, GitHub OTA, and Settings System refuse self-OTA under Launcher with an "update through Launcher" explanation. |
| C5 — Boot-time persistence diagnostic | ✅ Complete | `src/main.cpp` prints a Launcher-specific app-only install warning when SPIFFS mount fails under Launcher. |
| C6 — Warm-handoff keyboard hardening | ✅ Complete | Keyboard init now uses bounded probe retry and explicit C3 mode reset; further warm-handoff work must be driven by physical hardware evidence. |
| C7 — Migration note | ✅ Complete | `firmware/README.md`, `docs/KNOWN_ISSUES.md`, and `docs/MISSING_FEATURES.md` document Launcher support caveats and mode-switch reset behavior. |
| O1 — LauncherHub catalog listing | ⏳ Blocked (external) | Issue #615 tracks the external maintainer/catalog process; direct URL and SD/WebUI install remain the documented path until LauncherHub listing is accepted. |
| O2 — Reboot to Launcher Settings entry | ⏳ Blocked (hardware) | Issue #616 tracks the required bench validation of return-to-Launcher semantics and whether a stock-framework app can safely write Launcher `otadata`; do not implement speculatively. |
| O3 — Shrink-audit app image | ✅ Complete | PR #613 (merged) added the measured size audit (`docs/LAUNCHER_SIZE_AUDIT.md`) and confirms no shrink work is required for Launcher compatibility right now. |
| O4 — Launcher PlatformIO env alias | ✅ Complete | Intentionally skipped as a separate env: C1's CI copy and the local `scripts/merge_bin.py` copy provide the Launcher artifact name with zero firmware delta. |

---

## Launcher Requirements (from source inspection)

How Launcher actually works, with file references into [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) @ v2.7.2:

### Resident layout and boot chain

- Launcher itself lives in an **app partition with subtype `test`** at 0x10000 (1.5 MB on 16 MB boards): `support_files/custom_16Mb.csv`:

  ```
  # Name,     Type, SubType,   Offset,   Size
  nvs,        data, nvs,       0x9000,   0x4000
  otadata,    data, ota,       0xD000,   0x2000
  phy_init,   data, phy,       0xf000,   0x1000
  app0,       app,  test,      0x10000,  0x180000
  coredump,   data, coredump,  0x190000, 0x10000
  ```

- Launcher releases are merged binaries (`Launcher-lilygo-t-deck.bin` = bootloader @0x0 + table @0x8000 + app @0x10000, built by `support_files/merge.py`).
- Launcher is built against a **custom framework package** (`platformio.ini`: pioarduino platform + `framework-arduinoespressif32-libs` from `bmorcelli/esp32-arduino-lib-builder`). The `launcher` branch of that lib-builder enables `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y` (so the Launcher app may rewrite the partition table and otadata at runtime) and disables the task WDT. The maintainer has stated a **custom bootloader is required for the return-to-Launcher flow** (issue [bmorcelli/Launcher#3](https://github.com/bmorcelli/Launcher/issues/3): *"you must use my custom bootloader_CP.bin … If you don't use it, you won't be able to get back to the launcher"*). Power-on boots Launcher; its boot screen waits ~2 s and then chains to the installed app (`src/main.cpp` boot-screen loop → `launcherBootAppByLabel` → `launcherPartitionSetOtaBoot` writes otadata → `ESP.restart()`, `src/app_registry.cpp:310-344`, `src/partition_table_model.cpp:858-918`).
- **Consequence for us:** when installed via Launcher, SigurdOS runs under *Launcher's bootloader and Launcher's runtime-generated partition table*. The bootloader, `boot_app0`, and partition table inside our merged binary are never flashed.

### Install paths and binary format detection

`src/sd_functions.cpp::updateFromSD` (SD card), `src/onlineLauncher.cpp::installExtFirmware` (online, via HTTP **range requests** — the file's 0x8000 region is fetched remotely), and the WebUI all share the same model:

1. **Partition-table signature probe at file offset 0x8000** (bytes `0xAA 0x50 0x01`).
2. **No signature → app-only image.** The ESP image header (magic `0xE9`) at offset 0 is parsed and the image measured segment-by-segment (`measureSdEspImage`). The app is installed into a dynamically created OTA app partition (size 64 KB-aligned, `LAUNCHER_APP_PARTITION_ALIGNMENT` in `src/partition_table_model.h`). **No SPIFFS partition is requested** (`spiffs=false`).
3. **Signature found → merged image.** Launcher walks the embedded table:
   - app entries with subtype `factory` (0x00), `ota_0` (0x10), or `test` (0x20) → app image source (our `app0`/`ota_0` @0x10000 matches; our `app1`/`ota_1` = 0x11 is ignored);
   - data subtype 0x82 (SPIFFS) → Launcher **creates a SPIFFS partition** for the app. Declared sizes ≤ 5 MB (`LAUNCHER_DEFAULT_SPIFFS_THRESHOLD`, `include/pre_compiler.h`) get the per-flash default — **0x100000 (1 MB) on 16 MB boards** (`LAUNCHER_DEFAULT_SPIFFS_SIZE`). If the file is shorter than the SPIFFS offset (ours is), the partition is created empty and no payload is copied;
   - data subtype 0x81 (FAT) → same, for `sys`/`vfs` partitions (we have none).
4. The new layout is computed by `src/partition_install_layout.cpp::launcherSelectInstallLayout` (may prompt the user to repartition/evict if space is tight), validated, written to flash at 0x8000, otadata is pointed at the new app slot, and the device reboots into the app.
5. After writing, the image is verified with `esp_image_verify` (`src/idf/idf_update.cpp:94`) — this **enforces the chip ID** in the image header, so only esp32s3 images install on a T-Deck.
6. Launcher always enforces its **required boot partitions**: `nvs @0x9000 size 0x4000`, `otadata @0xD000 size 0x2000`, `phy_init @0xF000 size 0x1000` (`src/idf/idf_update.cpp:25-29`). Note both nvs size and otadata offset **differ from our standalone table** (see below).

### Catalog / distribution

- The OTA menu queries `https://api.launcherhub.net/firmwares?category=<OTA_TAG>`; the T-Deck build sets `-DOTA_TAG='"t-deck"'` (`boards/lilygo-t-deck/platformio.ini`). Catalog browsing also exists at <https://bmorcelli.github.io/Launcher/catalog.html>.
- Direct URL installs (`installExtFirmware`) work with any HTTPS file host that supports range requests — GitHub release asset URLs qualify.
- SD installs require a FAT32 SD card and the `.bin` on it; the installed app's display name is derived from the file name (`launcherAppNameFromFile`, `src/app_registry.cpp`), truncated to 20 chars.

### T-Deck hardware state Launcher creates before handoff

`boards/lilygo-t-deck/interface.cpp::_setup_gpio`:

- `Wire.begin(18, 8)` (same pins we use), 500 ms delay "time to ESP32C3 start"
- GT911 touch via `TouchDrvGT911` (`GT911_SLAVE_ADDRESS_L` = 0x5D), INT on GPIO 16
- Trackball ISRs on GPIOs 3/15/1/2 (FALLING), Sel button GPIO 0
- `PIN_POWER_ON` (GPIO 10) driven HIGH — same peripheral-power pin we drive in `src/hal/tdeck_board.h:58-59`
- LoRa CS (GPIO 9) driven HIGH; **the SX1262 is never initialized**
- ST7789 TFT initialized (TFT_RST not wired: `-DTFT_RST=GFX_NOT_DEFINED`), backlight on LEDC channel 0
- Keyboard: only the backlight command `0x01` is ever sent to the C3 at 0x55 (`_setBrightness`) — Launcher never touches the mode commands `0x03`/`0x04`

Handoff is `ESP.restart()` — a **software reset**. The ESP32-S3's own GPIO matrix/peripherals reset, but external devices (C3 keyboard MCU, GT911, ST7789, SX1262, SD card) keep whatever state they had, modulo what happens to the GPIO-10-gated power rail while the S3 is in reset.

---

## Current Firmware Assumptions (this repo)

| Assumption | Where |
|---|---|
| Build system: PlatformIO + Arduino, `platformio/espressif32@6.11.0` (Arduino core 2.x / ESP-IDF 4.4) | `platformio.ini:68-69` |
| Single hardware target: LilyGo T-Deck, esp32s3, 16 MB DIO flash, 8 MB OPI PSRAM (board JSON memory type remains `qio_opi`, while `platformio.ini` overrides boot flash mode to DIO) | `boards/t-deck.json`, `platformio.ini` |
| Partition table: stock `default_16MB.csv` — `nvs 0x9000/0x5000`, `otadata 0xE000/0x2000`, `app0 (ota_0) 0x10000/0x640000`, `app1 (ota_1) 0x650000/0x640000`, `spiffs 0xC90000/0x360000`, `coredump 0xFF0000/0x10000` | `platformio.ini:73` (`board_build.partitions = default_16MB.csv`, resolved from the Arduino framework package) |
| Artifacts: `firmware.bin` (app, flash @0x10000) and `firmware-merged.bin` (bootloader @0x0 + table @0x8000 + `boot_app0` @0xE000 + app @0x10000, `flash_mode keep`), plus per-component webflasher files + `webflasher/manifest.json` | `scripts/merge_bin.py`, `firmware/README.md` |
| Release: tag push builds `SigurdOS_TDeck` and uploads `firmware.bin` + `firmware-merged.bin` to a GitHub release | `.github/workflows/build-release.yml` |
| Identity/contacts/channels persist in **SPIFFS**; mount failure = ephemeral identity (warning printed, mesh still starts) | `src/main.cpp:50-52`, `sigurdos::mesh::init(spiffs_ok)` in `src/mesh/mesh_wrapper.*` |
| Settings persist in **NVS** via `Preferences`, namespace `"sigurdos"` | `src/hal/prefs.cpp:9` |
| Self-OTA #1: WiFi AP + web upload page using Arduino `Update`; header comment explicitly states "Depends on OTA partition table (`board_build.partitions = default_16MB.csv`)" | `src/hal/wifi_ota.h`, `src/hal/wifi_ota.cpp` |
| Self-OTA #2: GitHub release selection followed by a versioned `…/releases/download/<tag>/firmware.bin` download flashed via `Update` | `src/hal/github_ota.cpp:318-399`, `src/hal/github_ota_plan.h` |
| Full hardware ownership at boot: peripheral power GPIO 10, shared SPI singleton (display CS 12 / LoRa CS 9 / SD CS 39), I2C bus (keyboard 0x55, GT911 0x5D), trackball GPIOs, GPS UART 43/44, USB CDC serial | `src/hal/tdeck_board.h`, `src/hal/spi_shared.cpp`, `src/hal/keyboard.cpp`, `src/hal/touch.cpp`, `src/hal/gps.cpp`; pinout in `src/hal/tdeck_pins.h` and `CLAUDE.md` |
| Keyboard driver switches the C3 keyboard MCU into **raw matrix mode** (I2C cmd `0x03`) after probing 0x55 | `src/hal/keyboard.cpp:31-76` (protocol), `src/hal/keyboard.cpp:353-399` (init) |
| Touch driver probes GT911 at 0x5D **and falls back to 0x14** | `src/hal/touch.cpp:31-41` |
| Map tiles live on **SD card** (PNG via lodepng, PSRAM cache) — not SPIFFS | `src/app/map_renderer.*`, `src/hal/sdcard.*` |
| Radio TX is gated behind `configured == true` (first-boot onboarding) | `CLAUDE.md` gotcha "Radio first boot"; `src/hal/prefs.*` |

---

## Root Causes of Incompatibility

Ordered by severity. Each lists the firmware functionality at risk.

### RC1 — App-only installs get no SPIFFS partition → mesh identity is ephemeral

Launcher's app-only path (no table at 0x8000 in the file) installs the app with `spiffs=false` (Launcher `src/sd_functions.cpp::updateFromSD`, first branch). SigurdOS then boots with `SPIFFS.begin(true)` failing (`src/main.cpp:50`), which means:

- a **new mesh identity (keypair) every boot** — other nodes see a "new" node each time, DMs to the old key break;
- contacts, channels, and message history never persist.

**At risk:** the entire persistence layer; effectively unusable as a daily-driver mesh node.
**Note:** this is not a crash — the firmware "works" but silently loses state. That makes it worse, because it looks installable.

### RC2 — Self-OTA assumes the standalone dual-slot table

Both OTA paths use Arduino `Update`, which targets `esp_ota_get_next_update_partition()`:

- Under Launcher with **one** installed app (just SigurdOS), there is no second OTA slot → `Update.begin()` fails. Feature is dead but fails cleanly (error surfaced in UI/serial: `src/hal/github_ota.cpp:318-320`, `src/hal/wifi_ota.cpp:109-110`).
- Under Launcher with **multiple** installed apps (Launcher supports several side-by-side, `src/app_registry.cpp`), the "next" OTA partition is **another firmware's partition**. A SigurdOS self-update would overwrite e.g. Bruce or Meshtastic in place, repoint otadata, and desync Launcher's NVS app registry (app name no longer matches partition contents).

**At risk:** Settings → firmware update features; *other people's installed firmware*; Launcher's app registry integrity.

### RC3 — Warm-handoff peripheral state (the documented breakage)

`docs/KNOWN_ISSUES.md` records the user-visible result: keyboard not working properly and "many other things break" when SigurdOS is launched from Launcher. The handoff differs from our only tested boot path (power-on reset) in specific, enumerable ways:

| Peripheral | Cold boot (standalone) | After Launcher handoff (SW reset) |
|---|---|---|
| ESP32-C3 keyboard MCU (0x55) | Cold-boots in default ASCII key mode while S3 boots; our init then switches it to raw mode | Already running; mode/state depends on what ran before; GPIO-10 power rail may or may not have dropped while the S3 was in reset (rail behavior during SW reset is unverified) |
| GT911 touch (0x5D) | Latches I2C addr 0x5D at its power-on | Not reset by S3 SW reset; retains config Launcher's `TouchDrvGT911` wrote; address re-latch only happens if the power rail actually drops. Our driver probes 0x5D then 0x14 (`src/hal/touch.cpp:31-41`), which mitigates the classic GT911 address-flip |
| ST7789 display | Full power-on init | No reset line on T-Deck (Launcher sets `TFT_RST=GFX_NOT_DEFINED`; we re-init via LovyanGFX including soft reset) — retains Launcher's panel config until our init completes |
| SX1262 LoRa | Power-on default, we hard-reset via RST 17 during radio init | Launcher never touches it beyond CS-high; our init hard-resets it — should be equivalent |
| SD card | Power-on default | Retains SPI state; re-init via CMD0 normally recovers it |
| NVS/SPIFFS | Our partitions | Launcher's partitions (see RC4/RC5) |

The C3 keyboard and the GPIO-10 power-rail timing are the prime suspects (our keyboard init probes 0x55 and aborts permanently on NACK — `sigurdos_keyboard_init`, `src/hal/keyboard.cpp:353-366`; the `CLAUDE.md` gotcha "Wire.endTransmission — NACK on keyboard init means keyboard MCU is dead" bakes in the cold-boot assumption). **The exact mechanism is unverified.** Per `CLAUDE.md`, remote-test mode cannot validate physical-layer input issues; this requires bench debugging on real hardware (Phase 5).

**At risk:** keyboard input (primary input method), possibly touch; anything else the user report covered ("many other things").

### RC4 — NVS geometry differs between the two worlds

Standalone: `nvs @0x9000 size 0x5000`, `otadata @0xE000`. Under Launcher: `nvs @0x9000 size 0x4000`, `otadata @0xD000` (enforced, Launcher `src/idf/idf_update.cpp:25-29`). Same offset, smaller size:

- A device that previously ran standalone has NVS pages laid out across 5 sectors; under the 4-sector declaration, `nvs_flash_init` can report no-free-pages/corruption, and the Arduino core's recovery **erases NVS and retries**. Result: `NodePrefs` (`namespace "sigurdos"`) reset to defaults → onboarding wizard re-runs, radio params return to build defaults, TX stays gated until reconfigured (`configured == false` — a safe failure).
- Namespaces do not collide (Launcher uses `"launcher"` + its app registry namespaces; we use `"sigurdos"`), but both live in the same physical partition under Launcher.

**At risk:** settings survival when *switching install methods* (not during normal Launcher use). One-time, recoverable, must be documented.

### RC5 — SPIFFS relocation and shrink between the two worlds

Standalone SPIFFS: `0xC90000`, 3.4 MB. Under Launcher (merged-bin install): created at a partitioner-chosen offset, **1 MB** (`LAUNCHER_DEFAULT_SPIFFS_SIZE` for 16 MB boards, Launcher `include/pre_compiler.h`). Because SPIFFS is found by subtype/label, not offset, SigurdOS works either way — but:

- data does **not** migrate between modes (the old region is simply no longer referenced, and Launcher may repartition over it) → identity/contacts are lost on each switch between standalone and Launcher installs;
- 1 MB is ample for our identity + contacts (`MAX_CONTACTS=350`) + channel data; map tiles are on SD and unaffected. No feature loss expected — verify in Phase 5.

**At risk:** continuity of identity when switching install methods. Within a single mode, no risk identified.

### RC6 — Packaging, naming, and documentation mismatch

- `firmware.bin` — the artifact Launcher users would naturally grab — is exactly the one that hits RC1.
- `firmware-merged.bin` is the right install source but its name and all docs (`firmware/README.md`, release notes template in `.github/workflows/build-release.yml`) describe flashing at 0x0 with esptool, which **uninstalls Launcher** (overwrites its bootloader and table). Nothing tells a Launcher user which file to feed to Launcher.
- The installed-app display name is derived from the file name (truncated to 20 chars): `firmware-merged.bin` would show up as "firmware-merged".
- SigurdOS is not on LauncherHub (category `t-deck`), so it does not appear in the device's OTA menu.

**At risk:** first-run experience; users bricking their Launcher setup (not the device — reflashing Launcher recovers it) by following our standalone instructions.

### Non-issues (verified, recorded to prevent re-investigation)

- **Flash offset of the app**: ESP32-S3 app images are mapped via MMU per-partition; no code in this repo assumes the 0x10000 offset at runtime (the offsets in `scripts/merge_bin.py` / `firmware/README.md` are flashing instructions, not runtime assumptions).
- **Chip/flash/PSRAM mismatch**: none — Launcher's T-Deck build matches our target, and PSRAM (OPI) is initialized by the app itself, not the bootloader.
- **`app1`/`ota_1`, `coredump`, `boot_app0` inside our merged bin**: ignored by Launcher's parser (only factory/ota_0/test app entries and 0x81/0x82 data entries are read; the merged file ends before those offsets anyway — file size 0x10000 + app size, see `scripts/merge_bin.py`).
- **IDF version skew** (our app: Arduino core 2.x/IDF 4.4; Launcher's bootloader: IDF 5.5-based): the app-image format is stable across these versions and the bootloader from *our* merged bin is never used under Launcher. Flagged for confirmation in Phase 2 testing, expected to be fine — Launcher routinely runs IDF 4.x-era firmware (e.g., Marauder).
- **GT911 dual-address**: our driver already probes 0x5D and 0x14 (`src/hal/touch.cpp:31-41`).
- **LoRa radio**: Launcher never initializes the SX1262 and parks CS high; our driver hard-resets it via RST (GPIO 17). MeshCore radio bring-up is expected to be unaffected.

---

## Required Changes

Everything here is additive; standalone behavior is untouched. Items marked **[docs]** need no code.

| # | Change | Addresses | How standalone behavior is preserved |
|---|---|---|---|
| C1 | **Publish a Launcher-install artifact** in releases: a copy of the merged image named `SigurdOS-tdeck-launcher.bin` (same bytes as `firmware-merged.bin`; the name gives Launcher users an unambiguous file and a sane installed-app name "SigurdOS-tdeck-launch…"). Add it to `.github/workflows/build-release.yml` release files. | RC1, RC6 | Pure addition — existing artifacts unchanged |
| C2 | **Document the Launcher install path** in `firmware/README.md`: install via Launcher SD/WebUI/URL using the merged image only; explicitly warn that `firmware.bin` (app-only) loses persistence under Launcher, and that flashing `firmware-merged.bin` at 0x0 with esptool *replaces* Launcher. | RC1, RC6 | **[docs]** |
| C3 | **Runtime Launcher detection** (new tiny HAL helper, e.g. `src/hal/launcher_env.cpp/h`): report "running under Launcher" when a `test`-subtype app partition exists (Launcher's resident slot — never present in `default_16MB.csv`), with the otadata offset (0xD000 vs 0xE000) as a confirming signal. Standalone tables can never trip this. | RC2 | Detection only; returns `false` on every standalone device |
| C4 | **Gate self-OTA on C3's detection**: when under Launcher, `wifi_ota` / `github_ota` refuse to start and the Settings UI explains "Update SigurdOS through Launcher instead." | RC2 | Code path only reachable when detection fires; standalone OTA untouched. This is the one behavior change *under Launcher*, and it prevents flash corruption of co-installed firmware |
| C5 | **Boot-time persistence diagnostics**: when SPIFFS mount fails *and* Launcher is detected, log (and optionally surface in onboarding) "installed app-only — reinstall from the merged/Launcher image for persistence" instead of the generic warning at `src/main.cpp:51-52`. | RC1 | Message-only change, additional branch on detection |
| C6 | **Warm-handoff hardening after bench root-cause** (Phase 5): expected scope is keyboard-init retry/timing (e.g., re-probe 0x55 with a bounded retry window instead of single-NACK-abort in `sigurdos_keyboard_init`) and explicit C3 mode reset (`0x04` key mode, then `0x03` raw) — both no-ops on a healthy cold boot. Final scope depends on what the bench work finds; do **not** implement speculatively. | RC3 | Retries/mode-set are idempotent on cold boot; validated by the existing `test_keyboard` native suite plus physical test |
| C7 | **Migration note** in docs (`firmware/README.md` + `docs/KNOWN_ISSUES.md` update): switching standalone ↔ Launcher resets NVS prefs and SPIFFS identity; back up/re-onboard expectations spelled out. | RC4, RC5 | **[docs]** |

## Optional Improvements

| # | Improvement | Value |
|---|---|---|
| O1 | LauncherHub catalog listing under category `t-deck` (see Release Integration) | One-tap install from the device's OTA menu |
| O2 | A "Reboot to Launcher" Settings entry, shown only when C3 detection fires (sets boot back to Launcher's resident partition if the partition-table APIs allow it from a stock app, then restarts; needs investigation — Launcher's own writes rely on `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED`, which we do not enable). If infeasible, document "power-cycle to return to Launcher". | UX parity with other Launcher-aware firmware |
| O3 | Shrink-audit of the app image (~2.6 MB today, `webflasher/manifest.json`; see `docs/LAUNCHER_SIZE_AUDIT.md`) | Complete: no shrink work required for Launcher compatibility right now |
| O4 | A `SigurdOS_TDeck_launcher` PlatformIO env alias (identical build, different `PROGNAME`) if we want the artifact name baked in rather than copied in CI | Cleaner CI; zero firmware delta |

**Explicitly rejected:** building SigurdOS against Launcher's partition scheme, removing self-OTA from standalone builds, or making Launcher the primary distribution. Standalone remains the first-class product (`docs/MISSING_FEATURES.md` calls Launcher support a niche target).

---

## Packaging Strategy

- **One binary, two names.** The Launcher artifact is byte-identical to `firmware-merged.bin`. Launcher consumes the embedded partition table as a *manifest* (app source offset + "this firmware wants SPIFFS") — exactly what its 2.6.7 changelog describes for non-merged binaries with attached tables (Launcher `README.md`).
- **Why not a special app+table-only artifact?** Launcher would accept one (table @0x8000, no bootloader), but it is a third artifact to maintain and is *dangerous outside Launcher* (not bootable if esptool-flashed). The merged image is valid in both worlds: esptool @0x0 standalone, or fed to Launcher.
- **Naming:** `SigurdOS-tdeck-launcher.bin` (≤ 20 chars of stem shown in Launcher's menu). Keep `firmware.bin` / `firmware-merged.bin` untouched for standalone users and the versioned GitHub-OTA asset selected through the release API.
- **Hosting:** GitHub release assets (already range-request capable for Launcher's online installer). No new infrastructure.

## Partition / Flash Strategy

- **Standalone:** unchanged — `default_16MB.csv`, artifacts and offsets exactly as today (`platformio.ini:73`, `scripts/merge_bin.py`).
- **Under Launcher:** accept Launcher's dynamically generated table as-is. Do **not** fight the partitioner: SigurdOS already discovers nvs/spiffs by API, and the app slot is wherever Launcher put it. Required properties: an app partition ≥ app size (2.6 MB — trivially available on 16 MB flash next to Launcher's 1.5 MB resident slot), and one SPIFFS partition (guaranteed by installing from the merged image; 1 MB default is sufficient for identity + 350 contacts + channels).
- **No partition-table changes are proposed in this repo.** The untracked local `partitions_8MB.csv` experiment is out of scope (and would, if ever shipped, need its own pass through this analysis — Launcher's 8 MB scheme has the same structure with a 0x150000 resident slot).

## Board-Target Strategy

- Single target (T-Deck) maps 1:1 onto Launcher's `lilygo-t-deck` env (`OTA_TAG="t-deck"`). T-Deck Plus is a separate Launcher env (`lilygo-t-deck-plus`) sharing the tag; if SigurdOS ever supports the Plus's touch variant, the same artifact serves both — defer until then.
- No board JSON or pin changes needed: every pin Launcher's T-Deck port touches (I2C 18/8, power 10, LoRa CS 9, trackball 3/15/1/2, TFT 12/11/40/41/38, backlight 42, SD CS 39) matches `src/hal/tdeck_pins.h`.

## Firmware Functionality Preservation Plan

Non-negotiables and how each is protected:

| Subsystem | Preservation mechanism | Regression risk |
|---|---|---|
| Mesh networking (MeshCore, LoRa) | No changes proposed; Launcher never touches the SX1262 | Low |
| Identity/contacts persistence (SPIFFS) | Standalone table unchanged; Launcher path guaranteed a SPIFFS partition by installing from the merged image (C1/C2); C5 only adds messaging | Low standalone / **High under Launcher if users install `firmware.bin`** — mitigated by C2 docs + C5 diagnostics |
| Settings (NVS) | Namespace `"sigurdos"` unchanged; geometry difference documented (C7) | Low; one-time reset when switching modes |
| Display/touch/trackball/keyboard | C6 changes are idempotent on cold boot and land only after bench root-cause with physical-hardware testing (per `CLAUDE.md`, remote-test cannot cover this) | Medium — keyboard init is the touchiest path; covered by `test_keyboard` native tests + physical validation before merge |
| Self-OTA (WiFi AP, GitHub) | Fully functional standalone; disabled-with-explanation only when Launcher detected (C4). Detection false-positive is structurally impossible on `default_16MB.csv` (no `test` app partition exists) | Low; native test for the detection helper + manual standalone OTA check in Phase 5 |
| GPS, SD/maps, BLE companion, buzzer, battery | Untouched | Low |
| Onboarding / TX gating | Untouched; NVS reset on mode switch re-runs onboarding, which is the designed safe state | Low |
| Web flasher / esptool / PlatformIO install paths | Artifacts and manifest unchanged (C1 adds a file) | Low |

Every code change (C3–C6) ships with: native tests (`pio test -e native_test`), a `pio run -e SigurdOS_TDeck` build, and a PR declaring physical-hardware or remote-test validation per `CONTRIBUTING.md` / the `CLAUDE.md` audit checklist.

---

## Testing / Validation Matrix

All rows assume LilyGo T-Deck (esp32s3, 16 MB flash, 8 MB PSRAM) — the only supported board. "Launcher path" = Launcher v2.7.2+ flashed first, SigurdOS installed from SD/URL/WebUI.

| # | Scenario | Install path | What to verify | Pass criteria | Regression risk |
|---|---|---|---|---|---|
| T1 | Baseline standalone | esptool `firmware-merged.bin` @0x0 | Full boot, display/touch/keyboard/trackball, SPIFFS mount, identity persists across reboot, LoRa TX/RX with a second MeshCore node, GPS, SD/map, BLE companion | All green; identical to current release behavior | — (control) |
| T2 | Standalone app-update | esptool `firmware.bin` @0x10000 over T1 | Settings + identity survive | Prefs and contacts intact | Low |
| T3 | Standalone self-OTA | T1 → Settings WiFi-OTA upload; and GitHub OTA | Update completes, boots from `ota_1`, identity survives; detection helper (C3) reports standalone **including when running from `app1`** | OTA works exactly as today | **Key C3 false-positive check** |
| T4 | Launcher install, merged artifact | Launcher SD install of `SigurdOS-tdeck-launcher.bin` | Launcher parses table, creates SPIFFS, boots SigurdOS; keyboard/touch/trackball/display work after warm handoff; identity persists across power cycles; LoRa mesh works | Daily-driver usable; RC3 resolved or precisely characterized | High (RC3) — this is the make-or-break row |
| T5 | Launcher install, app-only (negative test) | Launcher SD install of `firmware.bin` | C5 diagnostic fires; firmware states persistence is unavailable | Clear user-facing message; no crash | Low |
| T6 | Launcher online install | Launcher OTA → direct GitHub URL (and LauncherHub once listed) | Range-request install completes; same checks as T4 | Same as T4 | Medium |
| T7 | Self-OTA under Launcher | T4 → attempt WiFi/GitHub OTA | C4 gate refuses with explanation; **no flash writes occur** | Other installed apps (install Bruce alongside) byte-identical before/after | High if C4 absent — this is RC2 |
| T8 | Multi-app coexistence | Launcher with SigurdOS + one other firmware | Both launch from Launcher repeatedly; SigurdOS SPIFFS/NVS state survives switching apps | No cross-corruption | Medium |
| T9 | Return to Launcher | T4 → power cycle | Launcher boot screen appears; relaunching SigurdOS preserves state; repeated relaunch loop ×10 (warm-handoff soak for RC3) | Stable across cycles | Medium |
| T10 | Mode switching / recovery | T4 → esptool `firmware-merged.bin` @0x0 (back to standalone) → reflash Launcher → reinstall | Each transition boots; documented NVS/SPIFFS reset (RC4/RC5) happens *predictably*; onboarding re-runs; radio TX stays gated until reconfigured | No boot loops; recovery always possible via esptool full flash | Low |
| T11 | WiFi/BLE under Launcher | T4 → BLE companion pairing, WiFi scan in Settings | Radio coexistence unchanged | Same behavior as T1 | Low |
| T12 | SD card after handoff | T4 with map tiles on SD | `/sdcard` mounts, map renders | Same as T1 | Low–medium (shared SPI bus re-init after warm reset) |
| T13 | Factory-ish reset under Launcher | T4 → erase NVS via Launcher's WebUI NVS editor or `nvs_flash_erase` debug cmd | Onboarding re-runs cleanly | Clean first-boot path | Low |
| T14 | Native test suite | `pio test -e native_test -v` after each code change | All existing tests + new C3/C6 tests | 100 % pass | — |

Hardware notes: T4/T9 specifically exercise the physical input layer and **must** be run on real hardware (remote-test mode injects input downstream of the failure point — `CLAUDE.md`, "Remote Test Controller / LIMITATION").

---

## CI / Build / Release Changes

1. `.github/workflows/build-release.yml`: after the existing build step, copy `.pio/build/SigurdOS_TDeck/firmware-merged.bin` to `SigurdOS-tdeck-launcher.bin` and add it to `files:` in the release step (3-line change). No new build env required (unless O4 is adopted).
2. `scripts/merge_bin.py`: optionally emit the extra copy + a `launcher` entry in `webflasher/manifest.json` artifacts (keeps local builds consistent with CI). Not required for CI-only artifacts.
3. `firmware/README.md`: new "Install via Launcher" section (C2) + migration caveats (C7).
4. No changes to `pr-ci.yml`; native tests cover new helpers automatically once added under `test/test_<name>/`.
5. When C3–C6 land, remove/replace the "SigurdOS doesn't work under bmorcelli/Launcher" section of `docs/KNOWN_ISSUES.md` and flip the `docs/MISSING_FEATURES.md` entry, per the Known Issue Detection checklist in `CLAUDE.md`.

## Release / Catalog Integration Steps

1. Ship C1/C2 in the next tagged release so a versioned `…/releases/download/<tag>/SigurdOS-tdeck-launcher.bin` URL exists.
2. Verify Launcher's direct-URL install against that asset (T6).
3. Apply for a LauncherHub listing (category `t-deck`). **The submission process is not documented in the Launcher repo** — the catalog is fed by `api.launcherhub.net` and curated by the Launcher maintainer (the "Starred" list is explicitly maintainer-controlled, Launcher `README.md` 2.6.0 notes). Path: open an issue on bmorcelli/Launcher or ask in the project's Discord (linked from their README). Until listed, document the direct-URL/SD path.
4. Add the Launcher install path to release notes template in `build-release.yml`.

---

## Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| RC3 root cause is deeper than keyboard init timing (e.g., power-rail behavior during SW reset affects several peripherals) | Medium | High — could require HAL-wide "warm boot" support, the cost `docs/KNOWN_ISSUES.md` originally feared | Phase 5 bench work *before* committing to C6 scope; if cost explodes, ship Launcher support as "experimental" with documented limitations rather than blocking everything else |
| User installs `firmware.bin` via Launcher and silently loses persistence | High (it's the obvious file name) | Medium | C2 docs + C5 runtime diagnostic + C1 clearly-named artifact |
| SigurdOS self-OTA corrupts a co-installed app before C4 lands | Medium | High (data loss in someone else's firmware) | Land C3+C4 in the *first* code PR, before any public "works with Launcher" claim |
| Launcher internals change (partitioner, required partitions, LauncherHub API) | Medium over time | Medium | Pin analysis to v2.7.2 in this doc; re-verify table-detection logic (`0x8000` signature) at each release we claim to support |
| Launcher's IDF 5.5 bootloader exposes an incompatibility with our IDF 4.4-built app | Low | High | T4 is an early, cheap go/no-go check; fallback is building a Launcher-target env on a newer platform (big change — only if proven necessary) |
| NVS erase-on-geometry-change loses user settings unexpectedly | High when switching modes | Low (onboarding re-runs; TX stays safely gated) | C7 documentation; never advertise in-place switching |
| Detection helper (C3) misfires on future standalone partition layouts | Low | Medium | Detection keyed on `test`-subtype app partition, which no stock Arduino CSV contains; native test enumerating both layouts |

## Open Questions

| # | Question | How to resolve |
|---|---|---|
| Q1 | What exactly breaks at warm handoff (RC3)? Is it C3-keyboard mode/timing, GPIO-10 rail behavior, or something else entirely? | Bench session: Launcher + debug build of SigurdOS, USB CDC logs of `sigurdos_keyboard_init` probe results, logic analyzer on I2C (18/8) and GPIO 10 across the handoff. Physical hardware required |
| Q2 | Does the GPIO-10 peripheral power rail drop during `ESP.restart()` + bootloader + Launcher relaunch window, resetting the C3/GT911, or does it stay up? | Scope GPIO 10 and the 3V3-periph rail through a Launcher→app handoff |
| Q3 | Precise return-to-Launcher semantics on current builds (power-on always boots Launcher's resident slot vs. otadata-selected app)? Affects only documentation wording and O2 | Flash Launcher on a bench T-Deck, install any app, power-cycle and observe; Launcher's serial prints "Press the button to enter the Launcher!" when its boot screen runs |
| Q4 | Can a stock-framework app (no `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED`) write otadata to implement O2 "Reboot to Launcher"? otadata is not in the protected region (only 0x0–0x8FFF is), so `esp_partition_write` to otadata *should* work — verify | Prototype behind C3 detection on bench hardware |
| Q5 | LauncherHub submission process and metadata requirements (icons? descriptions? versioning?) | Ask the Launcher maintainer (GitHub issue or Discord); inspect `api.launcherhub.net/firmwares?category=t-deck` response shape for required fields |
| Q6 | Is 1 MB SPIFFS truly sufficient at `MAX_CONTACTS=350` plus channels and message stores? | Measure SPIFFS usage on a long-running standalone node (`SPIFFS.usedBytes()` via debug dump), compare against 0x100000 |
| Q7 | The tracked `webflasher/manifest.json` currently records a local 8 MB test env (`SigurdOS_TDeck_remote_test_radio_roomtest_8mb`, `partitions/partitions_8MB.csv`) that doesn't exist in tracked `platformio.ini` — regenerate from a clean `SigurdOS_TDeck` build at next release so published metadata matches the canonical 16 MB layout | One `pio run -e SigurdOS_TDeck` + commit during the next release |

## Phased Implementation Plan

### Phase 0 — Baseline research and reproducible builds *(this document)*
- ✅ Launcher v2.7.2 source analysis (install paths, partitioner, boot chain, T-Deck port, catalog API)
- ✅ SigurdOS build/partition/artifact/runtime audit with file citations
- Reproduce a clean release build (`pio run -e SigurdOS_TDeck`) and confirm `firmware-merged.bin` layout matches `scripts/merge_bin.py` expectations (also resolves Q7)
- **Exit:** this roadmap merged; bench T-Deck reserved for Launcher experiments (a device that may be repeatedly reflashed)

### Phase 1 — Artifact and partition audit (hardware-free)
- Byte-level check of `firmware-merged.bin`: partition-table signature at 0x8000, app entry subtype/offset, SPIFFS entry as Launcher's parser will read them (script: walk 0x20-byte entries exactly like `updateFromSD` does)
- Dry-run Launcher's layout math for our app size against `custom_16Mb.csv` starting state
- **Exit:** written confirmation that the merged artifact is a valid Launcher install source, or a fix list for `scripts/merge_bin.py`

### Phase 2 — Boot/flash compatibility (first hardware contact)
- Flash Launcher on the bench T-Deck; install `firmware-merged.bin` from SD; capture full serial logs
- Go/no-go on: image verify, partition creation, first boot reaching the UI, IDF-skew concerns
- Run T9 power-cycle loop for boot-chain stability
- **Exit:** SigurdOS boots under Launcher; remaining failures enumerated with logs (feeds Phase 5)

### Phase 3 — Board and hardware mapping
- Resolve Q1/Q2 (bench instrumentation of I2C, GPIO 10, keyboard probe)
- Characterize GT911, ST7789, SD, SX1262 state after handoff vs. cold boot
- **Exit:** RC3 root cause documented; C6 scope finalized (or declared unnecessary)

### Phase 4 — Launcher packaging/catalog support (code + CI)
- C1 (release artifact), C2/C7 (docs), C3 (detection helper + native tests), C4 (self-OTA gate), C5 (diagnostics), C6 (as scoped by Phase 3)
- Each as a separate small PR against `dev`, with `pio test -e native_test` green and hardware-testing declarations per `CONTRIBUTING.md`
- **Exit:** all required changes merged; `docs/KNOWN_ISSUES.md` entry replaced with current status

### Phase 5 — Runtime regression testing
- Execute the full testing matrix T1–T14 (standalone rows first — they are the non-negotiable regression gate; then Launcher rows)
- Soak: 24 h mesh operation under Launcher install (identity stability, saveState cycle, heap)
- **Exit:** matrix recorded in the PR(s)/release notes; standalone rows byte-for-byte behavior-identical to baseline

### Phase 6 — Release integration and maintainer handoff *(catalog info gathered)*
- Tagged release including the Launcher artifact; release-notes section for Launcher users
- LauncherHub submission (Q5): **API researched (2026-06-10).** LauncherHub at `api.launcherhub.net` has 16 T-Deck firmwares; format is `{fid, name, author, star}`. **Submission requires opening an issue on [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) or contacting the maintainer via Discord** — the catalog is curated (no self-service API). Our release assets are ready: `SigurdOS-tdeck-launcher.bin` with range-request support at the versioned `…/releases/download/<tag>/SigurdOS-tdeck-launcher.bin` asset.
- Update this document to "supported" status with the validated Launcher version range; hand off the re-verification checklist (risk table) to the release process
- **Exit:** Launcher install path documented, released, and reproducible by users without maintainer involvement

---

*Analysis pinned to: SigurdOS-tdeck `dev` @ 5ec2caf (beta-0.1.40), bmorcelli/Launcher v2.7.2. Re-verify Launcher-side claims against newer Launcher releases before implementation phases.*
