# ESP32-S3 eFuse Safety Audit

**Issue:** #1210, “security: define a production ESP32-S3 hardware root of trust”  
**Audit date:** 2026-07-27  
**Repository revision:** `924d6cc9ce715d08108026fe967358ba28b029a0`  
**MeshCore revision:** `a9b5174158882554c9722a6d58143db68fefc7ee`

## Executive summary

The firmware artifact produced by the repository's current pinned
Arduino-ESP32 toolchain is safe from an accidental eFuse burn: Secure Boot,
flash encryption, and application anti-rollback are compiled out of the pinned
prebuilt ESP32-S3 SDK. The repository contains no `espefuse` invocation, direct
eFuse-write API call, provisioning script, CI provisioning step, or PlatformIO
upload hook that burns eFuses. The eFuse accesses compiled into the application
are reads used for the MAC address, ADC calibration, and chip/security-state
inspection.

The repository as a source and configuration contract is **not yet safe enough
to guarantee “never burn eFuses.”** Two findings are critical:

1. `sdkconfig.defaults` actively enables
   `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y`.
2. The normal boot-health path calls
   `esp_ota_mark_app_valid_cancel_rollback()`.

ESP-IDF anti-rollback is not software-only and does not require Secure Boot V2
to burn an eFuse. When anti-rollback is compiled in, confirming a new image can
advance the chip's eFuse secure version. ESP-IDF can also advance it from the
bootloader when selecting a serially flashed image with a higher secure
version. The current build happens to be safe because the Arduino framework
uses a precompiled SDK whose actual configuration has anti-rollback disabled;
the configured `board_build.sdkconfig_path` is not consumed by PlatformIO's
Arduino builder. That incidental toolchain behavior is not an adequate safety
gate.

**Verdict:** do not close #1210. First remove the active anti-rollback setting
from every default/development profile, explicitly prohibit anti-rollback and
all other eFuse-burning options in a CI contract, and guard or remove the
production provisioning design. The current firmware may be built and flashed
without burning eFuses, but a future framework/build change could activate the
latent eFuse update path.

## Scope and method

The audit covered all 690 tracked files, the pinned MeshCore submodule, 68
Python source files under `scripts/`, all six workflow files under
`.github/workflows/`, local ignored configuration remnants, PlatformIO and
CMake/build configuration, and the existing `.pio` build plus pinned
Arduino-ESP32 package. Generated dependency trees were searched separately
from repository-authored files so toolchain implementations were not mistaken
for SigurdOS call sites.

Searches included:

- `espefuse`, `esp_efuse`, `esp_efuse_burn`, `efuse_burn`, `burn_efuse`
- eFuse write/batch/protect APIs and `ets_efuse_program`
- Secure Boot, flash-encryption, ROM-download-disable, JTAG-disable, and
  anti-rollback Kconfig symbols
- provisioning, key-burning, security-version, build, flash, upload, and CI
  paths

The storage security contract test was executed directly. The existing
`SigurdOS_TDeck` ELF, map, bootloader, pinned SDK configuration, and symbol
table were also inspected without flashing hardware.

## Findings

