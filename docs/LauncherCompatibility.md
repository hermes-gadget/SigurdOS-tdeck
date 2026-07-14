# SigurdOS T-Deck ↔ bmorcelli/Launcher Compatibility Analysis

> **Analysis date:** 2026-06-17 (updated 2026-07-14)
> **SigurdOS version:** beta-0.1.44-RC6 (`dev` branch)
> **Launcher version:** v2.7.2 (analyzed from GitHub source)
> **Methodology:** Source-only analysis — no changes made, no physical hardware tested

---

## Table of Contents

- [Executive Summary](#executive-summary)
- [1. Pin Map Comparison](#1-pin-map-comparison)
- [2. Partition Table Compatibility](#2-partition-table-compatibility)
- [3. Boot Flow & Warm-Handoff Analysis](#3-boot-flow--warm-handoff-analysis)
- [4. Peripheral Driver Deep Dive](#4-peripheral-driver-deep-dive)
- [5. T-Deck Variant Differences](#5-t-deck-variant-differences)
- [6. Things That Will Work](#6-things-that-will-work)
- [7. Things That Might Get In The Way](#7-things-that-might-get-in-the-way)
- [8. T-Deck Pro / Hardware Variants](#8-t-deck-pro--hardware-variants)
- [9. Required Changes Status](#9-required-changes-status)
- [10. Bottom Line](#10-bottom-line)

---

## Executive Summary

**SigurdOS is structurally installable by Launcher, but has several incompatibilities that would prevent a reliable experience.** The core issues are:

| # | Problem | Severity | Root Cause |
|---|---------|----------|------------|
| **A** | Warm-handoff peripheral state mismatch | **HIGH** | Launcher's `ESP.restart()` doesn't reset C3 keyboard MCU, GT911, ST7789 |
| **B** | Self-OTA would corrupt Launcher's app registry | **HIGH** | SigurdOS assumes dual-OTA partition layout; Launcher has single-app slots |
| **C** | App-only installs lose all persistence | **HIGH** | Launcher creates no SPIFFS when installing `firmware.bin` (no partition table at 0x8000) |
| **D** | NVS geometry differs | **MEDIUM** | Standalone uses NVS size 0x5000 vs Launcher 0x4000 |
| **E** | SPIFFS shrinks from 3.4→1 MB under Launcher | **LOW** | Launcher's merged-bin path creates SPIFFS at 1 MB default |
| **F** | Not listed in LauncherHub catalog | **MEDIUM** | No catalog presence — users must sideload via SD or manual URL |

The firmware already has detection logic and self-OTA gating implemented. The remaining issues are well-understood and tracked in the [Launcher Roadmap](LAUNCHER_ROADMAP.md).

---

## 1. Pin Map Comparison

Every pin SigurdOS touches matches the pins Launcher's T-Deck port touches. **The pin maps are identical.**

| Function | SigurdOS (`tdeck_pins.h`) | Launcher (`platformio.ini`/`interface.cpp`) | Match? |
|----------|---------------------------|---------------------------------------------|--------|
| TFT CS | GPIO 12 | `TFT_CS=12` | ✅ |
| TFT DC | GPIO 11 | `TFT_DC=11` | ✅ |
| TFT RST | -1 (NC) | `TFT_RST=GFX_NOT_DEFINED` | ✅ |
| TFT BL | GPIO 42 | `BACKLIGHT=42` | ✅ |
| TFT SCLK | GPIO 40 | `TFT_SCLK=40` | ✅ |
| TFT MOSI | GPIO 41 | `TFT_MOSI=41` | ✅ |
| TFT MISO | GPIO 38 | `TFT_MISO=38` | ✅ |
| LoRa CS | GPIO 9 | GPIO 9 (driven HIGH) | ✅ |
| LoRa DIO1 | GPIO 45 | (not configured by Launcher) | ✅ |
| LoRa RST | GPIO 17 | (not configured by Launcher) | ✅ |
| LoRa BUSY | GPIO 13 | (not configured by Launcher) | ✅ |
| SD CS | GPIO 39 | `SDCARD_CS=39` | ✅ |
| Touch SDA | GPIO 18 | `KB_I2C_SDA=18` | ✅ |
| Touch SCL | GPIO 8 | `KB_I2C_SCL=8` | ✅ |
| Touch INT | GPIO 16 | `BOARD_TOUCH_INT=16` | ✅ |
| Keyboard I2C | 0x55 | `LILYGO_KB_SLAVE_ADDRESS=0x55` | ✅ |
| Touch I2C | 0x5D | `GT911_SLAVE_ADDRESS_L` (0x5D) | ✅ |
| Trackball Up | GPIO 3 | `UP_BTN=3` | ✅ |
| Trackball Down | GPIO 15 | `DW_BTN=15` | ✅ |
| Trackball Left | GPIO 1 | `L_BTN=1` | ✅ |
| Trackball Right | GPIO 2 | `R_BTN=2` | ✅ |
| Trackball Click | GPIO 0 | `SEL_BTN=0` | ✅ |
| Batt ADC | GPIO 4 | `ANALOG_BAT_PIN=4` | ✅ |
| Periph Power | GPIO 10 | `PIN_POWER_ON=10` | ✅ |
| Buzzer | GPIO 46 | (not configured) | ✅ |
| GPS RX/TX | 44/43 | (not configured) | ✅ |

---

## 2. Partition Table Compatibility

### Standalone SigurdOS (`default_16MB.csv`)

```
nvs,      data, nvs,      0x9000,  0x5000   (20 KB)
otadata,  data, ota,      0xE000,  0x2000   (8 KB)
app0,     app,  ota_0,    0x10000, 0x640000 (6.25 MB)
app1,     app,  ota_1,    0x650000,0x640000 (6.25 MB)
spiffs,   data, spiffs,   0xC90000,0x360000 (3.375 MB)
coredump, data, coredump, 0xFF0000,0x10000
```

### Launcher's Resident Layout (`custom_16Mb.csv`)

```
nvs,       data, nvs,       0x9000,   0x4000   (16 KB)
otadata,   data, ota,       0xD000,   0x2000   (8 KB)
phy_init,  data, phy,       0xF000,   0x1000
app0,      app,  test,      0x10000,  0x180000 (1.5 MB — Launcher's resident slot)
coredump,  data, coredump,  0x190000, 0x10000
```

### Under-Launcher Runtime Layout (dynamic, created by Launcher's installer)

When SigurdOS is installed via Launcher's merged-bin path, Launcher's partitioner creates a layout like:

```
nvs @0x9000     size 0x4000
otadata @0xD000 size 0x2000
phy_init @0xF000 size 0x1000
app0 (test)  @0x10000 size 0x180000   ← Launcher's resident slot
... Launcher-managed gap ...
SigurdOS app slot  @ <dynamic offset> size ≥ app_size
spiffs              @ <dynamic offset> size 0x100000 (1 MB for 16 MB boards)
coredump            @ <dynamic offset> size 0x10000
```

### Key Differences & Impact

| Aspect | Standalone | Under Launcher | Impact on SigurdOS |
|--------|-----------|----------------|-------------------|
| **NVS size** | 0x5000 (5 sectors) | 0x4000 (4 sectors) | NVS layout mismatch → `nvs_flash_init` erases + recovers → settings reset |
| **Otadata offset** | 0xE000 | 0xD000 | Used as detection signal (C3 already implemented) |
| **App slot subtype** | `ota_0` (0x10) | Dynamic (any OTA) | SigurdOS finds its app partition via ESP-IDF API — works |
| **App slots** | 2 (dual OTA) | 1+ (dynamic) | Self-OTA targets wrong slot → must be gated (C4 already implemented) |
| **SPIFFS** | 0xC90000, 3.375 MB | Dynamic, 1 MB default | SigurdOS finds SPIFFS by label/subtype — works at any offset. 1 MB is sufficient for identity+contacts+channels |
| **`phy_init`** | Not present in standalone | Present | No impact — phy_init is separate |

### Launcher Detection (C3)

SigurdOS uses a dual-signal detection mechanism (`src/hal/launcher_env.cpp`):

1. **Primary signal:** An app partition with subtype `test` (0x20) at offset 0x10000 — Launcher's resident slot, never present in `default_16MB.csv`
2. **Confirmatory signal:** Otadata partition at offset `0xD000` — Launcher's otadata location, vs 0xE000 in standalone

Both signals must be true. False positives are structurally impossible on the stock Arduino partition layout.

---

## 3. Boot Flow & Warm-Handoff Analysis

### SigurdOS Cold Boot (standalone)

1. `Serial.begin(115200)` — USB CDC
2. `board.begin()` — GPIO 10 HIGH, I2C begin(18,8), ADC init, trackball pins as INPUT_PULLUP
3. `sigurdos_display_init()` — LovyanGFX ST7789 init, LVGL init, draw buffer allocation, first splash frame
4. `SPIFFS.begin(true)` — mounts SPIFFS partition
5. `prefs_get()` — loads settings from NVS namespace `"sigurdos"`
6. `sigurdos_display_init_inputs()` — touch, keyboard, trackball init (deferred to after display)
7. GPS init (if enabled)
8. `sigurdos::mesh::init(spiffs_ok)` — shared SPI begin, LoRa SX1262 init (+ hard reset via GPIO 17)
9. Load persisted state (chats, contacts)
10. SD card init
11. Map init
12. "Ready" — auto-connect WiFi if credentials stored

### Launcher Install + Boot Flow

1. **Launcher is flashed first** — its custom bootloader + merged binary
2. **User installs SigurdOS** from SD/URL/WebUI — Launcher's partitioner:
   - Detects partition table at file offset 0x8000 in the merged binary
   - Extracts app entry (subtype `ota_0` → treat as app source)
   - Creates SPIFFS partition (1 MB default on 16 MB boards)
   - Dynamically allocates OTA app partition
   - Writes new partition table, writes otadata, **`ESP.restart()`**
3. **Launcher boots, shows boot screen**, waits 2-5 seconds
4. **Launcher chains to SigurdOS**: writes otadata → `ESP.restart()`
5. **SigurdOS boots** — now running under Launcher's bootloader + Launcher's partition table

### Key fact about how Launcher works

Launcher releases are merged binaries (`Launcher-lilygo-t-deck.bin` = bootloader @0x0 + table @0x8000 + app @0x10000). Launcher is built against a **custom framework package** that enables `CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y` (so it can rewrite the partition table and otadata at runtime). **The Launcher maintainer has stated a custom bootloader is required for the return-to-Launcher flow.** Power-on always boots Launcher; its boot screen waits ~2 s and then chains to the installed app.

**Consequence for SigurdOS:** when installed via Launcher, SigurdOS runs under *Launcher's bootloader and Launcher's runtime-generated partition table*. The bootloader, `boot_app0`, and partition table inside our merged binary are never flashed — they are only read by Launcher as a *manifest*.

### What Launcher Leaves Behind Before Handoff

From `boards/lilygo-t-deck/interface.cpp::_setup_gpio()`:

| Peripheral | State After Launcher | SigurdOS Expects (cold boot) | Risk |
|-----------|---------------------|------------------------------|------|
| **I2C bus** (18/8) | Initialized, running | Wire.begin(18,8) → fresh init | **LOW** — Wire.begin() reinitializes |
| **GPIO 10** | HIGH (peripherals powered) | HIGH | **NONE** — same state |
| **C3 Keyboard MCU** (0x55) | Running, in **ASCII key mode** (Launcher never touches mode commands) | Expects to probe + switch to raw mode via I2C cmd `0x03` | **MEDIUM-HIGH** — see below |
| **GT911 Touch** (0x5D) | Initialized via `TouchDrvGT911`, configured | Probes 0x5D first, falls back to 0x14; re-reads + re-writes config (186 bytes) | **LOW-MEDIUM** — SigurdOS's config rewrite should be idempotent |
| **ST7789 Display** | Initialized via `Ard_eSPI` (Arduino_GFX), rotation=1, 320x240, inversion=true | LovyanGFX full init including software reset (SWRESET 0x01) | **LOW** — soft reset recovers panel |
| **SX1262 LoRa** | **CS driven HIGH**, never initialized | Hard-reset via GPIO 17 during `mesh::init()` | **LOW** — full re-init |
| **SD Card** | Not mounted by Launcher | `SD.begin(CS=39, SPI, 4MHz)` | **LOW** — CMD0 resets card |
| **Trackball GPIOs** (1,2,3,15) | **ISR-attached!** (FALLING interrupt handlers) | `pinMode(x, INPUT_PULLUP)` — **does NOT detach Launcher's ISRs** | **MEDIUM** — see below |

### The Critical Warm-Handoff Problem

**C3 Keyboard MCU:** The keyboard is a separate ESP32-C3 that continues running across the `ESP.restart()`. Launcher leaves it in default **ASCII key mode** (single-byte reads — Launcher never sends the raw mode command `0x03`). SigurdOS's keyboard init (`sigurdos_keyboard_init`) probes 0x55, then sends:
1. `Wire.write(0x04)` — switch to key mode (redundant, already in key mode)
2. Reads brightness commands (0x01, 0x02)
3. `Wire.write(0x03)` — switch to **raw matrix mode**

The problem: the C3 MCU may not respond to the I2C probe immediately after the S3 reset because:
- The C3 was in the middle of a keyboard scan cycle
- GPIO 10 power rail might have glitched during S3 reset
- The C3 firmware may have stale I2C state

**SigurdOS already has warm-handoff hardening (C6):** bounded retry with 3×100ms delays + explicit mode reset. However, the keyboard init has a hard failure path: if the probe fails after all retries, `sigurdos_keyboard_init()` returns `false` and the keyboard is permanently unavailable.

**The user-reported breakage** ("keyboard doesn't work, many things break") has never been root-caused on physical hardware. This requires bench debugging with a logic analyzer on the I2C lines (pins 18/8) and GPIO 10 during the handoff.

**Trackball ISR collision:** Launcher's `interface.cpp` sets up `attachInterrupt(FALLING)` on GPIOs 1, 2, 3, 15. After `ESP.restart()`, the S3's CPU and interrupt controller are fully reset, so **ISR vectors are cleared** and Launcher's interrupt handlers no longer fire. However, the GPIO pin configuration (direction, pull-up/pull-down) set by Launcher's `pinMode()` calls may persist through a software-only reset, potentially conflicting with SigurdOS's own `pinMode(x, INPUT_PULLUP)` calls. This is a lower risk than previously assessed — `pinMode()` reconfigures the GPIO registers regardless of prior state.

---

## 4. Peripheral Driver Deep Dive

### Display — ST7789 (LovyanGFX vs Arduino_GFX)

| Aspect | SigurdOS (LovyanGFX) | Launcher (Ard_eSPI/Arduino_GFX) |
|--------|---------------------|--------------------------------|
| Library | LovyanGFX 1.2.21 | Arduino_GFX (wrapped in `Ard_eSPI`) |
| SPI host | SPI2_HOST | HWSPI (default SPI) |
| Init sequence | Panel_ST7789::setRotation(1) → SW reset → registers | Arduino_GFX constructor chain |
| Buffering | Full-frame PSRAM buffer (320×240×2) | Framebuffer varies |
| Brightness | LEDC ch0, 44.1 kHz | LEDC, freq varies |
| Rotation | 1 (landscape, native portrait) | 1 → `ROTATION=1` |

**Verdict:** Both use standard ST7789 init. SigurdOS's LovyanGFX driver does a software reset (SWRESET) which recovers the panel regardless of what Launcher wrote. This should work.

### Touch — GT911 (Raw I2C vs TouchDrvGT911)

| Aspect | SigurdOS (raw I2C) | Launcher (TouchDrvGT911) |
|--------|--------------------|--------------------------|
| Library | Manual I2C register read/write | `TouchDrvGT911` library |
| Address probe | 0x5D first, then 0x14 fallback | Hardcoded `GT911_SLAVE_ADDRESS_L` (0x5D) |
| Config handling | Reads 186-byte config, writes it back (idempotent) | Library handles config internally |
| Polling | INT pin + status register (0x814E) | Library's `getPoint()` |
| Swap XY | Yes (`TOUCH_SWAP_XY=true`) | Rotation-dependent (`touch.setSwapXY`) |
| Mirror | Yes (`TOUCH_MIRROR_Y=true`) | Rotation-dependent (`touch.setMirrorXY`) |

**Verdict:** SigurdOS's GT911 driver is robust — it re-reads the chip's config and writes it back on every boot. This is idempotent. The dual-address probe (0x5D → 0x14 fallback) is a proactive mitigation against a known GT911 quirk where the address flips under certain power sequences.

### Keyboard — ESP32-C3 I2C Slave

**This is the most fragile link.** Key differences:

| Aspect | SigurdOS | Launcher |
|--------|----------|----------|
| Protocol | Switches C3 to **raw matrix mode** (0x03) | Uses C3's default **ASCII key mode** (0x04) |
| Polling rate | 5 ms intervals | ~190 ms intervals (via main loop timing) |
| Key map | 5×7 raw matrix with Sym/Alt/Mic/Shift layers | Single ASCII byte from C3 |
| Special chars | Built-in extended character picker (ä, é, ñ, etc.) | C3's own ASCII mapping only |
| Init retries | 3 retries with 100ms delays | 500ms initial delay, no retries |

**What could break:**
1. **Timing:** The C3 may still be processing a scan cycle when SigurdOS's init probe arrives. SigurdOS's 3×100ms retry helps but the C3's I2C response time isn't bounded.
2. **Mode transition:** Switching from key mode to raw mode requires the C3 to flush its key buffer.
3. **Backlight:** Both write to the same LEDC channel — last write wins.

### Trackball — Polling vs ISR

| Aspect | SigurdOS | Launcher |
|--------|----------|----------|
| Method | Polling (`digitalRead` in `sigurdos_trackball_scan()`) | ISRs (`attachInterrupt(FALLING)`) |
| Debounce | 20ms click, 150ms direction, 80ms left, 250ms settle | Axis accumulation with 250ms expiry |
| Event model | Queue-based (8 entries) | Direct volatile globals (`PrevPress`, `NextPress`) |

**The ISR collision** is a real concern (see Section 3). SigurdOS's `sigurdos_trackball_init()` should call `detachInterrupt()` on each trackball pin before setting pinMode.

### SD Card

Both share the same SPI bus. Launcher mounts SD with `SD.begin()` using `SDCARD_CS=39`. SigurdOS mounts SD in `setup()` after mesh init using the shared SPI singleton. The SD card responds to CMD0 (GO_IDLE_STATE) which resets its state machine. Should work.

SigurdOS's SD init calls `sigurdos_shared_spi_begin()` which deliberately re-calls `SPI.begin()` on every invocation — this resets SPI2 peripheral hardware to clear stale LoRa state.

### LoRa — SX1262

Launcher **never initializes** the SX1262. It only drives CS (GPIO 9) HIGH to prevent bus contention. SigurdOS's `mesh::init()` hard-resets the SX1262 via GPIO 17 (RST pin) and does a full RadioLib init. This should work identically to cold boot.

---

## 5. T-Deck Variant Differences

This section explains **why some users have different experiences.**

### T-Deck (standard) — SigurdOS's Native Target

- **Touch:** GT911 @ 0x5D (INT on GPIO 16)
- **Keyboard:** ESP32-C3 I2C @ 0x55, 7×5 matrix
- **Display:** ST7789 2.8" IPS LCD, 320×240 portrait native
- **Trackball:** 5-direction on GPIOs 0,1,2,3,15
- **GPS:** None (external only)
- **SigurdOS support:** ✅ Native target

### T-Deck Plus

- Same as T-Deck plus built-in **L76K GPS** on Serial1 (43/44)
- Same touch, keyboard, display, trackball
- Launcher flag: `T_DECK_PLUS=1`
- **SigurdOS impact:** SigurdOS already has GPS HAL (`gps.cpp`). If compiled for the Plus, GPS works. If compiled for standard, GPS pins are unused — no harm. The Plus would actually work with standard SigurdOS builds; GPS just won't be used.
- **Launcher impact:** Plus uses the same OTA tag `t-deck` for catalog filtering

### T-Deck Pro — FULLY INCOMPATIBLE

This is the key difference between user experiences. The Pro has **completely different hardware**:

| Component | Standard T-Deck | T-Deck Pro | SigurdOS Support |
|-----------|----------------|------------|------------------|
| Display | ST7789 LCD (SPI) | E-Paper ED047TC1 | ❌ Wrong driver |
| Touch | GT911 @ 0x5D | CST328/CST816 @ 0x1A | ❌ Wrong I2C addr + protocol |
| Keyboard | ESP32-C3 @ 0x55 (7×5) | TCA8418 @ 0x34 (4×10) | ❌ Wrong I2C addr + protocol |
| Trackball | GPIOs 0,1,2,3,15 | None (buttons only) | ❌ No trackball |
| PMIC | TP4054 | BQ25896 + BQ27220 | ❌ Wrong battery driver |
| GPIO Expander | None | XL9555 @ 0x20 | ❌ Missing driver |
| GPS | External only | Built-in L76K | N/A |
| Cellular | None | A7682E 4G LTE | ❌ Missing driver |
| Motor | None | DRV2605 haptic | N/A |
| LoRa | SX1262 | SX1262 | ✅ Same radio |

**Users with T-Deck Pro will have a completely non-functional experience.** The firmware won't display anything (wrong driver for e-paper), won't read the keyboard (different controller), and won't get touch input. This is expected — SigurdOS does not target the Pro.

If Launcher integration succeeds for the standard T-Deck, a separate Pro build target would be needed for Pro support.

### Flash Mode Variations (QIO vs DIO)

Even among standard T-Decks, some units require `flash_mode = dio` while the board JSON specifies `qio`. SigurdOS already forces DIO: `board_build.flash_mode = dio` in `platformio.ini`. Launcher's T-Deck builds default to DIO (their custom framework handles this). This is not a compatibility issue between firmwares — but can cause boot loops on affected hardware regardless of Launcher.

### PSRAM Variations

Some early T-Deck units shipped with 8 MB flash and **no PSRAM**. SigurdOS requires PSRAM (`BOARD_HAS_PSRAM=1` in build flags) — it would fail to boot on those units. This is a hardware requirement, not a Launcher issue.

---

## 6. Things That Will Work

These subsystems have been verified to be compatible by source inspection:

1. **Pin compatibility:** All pins match perfectly — zero signal conflicts
2. **ESP32-S3 target:** Same chip, same 16 MB flash, same 8 MB OPI PSRAM
3. **Merged binary parsing:** Launcher detects partition table at 0x8000 in `firmware-merged.bin` and can extract the app + create SPIFFS
4. **Display re-init:** LovyanGFX software reset recovers ST7789 regardless of Launcher's prior panel config
5. **LoRa radio:** Full hardware reset via GPIO 17 + RadioLib init works from any state
6. **SD card:** CMD0 reset recovers the card regardless of prior SPI state
7. **GPS:** Serial1 re-init works fine (unused in standard T-Deck builds)
8. **BLE companion:** Full BLE stack init works regardless of Launcher
9. **WiFi:** Full WiFi stack init works regardless of Launcher
10. **SPIFFS persistence (merged-bin install):** Launcher creates a fresh 1 MB SPIFFS partition — identity+contacts persist across reboots
11. **Battery reading:** ADC on GPIO 4, same voltage divider, same efuse-calibrated `analogReadMilliVolts` approach
12. **Buzzer:** GPIO 46, Launcher doesn't touch it
13. **Self-OTA gating:** Already implemented (C4) — WiFi and GitHub OTA are refused with explanation when running under Launcher
14. **Runtime Launcher detection:** Already implemented (C3) — dual-signal probe correctly identifies Launcher vs standalone
15. **Boot-time persistence diagnostic:** Already implemented (C5) — clear warning when SPIFFS mount fails under Launcher (app-only install)

---

## 7. Things That Might Get In The Way

### HIGH RISK — Will Break (Must Fix)

#### 1. App-only install via `firmware.bin`

Launcher's app-only path creates **no SPIFFS partition**. SigurdOS boots but:
- Generates a **new mesh identity (keypair) on every boot** — other nodes see a "new" node each time, DMs to the old key break
- Contacts, channels, and message history never persist

**Fix:** Document that Launcher users must use the merged image (`SigurdOS-tdeck-launcher.bin`). This is already documented in `firmware/README.md` and `docs/LAUNCHER.md`.

#### 2. Self-OTA corruption of co-installed apps

Arduino `Update` targets `esp_ota_get_next_update_partition()`. Under Launcher with multiple apps, this would overwrite **another firmware's partition** and desync Launcher's app registry.

**Fix:** Already gated (C4). Both WiFi AP OTA and GitHub OTA refuse to start when Launcher is detected, with the message "Update SigurdOS through Launcher instead."

#### 3. Trackball GPIO pin state from Launcher

Launcher's `interface.cpp` calls `pinMode()` and `attachInterrupt(FALLING)` on GPIOs 1, 2, 3, 15. After `ESP.restart()`, the interrupt controller is reset and ISR vectors are cleared — Launcher's interrupt handlers no longer fire. However, the GPIO pin configuration (direction, pull-up/down) set by Launcher may persist through a software-only reset. When SigurdOS calls `pinMode(x, INPUT_PULLUP)`, it reconfigures the GPIO regardless of prior state, so this is effectively harmless.

No code change is required. The original `detachInterrupt()` concern was based on an incorrect assumption that ISR vectors survive `ESP.restart()` on ESP32-S3. `pinMode()` fully overrides any prior GPIO configuration.

### MEDIUM RISK — Could Break (Needs Verification)

#### 4. C3 Keyboard init timing

The C3 may not respond to the I2C probe immediately after warm handoff. SigurdOS's 3×100ms retry helps but the failure path is hard (keyboard permanently dead if probe fails after all retries).

**Root cause unverified** — requires bench testing with logic analyzer on I2C lines (pins 18/8) and GPIO 10 during the Launcher→SigurdOS handoff.

#### 5. NVS geometry mismatch on mode switch

Switching from standalone → Launcher (or vice versa) causes NVS to reset because the partition sizes differ (0x5000 vs 0x4000). Result:
- Settings lost (including radio params, configured flag, theme, brightness)
- Onboarding re-runs
- Radio TX stays gated until reconfigured (`configured == false`)

This is a **one-time event** — not a recurring problem. Documented in `docs/LAUNCHER.md` as RC4.

#### 6. Keyboard left in wrong mode

Launcher uses ASCII key mode; SigurdOS switches to raw matrix mode. If the C3 firmware has bugs in the mode-switch path, raw mode might not work. SigurdOS's explicit mode reset (cmd `0x04` then `0x03`) should handle this — confirmed by other firmwares that use the same approach.

### LOW RISK — Unlikely to Break

7. **GT911 address flip:** Known GT911 quirk where address flips to 0x14 under certain power sequences. SigurdOS already probes both addresses.

8. **SPI bus contention:** Three devices share SPI2_HOST (display, LoRa, SD). After warm handoff, Launcher may have left the bus in an inconsistent state. SigurdOS's `sigurdos_shared_spi_begin()` deliberately re-calls `SPI.begin()` to reset the peripheral.

9. **Display backlight:** Both firmwares use LEDC ch0 on GPIO 42. Last write wins.

---

## 8. T-Deck Pro / Hardware Variants

### Why Some Users Have Different Experiences

The T-Deck product line has three distinct hardware variants:

| Variant | Touch | Keyboard | Display | SigurdOS |
|---------|-------|----------|---------|----------|
| **T-Deck (standard)** | GT911 @ 0x5D | ESP32-C3 @ 0x55 | ST7789 LCD | ✅ Target |
| **T-Deck Plus** | GT911 @ 0x5D | ESP32-C3 @ 0x55 | ST7789 LCD | ✅ Works |
| **T-Deck Pro** | CST328/CST816 @ 0x1A | TCA8418 @ 0x34 | E-Paper ED047TC1 | ❌ Incompatible |

**If a user reports SigurdOS not working and they have a T-Deck Pro, that's expected.** The Pro has completely different peripherals — the firmware's display driver expects an ST7789 LCD, the keyboard driver expects an ESP32-C3 at I2C 0x55, and the touch driver expects a GT911 at 0x5D. None of these exist on the Pro.

### Launcher's T-Deck Pro Support

Launcher has full support for the T-Deck Pro via a separate build environment (`lilygo-t-deck-pro`) with a different OTA tag (`t-deck-pro`). The Pro interface code handles the e-paper display, TCA8418 keyboard, CST328 touch, XL9555 GPIO expander, BQ25896 PMIC, BQ27220 fuel gauge, and even an A7682E 4G LTE modem.

If SigurdOS ever targets the Pro, it would require a separate build environment with completely different HAL drivers.

---

## 9. Required Changes Status

Updated 2026-06-17. Cross-references to the [Launcher Roadmap](LAUNCHER_ROADMAP.md).

| # | Change | Status | Code Impact | Roadmap ID |
|---|--------|--------|-------------|------------|
| 1 | Detach Launcher's trackball ISRs in `sigurdos_trackball_init()` | **NOT NEEDED** | ISR vectors are cleared by `ESP.restart()` on ESP32-S3; `pinMode()` overrides any lingering GPIO config. The original `detachInterrupt()` concern was based on an incorrect assumption — no code change required. | — (retracted) |
| 2 | Publish Launcher-specific artifact name | ✅ Done | CI copies `firmware-merged.bin` → `SigurdOS-tdeck-launcher.bin` | C1 |
| 3 | Document Launcher install path | ✅ Done | `firmware/README.md` | C2 |
| 4 | Runtime Launcher detection | ✅ Done | `src/hal/launcher_env.cpp` — dual-signal probe | C3 |
| 5 | Gate self-OTA under Launcher | ✅ Done | WiFi/GitHub OTA refuse when detected | C4 |
| 6 | Boot-time persistence diagnostic | ✅ Done | App-only install warning in `src/main.cpp` | C5 |
| 7 | Warm-handoff keyboard hardening | ✅ Done | 3×100ms retry + explicit mode reset in `src/hal/keyboard.cpp` | C6 |
| 8 | Migration notes | ✅ Done | Docs cover NVS/SPIFFS reset expectations | C7 |
| 9 | Shrink-audit of app image | ✅ Done | Confirmed no shrink work needed | O3 |
| 10 | Bench validation of RC3 (keyboard + trackball) | **NOT DONE** | Requires physical hardware + logic analyzer | — |
| 11 | LauncherHub catalog listing | **NOT DONE** | External — needs maintainer submission | O1 |
| 12 | "Reboot to Launcher" Settings entry | **NOT DONE** | Blocked — needs otadata write feasibility study | O2 |

### The ONLY Remaining Work

**Bench validation of keyboard warm-handoff (RC3).** Everything else is either already done, requires physical hardware testing, or (like the ISR concern) is retired as incorrect. No code changes are needed for Launcher compatibility beyond what is already merged.

---

## 10. Bottom Line

SigurdOS is **architecturally compatible** with bmorcelli/Launcher — the pin maps are identical, the partition API usage is correct, and the peripheral init sequences can recover from a warm handoff in most cases. The firmware already has most of the necessary compatibility infrastructure in place (detection, self-OTA gating, keyboard hardening, documentation).

**The two things that would actually prevent a working experience today:**

1. ~~Trackball ISR collision~~ — **retired.** ISR vectors are cleared by `ESP.restart()` on ESP32-S3. `pinMode()` overrides any lingering GPIO state. No code change needed.
2. **Warm-handoff keyboard timing** (may already be fixed by C6 hardening; needs bench testing on physical hardware)

**The biggest user-facing gap** is that users who install the obvious `firmware.bin` (app-only) file get a silently-broken experience with no persistence. This is addressed by documentation and the clear naming of the Launcher artifact (`SigurdOS-tdeck-launcher.bin`) but remains a UX trap.

**T-Deck Pro users** should be directed away from SigurdOS — the firmware is fundamentally incompatible with the Pro's e-paper display, TCA8418 keyboard, and CST328 touch controller. A separate Pro build target would be needed.

---

## Appendix A: Key Source Files

### SigurdOS-tdeck
- `platformio.ini` — build config, partition table (`default_16MB.csv`), pin defines
- `src/main.cpp` — boot sequence, deferred input init, boot status splash
- `src/hal/tdeck_pins.h` — canonical pin definitions
- `src/hal/tdeck_board.h` — TDeckBoard class (peripheral power, I2C, battery, sleep)
- `src/hal/display.cpp` — LovyanGFX ST7789 init, LVGL flush callback, auto-off timer
- `src/hal/touch.cpp` — GT911 raw I2C driver with dual-address probe
- `src/hal/keyboard.cpp` — ESP32-C3 keyboard driver with raw matrix mode
- `src/hal/trackball.cpp` — 5-direction trackball polling with debounce
- `src/hal/sdcard.cpp` — SD card via shared SPI singleton
- `src/hal/spi_shared.cpp` — Shared SPI bus singleton (SPI2_HOST / FSPI)
- `src/hal/battery.cpp` — Battery ADC with efuse calibration
- `src/hal/launcher_env.cpp` — Launcher detection (dual-signal probe)
- `docs/LAUNCHER.md` — Launcher compatibility detection documentation
- `docs/LAUNCHER_ROADMAP.md` — Full compatibility roadmap and implementation plan

### bmorcelli/Launcher
- `boards/lilygo-t-deck/platformio.ini` — T-Deck build flags and pin definitions
- `boards/lilygo-t-deck/interface.cpp` — GPIO setup, trackball ISRs, keyboard input handler
- `boards/pinouts/lilygo-t-deck.h` — T-Deck Arduino pin aliases
- `boards/lilygo-t-deck-pro/platformio.ini` — T-Deck Pro build flags
- `boards/lilygo-t-deck-pro/interface.cpp` — Pro GPIO setup, TCA8418 keyboard, e-paper display
- `support_files/custom_16Mb.csv` — Launcher's resident partition table
- `include/pre_compiler.h` — Compile-time defaults (SPIFFS thresholds, display config)
- `src/main.cpp` — Launcher boot flow, app chain-boot
- `src/display.cpp` — Display init via Arduino_GFX

---

## Appendix B: Terminology

| Term | Definition |
|------|-----------|
| **Launcher** | bmorcelli/Launcher — resident "app store" firmware for ESP32 devices |
| **Merged binary** | A .bin file containing bootloader + partition table + app, flashable at offset 0x0 |
| **App-only binary** | A .bin file containing only the ESP32 app image, flashable at the app partition offset |
| **Warm handoff** | Launcher's process of chain-booting an installed app via `otadata` write + `ESP.restart()` |
| **otadata** | ESP-IDF partition (subtype `data/ota`) that stores which OTA slot to boot |
| **GPIO 10** | Peripheral power enable pin — HIGH = display/keyboard/touch/LoRa/SD powered |
| **I2C bus** | Pins 18 (SDA) and 8 (SCL) — shared by GT911 touch and ESP32-C3 keyboard |
| **SPI bus** | Pins 40 (SCLK), 41 (MOSI), 38 (MISO) — shared by ST7789 display, SX1262 LoRa, and SD card |
