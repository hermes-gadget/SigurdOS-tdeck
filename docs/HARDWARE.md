# SigurdOS T-Deck — Hardware Reference

> Complete documentation of all T-Deck peripherals, pin assignments, interfaces,
> and configuration details for the SigurdOS T-Deck firmware.

**Board:** LilyGo T-Deck  
**MCU:** ESP32-S3 (240 MHz, 16 MB flash, 8 MB PSRAM)  
**Source:** `src/hal/tdeck_pins.h` — canonical pin definitions  
**Firmware Version:** `SIGURDOS_VERSION` (`src/hal/tdeck_pins.h`)

---

## Table of Contents

- [1. Pin Map (Summary)](#1-pin-map-summary)
- [2. Shared SPI Bus](#2-shared-spi-bus)
- [3. Shared I2C Bus](#3-shared-i2c-bus)
- [4. Display — ST7789 (LovyanGFX)](#4-display--st7789-lovyangfx)
- [5. Touch — GT911 Capacitive](#5-touch--gt911-capacitive)
- [6. Keyboard — ESP32-C3 I2C Slave](#6-keyboard--esp32-c3-i2c-slave)
- [7. Trackball — 5‑Direction GPIO](#7-trackball--5direction-gpio)
- [8. Battery — ADC Monitor](#8-battery--adc-monitor)
- [9. GPS — UART NMEA](#9-gps--uart-nmea)
- [10. SD Card — SPI + FATFS](#10-sd-card--spi--fatfs)
- [11. LoRa — SX1262 (RadioLib)](#11-lora--sx1262-radiolib)
- [12. Buzzer](#12-buzzer)
- [13. Peripheral Power](#13-peripheral-power)
- [14. Deep Sleep & Wake](#14-deep-sleep--wake)

---

## 1. Pin Map (Summary)

| Pin | Function            | Interface | Notes                              |
|-----|---------------------|-----------|-------------------------------------|
| 0   | Trackball Click     | GPIO      | Center press / BOOT button          |
| 1   | Trackball Left      | GPIO      | Directional input                   |
| 2   | Trackball Right     | GPIO      | Directional input                   |
| 3   | Trackball Up        | GPIO      | Directional input                   |
| 4   | Battery ADC         | ADC1      | Voltage divider input               |
| 8   | Touch SCL           | I2C       | Shared I2C bus                      |
| 9   | LoRa NSS            | SPI       | Chip select                         |
| 10  | Peripheral Power    | GPIO      | HIGH = peripherals powered          |
| 11  | TFT DC              | SPI       | Display data/command                |
| 12  | TFT CS              | SPI       | Display chip select                 |
| 13  | LoRa BUSY           | GPIO      | Radio busy indicator                |
| 15  | Trackball Down      | GPIO      | Directional input                   |
| 16  | Touch INT           | GPIO      | Interrupt (active low)              |
| 17  | LoRa RST            | GPIO      | Radio reset (active low)            |
| 18  | Touch SDA           | I2C       | Shared I2C bus                      |
| 38  | SPI MISO            | SPI       | Shared bus (LoRa + SD)              |
| 39  | SD Card CS          | SPI       | microSD chip select                 |
| 40  | SPI SCLK            | SPI       | Shared bus (all SPI peripherals)    |
| 41  | SPI MOSI            | SPI       | Shared bus (LoRa + SD)              |
| 42  | TFT Backlight       | PWM       | Backlight brightness control        |
| 43  | GPS TX              | UART      | Serial1 TX                          |
| 44  | GPS RX              | UART      | Serial1 RX                          |
| 45  | LoRa DIO1           | GPIO      | Radio interrupt while awake (not RTC-capable) |
| 46  | Buzzer              | GPIO      | Active-low buzzer output            |

---

## 2. Shared SPI Bus

The Display, LoRa radio, and SD card share a **single SPI bus** on `SPI2_HOST`
(FSPI) with separate chip select lines.

| Signal  | Pin | Shared By                     |
|---------|-----|-------------------------------|
| SCK     | 40  | Display, LoRa, SD             |
| MOSI    | 41  | Display (SDA), LoRa, SD       |
| MISO    | 38  | LoRa, SD (display is write-only) |

**CS lines (separate):**

| Peripheral | CS Pin | Driver              |
|------------|--------|----------------------|
| Display    | 12     | LovyanGFX            |
| LoRa       | 9      | RadioLib (SX1262)    |
| SD Card    | 39     | Arduino SD / FATFS   |

> **Note:** The bus has no single `SPI.begin()` call — each driver initialises
> its own transaction on `SPI2_HOST` with separate `SPIClass` instances.
> The SD card uses `FSPI`, the display uses `SPI2_HOST` via LovyanGFX, and
> the LoRa radio uses `SPI2_HOST` via RadioLib's `Module` class.

---

## 3. Shared I2C Bus

The GT911 touch controller and keyboard MCU share a single I2C bus on pins 18
(SDA) and 8 (SCL).

| Device     | Address | Speed       | Driver     |
|------------|---------|-------------|------------|
| Touch      | 0x5D / 0x14 | 400 kHz | GT911      |
| Keyboard   | 0x55    | 400 kHz     | ESP32-C3   |

`TDeckBoard::begin()` recovers the lines before `Wire.begin()`, then owns shared
bus setup at 400 kHz with a 20 ms transaction timeout. Recovery releases SCL as
open drain for at most nine clocks and emits a STOP only after SDA is released;
it never bit-bangs pins while the Wire controller is active. Startup is
process-wide idempotent because the application and mesh layer each own a
`TDeckBoard`; the second call only reasserts the clock and timeout.

Discovery is deliberately limited to keyboard `0x55` and GT911 `0x5D`/`0x14`.
There is no full-address scan. Touch and keyboard cache their first completed
initialization result, avoiding repeated probes after a confirmed failure.

---

## 4. Display — ST7789 (LovyanGFX)

| Property           | Value                     |
|--------------------|---------------------------|
| Driver             | LovyanGFX v1 (`LGFX_USE_V1`) |
| Panel              | `Panel_ST7789`            |
| Bus                | `Bus_SPI` on `SPI2_HOST`  |
| SPI Speed (write)  | 40 MHz                    |
| SPI Speed (read)   | 16 MHz                    |
| SPI Mode           | 0                         |
| DMA Channel        | `SPI_DMA_CH_AUTO`         |
| 3-Wire SPI         | No (MISO not connected)   |
| Native Resolution  | 240 × 320 (portrait)      |
| Display Resolution | **320 × 240** (landscape) |
| Rotation           | **1** (90° CW)            |
| Colour Depth       | RGB565 (16-bit)           |
| Invert             | Yes (`cfg.invert = true`) |
| RGB Order          | Default                   |

### Pins

| Signal     | Pin | Def              |
|------------|-----|-------------------|
| TFT_CS     | 12  | `PIN_TFT_CS`      |
| TFT_DC     | 11  | `PIN_TFT_DC`      |
| TFT_RST    | —   | `PIN_TFT_RST = -1` (not connected) |
| TFT_BL     | 42  | `PIN_TFT_BL` (PWM backlight) |
| TFT_SCL    | 40  | Shared SPI SCK    |
| TFT_SDA    | 41  | Shared SPI MOSI   |

### Backlight (PWM)

| Parameter    | Value      |
|--------------|------------|
| BL Pin       | 42         |
| PWM Freq     | 44100 Hz   |
| PWM Channel  | 0          |
| Invert       | No         |
| Default      | 255 (full) |

### Draw Buffer

- **Preferred:** Full-screen PSRAM buffer — `320 × 240 × 2 = 153,600 bytes`
  allocated via `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`.
  Render mode: `LV_DISPLAY_RENDER_MODE_FULL` (single flush, no tear lines).
- **Fallback:** Partial DRAM buffer — `320 × 80` lines (51,200 bytes).
  Render mode: `LV_DISPLAY_RENDER_MODE_PARTIAL`.

### Auto-Off (Power Saving)

- Timeout: **30 seconds** (`AUTO_OFF_MS = 30000`)
- On timeout: backlight → 0, keyboard backlight → 0
- Wake triggers: touch press, keyboard key, trackball event
- Wake sequence: restore backlight, keyboard backlight, re-assert rotation(1),
  invalidate active screen
- **Disabled** in `SIGURDOS_DEBUG_DISPLAY` builds (screen must stay on for
  observation).

### Pixel Format

Each pixel is 2 bytes in RGB565 format:

```
Bit   15 14 10  9  5  4  0
      ┌──┬─────┬─────┬─────┐
      │ R│  G  │  B  │     │
      └──┴─────┴─────┴─────┘
```

---

## 5. Touch — GT911 Capacitive

| Property           | Value                    |
|--------------------|--------------------------|
| Controller         | GT911                    |
| Interface          | I2C (shared bus)         |
| I2C Address        | **0x5D** (alternate 0x14) |
| Bus Speed          | **400 kHz**              |
| INT Pin            | 16 (active low)          |
| RST Pin            | — (not connected)        |
| Max Touch Points   | 5 (driver uses first valid only) |
| Poll Interval      | 10 ms                    |
| Native Resolution  | 240 × 320 (portrait)     |

### Coordinate Transforms

The GT911 sensor is mounted in portrait orientation (240 × 320 native) relative
to the landscape display (320 × 240). The driver applies these transforms:

| Transform       | Value  | Effect                                 |
|-----------------|--------|----------------------------------------|
| SWAP_XY         | true   | Swap axes (portrait → landscape)       |
| MIRROR_X        | false  | No horizontal flip                     |
| MIRROR_Y        | true   | Invert Y axis for correct up/down      |

The transform pipeline in `touch.cpp`:

```
raw_x, raw_y  →  swap XY  →  scale to 320×240  →  mirror Y  →  clamp
```

### Init Sequence

1. Reassert the shared 400 kHz clock and 20 ms timeout
2. Configure INT pin as `INPUT_PULLUP`
3. Hardware reset via INT: LOW (1 ms) → HIGH (10 ms) → INPUT_PULLUP
4. Probe only the two valid GT911 addresses (0x5D, then 0x14)
5. Read config (186 bytes) from register `0x8047` and write it back
6. Clear status register `0x814E`

### Data Read

- Polled every 10 ms in `sigurdos_touch_loop()`
- INT pin goes LOW when data is ready
- Read 40 bytes (5 points × 8 bytes each) from status register + 1
- Acknowledge by writing 0 to status register
- Single-touch only: first valid point is used

---

## 6. Keyboard — ESP32-C3 I2C Slave

| Property           | Value                         |
|--------------------|-------------------------------|
| MCU (Keyboard)     | ESP32-C3 (dedicated)          |
| Main MCU           | ESP32-S3 (I2C master)         |
| Interface          | I2C (shared bus)              |
| I2C Address        | **0x55**                      |
| Bus Speed          | **400 kHz** (shared with touch) |
| Protocol           | LilyGo T-Deck Keyboard_ESP32C3 (MIT) |
| Operating Mode     | **Key mode** (pre-decoded ASCII) |
| Raw mode           | 20 ms modifier-only sampler   |
| Poll Interval      | 5 ms                          |
| Backlight Default  | 127 (mid-brightness)          |
| Matrix             | 5 columns × 7 rows            |

### I2C Write Commands (Master → Slave)

| Command Byte | Payload  | Description                        |
|--------------|----------|------------------------------------|
| `0x01`       | `<duty>` | Set backlight brightness (0–255)   |
| `0x02`       | `<duty>` | Set default brightness for Alt+B   |
| `0x03`       | —        | Switch to **raw mode** (bitmask)   |
| `0x04`       | —        | Switch to **key mode** (ASCII)     |

### I2C Read (Master ← Slave)

- Key mode (primary): `Wire.requestFrom(0x55, 1)` returns one byte:

| Value          | Meaning                        |
|----------------|--------------------------------|
| `0x00`         | No key pressed                 |
| `0x08`         | Backspace                      |
| `0x0D`         | Enter                          |
| `0x09`         | Tab                            |
| `0x0C`         | Channel-menu shortcut event    |
| `0x20`–`0x7E`  | ASCII printable character      |

- Raw mode (compatibility sample): `Wire.requestFrom(0x55, 5)` returns one
  7-bit row mask per column. The host enters this mode briefly after each ASCII
  byte and every 20 ms for modifier-only taps, then immediately restores key
  mode.

### Key Matrix (5 × 7)

```
     Col0     Col1     Col2     Col3     Col4
Row0   q        e        r        u        o
Row1   w        s        g        h        l
Row2   sym      d        t        y        i
Row3   a        p        RShift   Enter    Bksp
Row4   ALT      x        v        b        $
Row5   SPC      z        c        n        m
Row6   Mic      LShift   f        j        k
```

### Host Compatibility Key Layers

- Normal characters, Shift, and held `Sym` are decoded by the keyboard MCU,
  so physical matrix differences between T-Deck models stay inside the C3.
- Raw samples preserve the host-only layers without decoding ordinary keys
  from the model-specific matrix.
- Tapping `Sym` arms it for one key; Shift+Sym output is repaired for the
  published C3 firmware's ASCII subtraction behavior.
- `Alt` opens an on-screen character picker for the pressed base key; tapping
  `Alt` arms the picker for one key.
- `Mic` is a fast extended-character alias for common accented characters.
- `Alt+Space` emits the channel-menu shortcut event (`0x0C`).
- `Alt+B` remains handled by the keyboard MCU for backlight toggling.
- Each key-mode byte is paired with a raw modifier sample, preventing a chord
  from producing both its base character and transformed character.

### International Layouts

- The active physical-key layout is stored in NVS as `kbd_layout` and restored
  when input initializes.
- Double-tapping Space in the same text field within 250 ms removes the first
  space, cycles to the next layout, and briefly shows its two-letter code.
- Available layouts, in cycle order: EN, BG, RU, UK, SR, EL, AR, FR, NL, DE,
  ES, IT.
- Mappings are inserted as complete UTF-8 strings, including multi-codepoint
  entries such as Arabic lam-alef and Dutch `ij`.
- The LVGL font fallback includes Greek, Cyrillic, Arabic, and contextual Arabic
  presentation forms. Bidi ordering and Arabic shaping are enabled globally.

### Backlight Control

| API Call                                          | Effect                          |
|----------------------------------------------------|---------------------------------|
| `sigurdos_keyboard_set_brightness(duty)` (0–255)     | Set immediate brightness        |
| `sigurdos_keyboard_set_default_brightness(duty)`     | Set brightness for Alt+B toggle |

- Minimum default brightness: **30** (below this yields no light on toggle)
- Default stored in NVS (`NodePrefs.kbd_backlight`, initial value 127)
- Backlight turns off with display auto-off; restored on wake

### Init Sequence

1. Reassert the shared 400 kHz clock and 20 ms timeout
2. Probe only address 0x55, with up to eight 100 ms-spaced cold-boot attempts
3. Request 1 byte after selecting key mode to confirm the C3 is ready
4. Send `CMD_BRIGHTNESS` (0x01) with stored value
5. Send `CMD_DEFAULT_BRIGHTNESS` (0x02) with min(30) clamping
6. Send `CMD_MODE_KEY` (0x04) and keep it as the primary operating mode
7. During polling, use bounded `CMD_MODE_RAW` (0x03) samples for Alt/Mic/Sym,
   then restore `CMD_MODE_KEY` before the next ASCII read

### Known Limitations

- Ctrl state is not available from the current keyboard matrix.
- C3 firmware without raw-mode commands remains usable in key-only mode, but
  cannot expose the host-side Alt/Mic/Sym one-shot extensions.

---

## 7. Trackball — 5‑Direction GPIO

| Button  | Pin | GPIO | Direction | Default State |
|---------|-----|------|-----------|---------------|
| UP      | 3   | 3    | Yes       | Pulled HIGH   |
| DOWN    | 15  | 15   | Yes       | Pulled HIGH   |
| LEFT    | 1   | 1    | Yes       | Pulled HIGH   |
| RIGHT   | 2   | 2    | Yes       | Pulled HIGH   |
| CLICK   | 0   | 0    | No        | Pulled HIGH / BOOT |

### Behaviour

- All pins configured as `INPUT_PULLUP` (external pull-up on GPIO 0 for BOOT)
- **Direction buttons**: fire one event per physical detent on **falling edge** (HIGH → LOW)
- **Click button**: fires on stable LOW after 20 ms debounce
- **Dead time**: 150 ms between directional events (prevents double-triggering)
- **Settle period**: 250 ms after init (ignores spurious edges during power-up)

### Event Queue

| Parameter          | Value |
|--------------------|-------|
| Queue Size         | 8     |
| Storage            | Ring buffer (head/tail) |
| Drain              | `sigurdos_trackball_next_event()` in display loop |
| Fallback Queue     | 8-entry fallback in `display.cpp` when LVGL can't process |

### LVGL Integration

- Registered as `LV_INDEV_TYPE_ENCODER`
- Up/Left → `enc_diff = -1`
- Down/Right → `enc_diff = +1`
- Click → `LV_INDEV_STATE_PRESSED`
- Trackball and keyboard share the same `lv_group_t` for focus navigation

---

## 8. Battery — ADC Monitor

| Property           | Value                         |
|--------------------|-------------------------------|
| ADC Pin            | **GPIO 4** (`PIN_BAT_ADC`)    |
| ADC Resolution     | 12-bit (0–4095)               |
| Voltage Divider    | **2 × 3.3 V × 1000** multiplier (`BAT_ADC_MULT = 6600.0f`) |
| Sampling           | 8-sample average              |
| Min Voltage        | 3000 mV (`BAT_MIN_MV`)        |
| Max Voltage        | 4200 mV (`BAT_MAX_MV`)        |
| Auto-Shutdown      | **3200 mV** (3.2 V) — `AUTO_SHUTDOWN_MV` in `TDeckBoard` |

### Voltage Calculation

The driver uses `analogReadMilliVolts()` (efuse-calibrated) and compensates
for the 2x voltage divider:

```c
voltage_mv = analogReadMilliVolts(PIN_BAT_ADC) * 2;  // efuse-calibrated, 2x divider
```

### Battery Percentage

```c
if (mv <= 3000) return 0;
if (mv >= 4200) return 100;
pct = ((mv - 3000) * 100) / (4200 - 3000);
```

### Auto-Shutdown

- Checked every 30 seconds in `main.cpp loop()`
- If `mv < 3200`: enters deep sleep indefinitely (`board.sleep(0)`)
- Shutdown powers off peripherals via `PIN_PERIPH_PWR` LOW
- No dedicated charge-detect pin — `sigurdos_battery_charging()` always returns
  `false`

---

## 9. GPS — UART NMEA

| Property           | Value                        |
|--------------------|------------------------------|
| Module             | L76K GNSS (multi-constellation) |
| Interface          | **UART (Serial1)**           |
| TX Pin (GPS to ESP) | **44** (`PIN_GPS_RX`)       |
| RX Pin (ESP to GPS) | **43** (`PIN_GPS_TX`)       |
| Baud Rate          | Primary **9600**, fallback **38400** |
| Data Format        | 8N1                          |
| NMEA Sentences     | `$GxGGA`, `$GxRMC`, `$GxGSV`, `$GxGSA` |
| Checksum           | Required XOR validation |

### NMEA Parsing

- **$GxGGA**: latitude, longitude, fix quality, satellites, altitude
- **$GxRMC**: speed (knots), heading (degrees)
- **$GxGSV**: satellites in view and SNR/CN0 diagnostics
- **$GxGSA**: fix type diagnostics
- Both `$GP` (GPS-only) and `$GN` (multi-constellation) prefixes supported
- Checksum validation: XOR of bytes between `$` and `*` must match 2-digit hex
  after `*`. Sentences without checksum are rejected.

### Parsed Fields

| Function              | Source Sentence | Type   |
|-----------------------|-----------------|--------|
| `sigurdos_gps_latitude`  | GGA             | float  |
| `sigurdos_gps_longitude` | GGA             | float  |
| `sigurdos_gps_altitude_m`| GGA             | float  |
| `sigurdos_gps_speed_kn`  | RMC             | float  |
| `sigurdos_gps_heading`   | RMC             | float  |
| `sigurdos_gps_satellites`| GGA             | uint8  |
| `sigurdos_gps_fix_quality`| GGA            | uint8  (0=none, 1=GPS, 2=DGPS) |
| `sigurdos_gps_has_fix`   | Combined        | bool   |
| `sigurdos_gps_hour/min/sec` | GGA        | uint8  (UTC) |

### Init Sequence

1. `Serial1.begin(active_baud, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX)`, starting at 9600 baud and cycling once to 38400 baud if no checksum-valid NMEA is seen
2. Buffer NMEA characters in 128-byte line buffer
3. On `\n` delimiter, validate checksum and dispatch parser

---

## 10. SD Card — SPI + FATFS

| Property           | Value                            |
|--------------------|----------------------------------|
| Interface          | SPI (shared bus, `SPI2_HOST` = FSPI) |
| CS Pin             | **39** (`PIN_SD_CS`)             |
| SCK                | 40 (shared)                      |
| MOSI               | 41 (shared)                      |
| MISO               | 38 (shared)                      |
| SPI Speed          | **4 MHz** (`SD.begin(..., 4000000)`) |
| Filesystem         | FATFS via Arduino SD library     |
| VFS Mountpoint     | **`/sdcard`** (`SIGURDOS_SD_MOUNTPOINT`) |
| Init Strategy     | Single attempt at boot (`sigurdos_sdcard_init()`) + lazy retry<br>via `sigurdos_sdcard_retry()` capped at 3 total attempts |
| Capacity           | Exposed via `sigurdos_sdcard_capacity_bytes()` |

### Shared Bus Note

The SD card uses `FSPI` (SPI2_HOST) via a separate `SPIClass sd_spi` instance.
The display (LovyanGFX) and LoRa (RadioLib) also use SPI2_HOST. All three share
the same physical bus lines (40/38/41). The SD driver calls `sd_spi.begin()`
with the shared pins; the other drivers manage their own bus configuration.

### Init Order

SD card must be initialised **after** the LoRa radio, because the LoRa/SPI init
(`mesh::init()`) sets up the shared bus pins and calls `sigurdos_shared_spi_begin()`.
If the SD card is initialised first with unconfigured pins, FATFS returns
`FR_NOT_READY`.

`sigurdos_sdcard_init()` makes only a **single attempt** at boot for fast startup.
Consumers (e.g., the map renderer) call **`sigurdos_sdcard_retry()`** lazily when
they need the card. The retry is capped at 3 total attempts to avoid unbounded
re-probing of a broken or absent card.

### API

| Function                             | Purpose                   |
|--------------------------------------|---------------------------|
| `sigurdos_sdcard_init()`              | Mount SD card (single attempt, fast boot) |
| `sigurdos_sdcard_retry()`             | Lazy retry (capped at 3), called by consumers |
| `sigurdos_sdcard_mounted()`           | Check mount status        |
| `sigurdos_sdcard_capacity_bytes()`    | Total card capacity       |
| `sigurdos_sdcard_free_bytes()`        | Free space                |
| `sigurdos_sdcard_read(path, buf, len)`| Read file                 |
| `sigurdos_sdcard_write(path, data, len)`| Write file              |
| `sigurdos_sdcard_exists(path)`        | File existence check      |

### T-Deck v1.0 Note

Older T-Deck v1.0 boards may use CS=21 instead of CS=39. If SD card detection
fails on v1.0 hardware, try pin 21.

---

## 11. LoRa — SX1262 (RadioLib)

| Property           | Value                              |
|--------------------|------------------------------------|
| Transceiver        | Semtech SX1262                     |
| Interface          | SPI (shared bus)                   |
| SPI Host           | `SPI2_HOST` (via RadioLib `Module`) |
| Library            | RadioLib via MeshCore `CustomSX1262Wrapper` |

### Pins

| Signal | Pin | Def             |
|--------|-----|-----------------|
| NSS    | 9   | `PIN_LORA_NSS`  |
| SCK    | 40  | `PIN_LORA_SCLK` |
| MOSI   | 41  | `PIN_LORA_MOSI` |
| MISO   | 38  | `PIN_LORA_MISO` |
| DIO1   | 45  | `PIN_LORA_DIO1` — interrupt / wake source |
| RST    | 17  | `PIN_LORA_RESET` — active low |
| BUSY   | 13  | `PIN_LORA_BUSY` — radio busy indicator |

MeshCore also defines aliases (`P_LORA_NSS`, `P_LORA_DIO_1`, `P_LORA_RESET`,
`P_LORA_BUSY`, `P_LORA_SCLK`, `P_LORA_MISO`, `P_LORA_MOSI`) that map to the
same pins for compatibility.

### Radio Defaults (Compile-Time)

| Parameter            | Value     | Macro        |
|----------------------|-----------|--------------|
| Frequency            | 869.618 MHz | `LORA_FREQ`  |
| Bandwidth            | 62.5 kHz  | `LORA_BW`    |
| Spreading Factor     | 8         | `LORA_SF`    |
| Coding Rate          | 5 (4/5)   | `LORA_CR`    |
| TX Power             | 22 dBm    | `LORA_TX_PWR`|

### Runtime Configuration

Radio parameters are configurable at runtime via NVS (`NodePrefs`):

| Field             | Type    | Range          |
|-------------------|---------|----------------|
| `freq`            | float   | MHz            |
| `bw`              | float   | kHz            |
| `sf`              | uint8_t | 6–12           |
| `cr`              | uint8_t | 5–8 (denominator) |
| `tx_power_dbm`    | int8_t  | 2–22 dBm       |

> **Safety:** `NodePrefs.configured` must be `true` before the radio transmits.
> Until the user explicitly saves settings in the UI, radio defaults are used
> only for display purposes and the MeshCore mesh will not start.

### Init Sequence

1. **Hard reset SX1262** via RST pin: LOW 100 µs → HIGH, then wait 10 ms for
   TCXO stabilisation. This prevents bootloop if BUSY is stuck HIGH from a
   previous crash.
2. Initialise SPI bus via `sigurdos_shared_spi_begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI)`
3. Call `radio_module.std_init(&sigurdos_shared_spi())`
4. Apply radio parameters: `setFrequency`, `setBandwidth`, `setSpreadingFactor`,
   `setCodingRate`, `setOutputPower`

### Deep Sleep Wake

- DIO1 is GPIO45 and is not RTC-capable on ESP32-S3, so LoRa packets cannot wake deep sleep
- `rtc_gpio_pulldown_en` configured and `ESP_EXT1_WAKEUP_ANY_HIGH` enabled
- LoRa NSS pin held via `rtc_gpio_hold_en` during sleep

---

## 12. Buzzer

| Property | Value              |
|----------|--------------------|
| Pin      | **46**             |
| Type     | Active-high buzzer (GPIO output - no PWM tone generation) |
| Default  | LOW (off)          |

> The buzzer is driven as a GPIO output - no PWM tone generation is implemented.
> **Non-blocking loop-driven playback:** `buzzer_loop()` is called once per
> main-loop iteration (`main.cpp:189`) and advances through the active pattern's
> step table (`src/hal/buzzer.h`). Each `BuzzerPatternStep` has a `level_high`
> (bool) and `duration_ms` field. Steps with `duration_ms = 0` are terminal
> markers that apply the level and then idle LOW until the next pattern starts.
> Starting a new beep while one is playing replaces it immediately (restart
> semantics — no overlap occurs as only the message-arrival path in `ui.cpp`
> triggers beeps). A `buzzer_quiet` preference in `NodePrefs` mutes
> message-arrival beeps.

> **API:** `buzzer_init()` configures the GPIO; `buzzer_beep_short()` triggers
> a ~100 ms pulse (DM arrival); `buzzer_beep_double()` fires two 60 ms pulses
> 60 ms apart (channel message arrival); `buzzer_loop()` advances playback.

---

## 13. Peripheral Power

| Property | Value              |
|----------|--------------------|
| Pin      | **10**             |
| Type     | Power MOSFET gate  |
| Default  | HIGH (on)          |

- HIGH = peripherals powered (keyboard MCU, GT911 touch, GPS, SD card, LoRa)
- LOW = peripherals disconnected (set before deep sleep, on battery shutdown)
- Controlled by `TDeckBoard::begin()` and `TDeckBoard::sleep()`

---

## 14. Deep Sleep & Wake

### Entering Deep Sleep (`TDeckBoard::sleep`)

1. Set `PIN_PERIPH_PWR` LOW (disconnect peripherals)
2. Set `PIN_TFT_BL` LOW (turn off display backlight)
3. Configure RTC GPIO:
   - `PIN_LORA_DIO1` → input-only with pulldown
   - `PIN_LORA_NSS` → hold enable
4. Enable wake sources:
   - **EXT1:** unavailable: `PIN_LORA_DIO1` is GPIO45, outside the ESP32-S3 RTC GPIO set
   - **Timer:** optional, if `secs > 0`
5. Call `esp_deep_sleep_start()`

### Wake Reasons

| Source            | Mechanism                  | `_startup_reason`    |
|-------------------|----------------------------|----------------------|
| LoRa packet       | Not available in deep sleep | — |
| Timer             | RTC timer                  | `BD_STARTUP_NORMAL`  |
| Power-on / reset  | ESP reset                  | `BD_STARTUP_NORMAL`  |

### Wake Recovery

- `rtc_gpio_hold_dis` on NSS
- `rtc_gpio_deinit` on DIO1
- Display re-initialised with rotation(1) and full black fill

---

## Appendix A — Boot Sequence

Numbers in brackets are the `[boot] step N` markers printed by debug builds.
The splash screen shows a status label updated by `boot_status()`, which calls
`sigurdos::ui::set_boot_status(...)` and flushes the display after each step.

```
 1. Serial.begin(115200)                 [step 1]
 2. TDeckBoard::begin()                  [step 2]
      → PIN_PERIPH_PWR HIGH
      → Trackball GPIO INPUT
      → LoRa DIO1 INPUT_PULLUP
      → ADC resolution 12-bit
      → Wire.begin(18, 8)
      → Deep sleep wake detection
 3. sigurdos_battery_init() + buzzer_init()
 4. sigurdos_display_init()              [step 3]  ◄─ moved BEFORE SPIFFS/GPS
      → LovyanGFX init (rotation 1, 320×240)
      → LVGL init
      → Draw-buffer alloc (PSRAM or DRAM)
      → On failure: restart (no hang)
 5. sigurdos::ui::init()                 [step 4]
      → Splash screen with status label
      → boot_status("Starting SigurdOS...")
 6. SPIFFS.begin()                       [step 5]
      → boot_status("Mounting storage...")
      → Launcher-aware warning if the mount fails
      → boot_status("Storage ready" / "Storage unavailable")
 7. Load NodePrefs, apply theme,
      display brightness, reset auto-off
      → boot_status("Loading settings...")
 8. sigurdos_display_init_inputs()        [step 6]  ◄─ input INITIALISATION deferred
      → GT911 touch init (I2C)
      → Keyboard init (ESP32-C3 I2C)
      → Trackball init (GPIO)
      → boot_status("Starting input...")
      → boot_status("Input ready")
 9. sigurdos_gps_init() (if GPS enabled) [step 7]
      → boot_status("Starting GPS...")
10. sigurdos::mesh::init()               [step 8]
      → Shared SPI bus init
      → SX1262 hard reset + std_init
      → Radio config from prefs or defaults
      → MeshCore SigurdMeshV2 init
      → boot_status("Starting radio...")
      → boot_status("Radio ready" / "Radio unavailable")
11. sigurdos::ui::load_persisted_state()
      → boot_status("Loading chats...")
      → boot_status("Chats ready")
12. Debug diagnostics (debug builds)     [step 9]
13. sigurdos_sdcard_init()               [step 10]
      → boot_status("Checking SD card...")
      → boot_status("SD card ready" / "No SD card")
14. sigurdos_map_init()
      → boot_status("Preparing map...")
15. boot_status("Ready")                 [step 11]
16. WiFi STA auto-connect (non-blocking beginConnect(), if credentials saved)
17. telemetry::init() (telemetry builds)
```

**Key changes from pre-PR-625 boot order:**

| Change | Before | After |
|--------|--------|-------|
| Display init | After SPIFFS & GPS (step 6) | **Before** SPIFFS & GPS (step 4) |
| Input init | Inline in display init | **Deferred** via `sigurdos_display_init_inputs()` (step 8), after prefs loaded |
| Splash status | Static splash | Live status label via `boot_status()` |
| SPIFFS error | Serial-only warning | Also shown on splash status label |

---

## Appendix B — Defines Reference

All hardware pin and configuration defines are in `src/hal/tdeck_pins.h`.

| Define               | Value | Purpose                    |
|-----------------------|-------|----------------------------|
| `PIN_LORA_NSS`        | 9     | LoRa chip select            |
| `PIN_LORA_DIO1`       | 45    | LoRa interrupt              |
| `PIN_LORA_RESET`      | 17    | LoRa reset                  |
| `PIN_LORA_BUSY`       | 13    | LoRa busy indicator         |
| `PIN_LORA_SCLK`       | 40    | SPI clock                   |
| `PIN_LORA_MISO`       | 38    | SPI MISO                    |
| `PIN_LORA_MOSI`       | 41    | SPI MOSI                    |
| `PIN_TFT_CS`          | 12    | Display chip select         |
| `PIN_TFT_DC`          | 11    | Display data/command        |
| `PIN_TFT_RST`         | -1    | Display reset (unused)      |
| `PIN_TFT_BL`          | 42    | Display backlight PWM       |
| `PIN_TOUCH_SDA`       | 18    | Touch I2C data              |
| `PIN_TOUCH_SCL`       | 8     | Touch I2C clock             |
| `PIN_TOUCH_INT`       | 16    | Touch interrupt             |
| `PIN_TOUCH_RST`       | -1    | Touch reset (unused)        |
| `TOUCH_I2C_ADDR`      | 0x5D  | GT911 primary address       |
| `TDECK_KB_I2C_ADDR`   | 0x55  | Keyboard MCU address        |
| `PIN_TRACKBALL`       | 0     | Trackball click / BOOT      |
| `PIN_TRACKBALL_UP`    | 3     | Trackball up                |
| `PIN_TRACKBALL_DOWN`  | 15    | Trackball down              |
| `PIN_TRACKBALL_LEFT`  | 1     | Trackball left              |
| `PIN_TRACKBALL_RIGHT` | 2     | Trackball right             |
| `PIN_BAT_ADC`         | 4     | Battery voltage ADC         |
| `PIN_PERIPH_PWR`      | 10    | Peripheral power enable     |
| `PIN_GPS_RX`          | 44    | GPS UART receive            |
| `PIN_GPS_TX`          | 43    | GPS UART transmit           |
| `PIN_SD_CS`           | 39    | SD card chip select         |
| `PIN_BUZZER`          | 46    | Buzzer output               |
| `TFT_WIDTH`           | 320   | Display width (landscape)   |
| `TFT_HEIGHT`          | 240   | Display height (landscape)  |
| `LORA_FREQ`           | 869.618 | Default frequency (MHz)  |
| `LORA_BW`             | 62.5  | Default bandwidth (kHz)     |
| `LORA_SF`             | 8     | Default spreading factor    |
| `LORA_CR`             | 5     | Default coding rate         |
| `LORA_TX_PWR`         | 22    | Default TX power (dBm)      |
| `BAT_ADC_MULT`        | 6600.0 | ADC-to-mV multiplier        |
| `BAT_MIN_MV`          | 3000  | 0% battery threshold        |
| `BAT_MAX_MV`          | 4200  | 100% battery threshold      |