| Severity | File and line | Finding | Risk / present state |
|---|---|---|---|
| 🔴 CRITICAL | `sdkconfig.defaults:7-8` | The development/default fragment actively sets `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y` and secure version 1. | The comment at lines 5-6 is incorrect if this configuration is ever applied. ESP-IDF anti-rollback uses the eFuse secure-version field independently of Secure Boot V2. This is a latent irreversible default. |
| 🔴 CRITICAL | `src/hal/ota_boot_health.cpp:49-63` | The normal boot-health loop calls `esp_ota_mark_app_valid_cancel_rollback()` after a healthy pending boot. Its compile guard checks rollback support, not that eFuse anti-rollback is disabled. | With application anti-rollback compiled in, this standard API marks the image valid **and updates the secure-version eFuse**. The call is safe in the currently pinned SDK only because `CONFIG_APP_ANTI_ROLLBACK` is not set there. |
| 🟡 WARNING | `platformio.ini:99-108`, `platformio.ini:192-196` | Firmware environments declare `framework = arduino` and use `board_build.sdkconfig_path`. The installed PlatformIO Arduino builder does not consume this option; custom sdkconfig processing belongs to the ESP-IDF builder. | Today this prevents the dangerous default anti-rollback line from taking effect, but it also means the repository's sdkconfig safety claims do not describe the artifact. A migration to ESP-IDF, Arduino-as-component, or a builder that honors the fragment could silently activate it. Debug settings in `sdkconfig.debug` are affected by the same mismatch. |
| 🟢 SAFE | Pinned Arduino-ESP32 SDK configuration and current `SigurdOS_TDeck` build artifact | The actual ESP32-S3 SDK has rollback enabled but `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK`, `CONFIG_SECURE_BOOT`, and `CONFIG_SECURE_FLASH_ENC_ENABLED` unset. The current ELF has no `esp_efuse_update_secure_version` symbol or call/reference to `ets_efuse_program`. | Current normal builds do not contain the anti-rollback eFuse update implementation. This is build evidence, not a durable source-level gate. |
| 🟡 WARNING | `sdkconfig.defaults.production:1-29` | Secure Boot V2, release-mode flash encryption, anti-rollback, ROM download disable, and UART console-disable lines are all commented out. No script, CMake file, PlatformIO hook, or workflow references this fragment or uncomments it. | The file is inert and cannot be accidentally activated by any current repository automation. It remains a copy/paste recipe for forbidden irreversible features; merely removing `#` or adopting a merge process would be dangerous. The statement that the fragment “enables” switches is presently false because every switch is commented. |
| 🟡 WARNING | `docs/PRODUCTION_ROOT_OF_TRUST.md:23-30` | The profile table says the default has no eFuse impact and that the production fragment enables compile-time switches without burning. | The default claim relies on the fragment being ignored. If anti-rollback were honored, it could burn the secure-version eFuse through boot/OTA lifecycle APIs. Release-mode flash encryption can also burn keys and protection/configuration eFuses during the first boot; it is not necessarily separated from runtime by a provisioning script. |
| 🟡 WARNING | `docs/PRODUCTION_ROOT_OF_TRUST.md:27`, `docs/PRODUCTION_ROOT_OF_TRUST.md:32-43` | Documentation references `scripts/provision_production_device.py` and an irreversible provisioning sequence. The script does not exist anywhere in the repository. | No executable provisioning path exists, which is safe operationally, but the documented path is stale/incomplete and directly conflicts with the project's “never burn eFuses” rule. The step “flash ... with `espefuse` dry-run” is also imprecise: `espefuse` manages eFuses, while `esptool` flashes images. |
| 🟢 SAFE | `docs/PRODUCTION_ROOT_OF_TRUST.md:59-63` | The document states CI, normal PlatformIO builds, and hardware-test paths must not burn eFuses. | This policy matches the audited automation, but it is not enforced by a denylist test. |
| 🟡 WARNING | `scripts/tests/test_storage_security_contract.py:8-17` | The test requires active release-mode flash encryption and encrypted-NVS settings in `sdkconfig.defaults`; those settings are absent. | Running `python3 -m unittest scripts.tests.test_storage_security_contract` fails on the first missing setting. “Fixing” the test by adding those options to the default profile could cause first-boot eFuse key/configuration burns. The contract points maintainers toward an unsafe remediation. |
| 🟡 WARNING | `docs/SECURITY_MODEL.md:39-45` | The security model says release builds enable flash encryption and encrypted NVS. | Current release builds use the pinned unencrypted Arduino SDK, and the storage contract fails. This overstates protection and creates pressure to enable irreversible defaults without a hardware-safe design. |
| 🟡 WARNING | `sdkconfig.defaults.bak:4-18` (ignored local file) | An ignored backup contains unresolved conflict markers plus active release-mode flash encryption, NVS encryption, and anti-rollback settings. | It is not tracked and no build references it, so it cannot affect current builds. It is nevertheless a hazardous local template that could be copied back over the canonical file. |
| 🟢 SAFE | `scripts/stamp_security_epoch.py:12-108`, `platformio.ini:151-156` | The post-build script writes `SIGURDOS_SECURITY_EPOCH` into the application descriptor and repairs the image checksum/hash. | This modifies only an image file and does not access hardware or eFuses. Its stamped value becomes eFuse-relevant only if hardware anti-rollback is enabled, reinforcing the need to keep that Kconfig option prohibited. |
| 🟢 SAFE | `src/hal/wifi_ota.cpp:42-46`, `src/hal/github_ota.cpp:126-130` | OTA code reads the running image's `secure_version` for a software downgrade check. | Image-descriptor reads and comparisons do not read or write eFuses. |
| 🟢 SAFE | `lib/meshcore/src/helpers/esp32/ESPNOWRadio.cpp:68`, `lib/meshcore/src/helpers/esp32/SerialBLEInterface.cpp:20` | MeshCore calls `esp_efuse_mac_get_default()` to obtain the factory MAC. `AsyncElegantOTA.cpp:125` uses `ESP.getEfuseMac()` for an identifier. | These are read-only factory-MAC accesses. `AsyncElegantOTA` is also not a SigurdOS eFuse provisioning path. |
| 🟢 SAFE | `src/hal/battery.cpp:33`, `src/hal/tdeck_board.h:261`, `test/mocks/Arduino.h:97`, related hardware docs | Battery measurement uses ESP32 ADC calibration data stored in eFuse. | Calibration reads are permitted and do not program eFuses. |
| 🟢 SAFE | `scripts/` (all Python sources) | No Python script imports/calls `espefuse`, invokes an eFuse-write command, or contains a direct eFuse programming path. The only matches are the software epoch stamper and the mismatched storage test. | Hardware flash helpers use normal image flashing, not eFuse programming. The documented production provisioning script is absent. |
| 🟢 SAFE | `.github/workflows/*.yml`, `.github/actions/` | No workflow/action mentions or invokes `espefuse`, an eFuse API, Secure Boot provisioning, flash-encryption provisioning, JTAG disabling, ROM-download disabling, or anti-rollback provisioning. | CI builds and packages firmware only. No workflow has an eFuse-burning step. |
| 🟢 SAFE | PlatformIO extra scripts and repository CMake/build files | Extra scripts patch USB CDC, check source patches, add metadata, stamp the image epoch, and merge images. No hook calls `espefuse` or enables a production fragment. No repository CMake file auto-enables an eFuse-burning configuration. | Normal `pio run` and the audited build hooks do not provision hardware. |
| 🟢 SAFE | Repository-wide direct API/command search | No SigurdOS source calls `esp_efuse_write_*`, `esp_efuse_batch_write_commit`, `esp_efuse_update_secure_version`, `ets_efuse_program`, key-burning, eFuse protection, or JTAG/download-disable APIs. | There is no direct first-party eFuse writer. The only latent write is indirect through ESP-IDF's OTA anti-rollback behavior described above. |

