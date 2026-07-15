# Launcher Compatibility Detection

> **Source**: `src/hal/launcher_env.h`, `src/hal/launcher_env.cpp`
> **Tests**: `test/test_launcher_env/test_launcher_env.cpp`
> **Mocks**: `test/mocks/esp_partition.h`, `test/mocks/mock_esp_partition.cpp`
> **Related**: `docs/LAUNCHER_ROADMAP.md` (full compatibility analysis), `docs/KNOWN_ISSUES.md` → "SigurdOS Launcher compatibility"

---

## Table of Contents

- [I. Overview](#i-overview)
- [II. Detection Mechanism](#ii-detection-mechanism)
- [III. Public API](#iii-public-api)
- [IV. Partition Table Comparison](#iv-partition-table-comparison)
- [V. Testing](#v-testing)
- [VI. Source Files](#vi-source-files)

---

## I. Overview

[bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) is a resident "app store" firmware for ESP32 devices with an explicit LilyGo T-Deck port. It keeps itself in a small protected app partition, rewrites the device's partition table at runtime to carve out an OTA app partition for each firmware it installs, and chain-boots installed firmware via `otadata` + software reset. Users return to Launcher at power-on (Launcher's custom bootloader boots its resident partition first).

**Why it matters for SigurdOS:**

1. **Different flash layout.** Standalone SigurdOS uses the stock `default_16MB.csv` partition table (dual OTA slots, large SPIFFS, nvs/otadata at stock offsets). Under Launcher, SigurdOS runs under a dynamically generated partition table with a single app slot, smaller SPIFFS, and different nvs/otadata geometry.

2. **Self-OTA is dangerous under Launcher.** The Arduino `Update` mechanism targets `esp_ota_get_next_update_partition()`, which under Launcher could overwrite another installed firmware's partition, corrupting Launcher's app registry.

3. **App-only installs lose persistence.** Launcher's app-only install path creates no SPIFFS partition, so mesh identity/contacts/channels never persist across reboots.

4. **Hardware handoff differences.** Launcher initialises I2C, touch, display, and keyboard backlight before jumping to SigurdOS via software reset, leaving external peripherals in a different state than a cold boot.

The detection system documented here provides a compact runtime check (`true`/`false`) so the rest of the firmware can gate behaviour accordingly — currently used to disable self-OTA and print tailored diagnostics.

---

## II. Detection Mechanism

Detection uses a **dual-signal** approach: two independent partition probes must both match before the firmware considers itself under Launcher. Neither signal alone is sufficient.

### Primary Signal: Resident `test`-Subtype App Partition

Launcher's own resident app partition uses the `ESP_PARTITION_SUBTYPE_APP_TEST` (0x20) subtype. This partition lives at offset `0x10000` with size `0x180000` and is defined in Launcher's `custom_16Mb.csv`:

```
app0, app, test, 0x10000, 0x180000
```

The stock Arduino `default_16MB.csv` partition table **never** contains an app partition with the `test` subtype — it only uses `ota_0` (subtype 0x10) and `ota_1` (subtype 0x11).

The probe searches for any app partition whose subtype is `ESP_PARTITION_SUBTYPE_APP_TEST`:

```cpp
bool has_launcher_resident_partition()
{
    esp_partition_iterator_t it = esp_partition_find(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_TEST,
        NULL);
    bool found = (it != NULL);
    if (it) {
        esp_partition_iterator_release(it);
    }
    return found;
}
```

### Confirmatory Signal: Otadata at 0xD000

Under Launcher the `otadata` partition lives at flash offset `0xD000`, whereas in the standalone `default_16MB.csv` layout it lives at `0xE000`. The probe finds the otadata partition by subtype and checks its address:

```cpp
constexpr uint32_t LAUNCHER_OTADATA_OFFSET = 0xD000;

bool has_launcher_otadata_partition()
{
    esp_partition_iterator_t it = esp_partition_find(
        ESP_PARTITION_TYPE_DATA,
        static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_DATA_OTA),
        NULL);
    if (!it) {
        return false;
    }
    const esp_partition_t* partition = esp_partition_get(it);
    bool found = partition && partition->address == LAUNCHER_OTADATA_OFFSET;
    esp_partition_iterator_release(it);
    return found;
}
```

### Combined Logic

```cpp
bool sigurdos_is_under_launcher()
{
    return has_launcher_resident_partition()
        && has_launcher_otadata_partition();
}
```

Both signals must be `true`. A custom table that happens to contain an `APP_TEST` slot but keeps otadata at `0xE000` will not trigger detection; conversely, a table with otadata at `0xD000` but no `test`-subtype app partition will also not trigger it.

### False Positive Prevention

False positives are **structurally impossible** on the stock Arduino `default_16MB.csv` layout because:

| Signal | Standalone (`default_16MB.csv`) | Launcher (`custom_16Mb.csv`) |
|--------|-------------------------------|------------------------------|
| `APP_TEST` partition (0x20) | Never present | Present (Launcher's resident slot) |
| Otadata at `0xD000` | No (it's at `0xE000`) | Yes |
| Both signals simultaneously | Impossible | Required for match |

The only way both signals could fire together is if a custom partition table includes both a `test`-subtype app partition **and** moves otadata to `0xD000`. This is by design — Launcher itself is the only known firmware that creates such a layout. If a future firmware also uses this combination, it would be detected as "under Launcher", which is a safe failure (self-OTA would be gated, which is correct for any single-app-slot layout).

---

## III. Public API

### `sigurdos_is_under_launcher()`

```cpp
bool sigurdos_is_under_launcher();
```

**Returns:** `true` when the current firmware is installed under bmorcelli/Launcher, `false` otherwise.

**Detection logic:** requires both the Launcher `test`-subtype resident app partition and the Launcher otadata location at `0xD000`.

**Usage locations:**
- `src/hal/wifi_ota.cpp` — gates WiFi OTA with "Update SigurdOS through Launcher instead" when detected
- `src/hal/github_ota.cpp` — gates GitHub release OTA when detected
- `src/main.cpp` — controls boot-time persistence diagnostics (SPIFFS mount failure under Launcher triggers a targeted warning about app-only install)

### `sigurdos_launcher_env_name()`

```cpp
const char* sigurdos_launcher_env_name();
```

**Returns:** a human-readable string — `"bmorcelli/Launcher"` when detected, `"standalone"` otherwise.

**Purpose:** used for log messages, serial console output, and debug telemetry. The return value is a pointer to a string literal (no heap allocation).

---

## IV. Partition Table Comparison

### Standalone (`default_16MB.csv`)

The stock Arduino `default_16MB.csv` layout as used by SigurdOS standalone builds:

| Name | Type | Subtype | Offset | Size |
|------|------|---------|--------|------|
| `nvs` | data | nvs (0x02) | `0x9000` | `0x5000` (20 KB) |
| `otadata` | data | ota (0x00) | `0xE000` | `0x2000` (8 KB) |
| `app0` | app | ota_0 (0x10) | `0x10000` | `0x640000` (~6.25 MB) |
| `app1` | app | ota_1 (0x11) | `0x650000` | `0x640000` (~6.25 MB) |
| `spiffs` | data | spiffs (0x82) | `0xC90000` | `0x360000` (~3.38 MB) |
| `coredump` | data | coredump | `0xFF0000` | `0x10000` (64 KB) |

Key characteristics:
- Two OTA app slots (`ota_0` / `ota_1`) for self-OTA via Arduino `Update`
- Otadata at `0xE000` — 0x1000 bytes after nvs
- NVS at `0x9000` with 0x5000 byte size (5 sectors)
- Large SPIFFS partition (3.38 MB) for mesh identity, contacts, channels, and message history

### Launcher (`custom_16Mb.csv` — Launcher's resident layout)

Launcher's own partition table, before it rewrites the table at install time:

| Name | Type | Subtype | Offset | Size |
|------|------|---------|--------|------|
| `nvs` | data | nvs | `0x9000` | `0x4000` (16 KB) |
| `otadata` | data | ota | `0xD000` | `0x2000` (8 KB) |
| `phy_init` | data | phy | `0xF000` | `0x1000` (4 KB) |
| `app0` | app | test (0x20) | `0x10000` | `0x180000` (1.5 MB) |
| `coredump` | data | coredump | `0x190000` | `0x10000` (64 KB) |

Key differences from standalone:

| Aspect | Standalone | Launcher |
|--------|-----------|----------|
| **NVS size** | `0x5000` (20 KB, 5 sectors) | `0x4000` (16 KB, 4 sectors) |
| **Otadata offset** | `0xE000` | `0xD000` |
| **App slot subtype** | `ota_0` (0x10) / `ota_1` (0x11) | `test` (0x20) — Launcher's resident slot |
| **App slots** | 2 (6.25 MB each) | 1 (1.5 MB resident) + dynamic per installed app |
| **SPIFFS** | `0xC90000` / 3.38 MB (fixed) | Created dynamically at install time, 1 MB default on 16 MB boards |
| **`phy_init`** | Present in esp-idf default but merged binary | Explicitly present |
| **Self-OTA** | Supported via dual OTA slots | Dangerous — would overwrite co-installed apps |

### Under-Launcher Runtime Layout (dynamic, created by Launcher's installer)

When SigurdOS is installed via Launcher's merged-bin path, Launcher's partition installer creates a layout like:

```
nvs @0x9000     size 0x4000
otadata @0xD000 size 0x2000
phy_init @0xF000 size 0x1000
app0 (test)  @0x10000 size 0x180000   ← Launcher's resident slot
... Launcher-managed gap ...
SigurdOS app slot  @ <dynamic offset> size ≥ app_size
spiffs              @ <dynamic offset> size 0x100000 (1 MB)
coredump            @ <dynamic offset> size 0x10000
```

The exact offsets for the installed app and its SPIFFS partition are chosen dynamically based on flash space available. The important constant is `LAUNCHER_OTADATA_OFFSET = 0xD000` — this is what the detection system probes.

---

## V. Testing

### Test Suite

The test file `test/test_launcher_env/test_launcher_env.cpp` contains **six test cases** covering all signal combinations. Tests use mock ESP-IDF partition functions (`test/mocks/mock_esp_partition.cpp`) with explicit test controls:

| Helper | Purpose |
|--------|---------|
| `sigurdos::test::mock_launcher_partition(bool present)` | Controls whether `esp_partition_find` returns a `test`-subtype app partition |
| `sigurdos::test::mock_otadata_partition(bool present, uint32_t address)` | Controls whether an otadata partition exists and at what address |

### Test Cases

| Test | `test` partition | Otadata address | Expected result | Verifies |
|------|-----------------|-----------------|-----------------|----------|
| `DetectsStandaloneByDefault` | absent | `0xE000` | `false` / `"standalone"` | Default mock state matches standalone layout |
| `DetectsLauncherWhenTestPartitionExists` | present | `0xD000` | `true` / `"bmorcelli/Launcher"` | Both signals together → detection |
| `TestPartitionWithoutLauncherOtadataStaysStandalone` | present | `0xE000` | `false` | Primary signal alone is **not** sufficient |
| `LauncherOtadataWithoutTestPartitionStaysStandalone` | absent | `0xD000` | `false` | Confirmatory signal alone is **not** sufficient |
| `MissingOtadataStaysStandalone` | present | (absent) | `false` | Missing otadata (even with `test` partition) → no detection |
| `StandaloneAfterLauncherThenRemove` | present→absent | `0xD000` | `true` → `false` | State is dynamic — removing the `test` partition reverts detection |

### Running the Tests

```bash
# Run all launcher env tests
pio test -e native_test -f test_launcher_env -v

# Expected output: 6 tests, all passed
```

### Mock Implementation

The mock ESP-IDF partition API (`test/mocks/esp_partition.h`, `test/mocks/mock_esp_partition.cpp`) provides:

- `esp_partition_find()` — returns non-NULL iterators based on the mock state variables `s_has_test_partition` and `s_has_otadata_partition`
- `esp_partition_iterator_release()` — no-op (no allocation happens in the mock)
- `esp_partition_get()` — returns the address of the static partition struct matching the current mock state
- The otadata mock struct's `address` field is updated by `mock_otadata_partition()` to simulate `0xD000` or `0xE000`

The mock function signatures match the real ESP-IDF API exactly, so `launcher_env.cpp` compiles and links in the native test environment without modification.

### Test Coverage Summary

| Scenario | Coverage |
|----------|----------|
| Standalone (default layout, no `test` partition) | ✅ `DetectsStandaloneByDefault` |
| Genuine Launcher (both signals) | ✅ `DetectsLauncherWhenTestPartitionExists` |
| Custom table with `test` partition but stock otadata | ✅ `TestPartitionWithoutLauncherOtadataStaysStandalone` |
| Custom table with `0xD000` otadata but no `test` partition | ✅ `LauncherOtadataWithoutTestPartitionStaysStandalone` |
| Missing otadata entirely | ✅ `MissingOtadataStaysStandalone` |
| Dynamic transition (Launcher → standalone) | ✅ `StandaloneAfterLauncherThenRemove` |
| Both the `env_name()` string and `is_under()` bool are always checked | ✅ Each test asserts both |
| Standalone OTA running from `app1` | ✅ Identical to standalone (no `test` partition in any slot) |
| Multiple apps under Launcher | ✅ Dynamic test proves re-probing after state change |

---

## VI. Source Files

| File | Purpose |
|------|---------|
| `src/hal/launcher_env.h` | Public API declarations + detection mechanism overview comment |
| `src/hal/launcher_env.cpp` | Detection implementation using `esp_partition_find` |
| `test/test_launcher_env/test_launcher_env.cpp` | 6 native test cases for all signal combinations |
| `test/mocks/esp_partition.h` | Mock ESP-IDF partition types, API declarations, and test control interface (`mock_launcher_partition`, `mock_otadata_partition`) |
| `test/mocks/mock_esp_partition.cpp` | Mock state variables and ESP-IDF stub implementations |

### Callers

| Caller | What it does when Launcher is detected |
|--------|----------------------------------------|
| `src/hal/wifi_ota.cpp` | Refuses to start the WiFi AP/OTA web server; shows "Update SigurdOS through Launcher instead" |
| `src/hal/github_ota.cpp` | Refuses to download and flash a GitHub release; shows "Update SigurdOS through Launcher instead" |
| `src/main.cpp` | On SPIFFS mount failure, prints a Launcher-specific diagnostic: "installed app-only — reinstall from the merged/Launcher image for persistence" |
| `src/ui/screens/screen_settings_system.cpp` | Disables the OTA-related Settings entries with an explanation when under Launcher |