## Required recommendations

### 1. Remove hardware anti-rollback from every normal profile

Delete `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y` and
`CONFIG_BOOTLOADER_APP_SECURE_VERSION=1` from `sdkconfig.defaults`. Retain
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` only if OTA health rollback is needed;
ordinary application rollback state is software/flash metadata and does not
require the secure-version eFuse.

Add explicit negative settings, checked against the final resolved build
configuration, for at least:

```text
# CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK is not set
# CONFIG_SECURE_BOOT is not set
# CONFIG_SECURE_BOOT_V2_ENABLED is not set
# CONFIG_SECURE_FLASH_ENC_ENABLED is not set
# CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE is not set
# CONFIG_SECURE_DISABLE_ROM_DL_MODE is not set
```

The exact resolved symbol names must match the pinned ESP-IDF/Arduino version.
Do not rely solely on defaults-file text, because the current Arduino build
ignores that file.

### 2. Add a fail-closed eFuse safety contract

CI should fail if any first-party file introduces:

- `espefuse`, any `burn_*efuse`/`burn_key` operation, or any direct eFuse-write
  API;
- active Secure Boot, flash encryption, anti-rollback, ROM-download-disable,
  or JTAG-disable settings;
- a production provisioning script or PlatformIO upload hook without an
  explicit policy change and hardware-owner review.

The check should inspect both repository text and the **resolved artifact SDK
configuration/symbols**. For the current Arduino build, assert that the selected
SDK's `CONFIG_APP_ANTI_ROLLBACK` is unset and that the linked ELF has no
`esp_efuse_update_secure_version` implementation/reference. A future framework
migration must fail until this safety assertion is updated deliberately.

### 3. Harden the OTA health path

Keep the rollback confirmation call only under an explicit compile-time
assertion that application anti-rollback is disabled. A build should fail, not
merely log a warning, if the source sees `CONFIG_APP_ANTI_ROLLBACK` or its
version-specific equivalent. This prevents the innocent-looking
`esp_ota_mark_app_valid_cancel_rollback()` call from becoming an eFuse update
after a toolchain change.

### 4. Resolve the production-profile contradiction

Under the stated “never burn eFuses” rule, remove the production eFuse profile
and irreversible operator sequence, or move them to a separate provisioning
repository with separate authorization and hardware ownership. At minimum:

- remove the nonexistent `scripts/provision_production_device.py` reference;
- do not describe the fully commented fragment as enabling anything;
- state that release-mode flash encryption may program eFuses during device
  boot and is prohibited in SigurdOS firmware;
- do not add a provisioning script to this repository.

### 5. Replace the unsafe storage test

Change `test_storage_security_contract.py` so it asserts that eFuse-burning
security settings are absent/disabled in normal builds. If encrypted storage is
still a product goal, design a non-eFuse software encryption approach or keep
the manufacturing system completely outside this repository and outside
ordinary firmware/CI flows. Do not satisfy the present failing test by enabling
flash encryption in `sdkconfig.defaults`.

### 6. Correct security claims and remove hazardous remnants

Update `docs/SECURITY_MODEL.md` to reflect that current release artifacts do
not enable ESP32 flash encryption or encrypted NVS. Remove the local
`sdkconfig.defaults.bak` remnant from development workspaces; it is ignored by
Git but contains dangerous active settings.

## Verdict for issue #1210

#1210 should remain open. The following gates remain:

- [ ] Default and debug/development profiles explicitly prohibit hardware
      anti-rollback, Secure Boot, flash encryption, JTAG disable, and ROM
      download disable.
- [ ] OTA image confirmation has a compile-time prohibition against
      anti-rollback/eFuse secure-version updates.
- [ ] CI scans first-party commands/config and validates the resolved build
      artifact for absence of eFuse-write capability.
- [ ] The failing storage-security contract is replaced with an eFuse-safe
      contract.
- [ ] The nonexistent provisioning-script reference and misleading production
      process are removed or isolated outside this repository.
- [ ] Security documentation matches the actual release artifact.

After those gates pass, the issue can be closed with the conclusion that
SigurdOS intentionally has **no ESP32-S3 hardware-root-of-trust provisioning
path in this repository**. If a production SKU later requires irreversible
eFuse provisioning, that must be handled as a separately authorized system;
it cannot coexist with this repository's absolute “never burn eFuses” rule.

## External technical basis

- Espressif's ESP-IDF v4.4 OTA documentation states that anti-rollback compares
  the application version with the chip eFuse and that
  `esp_ota_mark_app_valid_cancel_rollback()` updates the secure version on the
  chip:
  <https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/system/ota.html#anti-rollback>
- Espressif's eFuse API documents `esp_efuse_update_secure_version()` as writing
  the secure-version eFuse:
  <https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/system/efuse.html>
- The ESP-IDF v4.4.7 Kconfig reference describes
  `CONFIG_BOOTLOADER_APP_SECURE_VERSION` as recorded in the eFuse field and
  provides a separate virtual-eFuse emulation option for tests:
  <https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/api-reference/kconfig.html>
