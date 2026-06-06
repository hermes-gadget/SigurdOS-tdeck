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
| 45  | LoRa DIO1           | GPIO      | Radio interrupt / wake source       |
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
| Touch      | 0x5D    | 400 kHz     | GT911      |
| Keyboard   | 0x55    | 100 kHz     | ESP32-C3   |

> **Clock contention:** Each driver re-asserts its preferred clock speed before
> every transaction (`Wire.setClock(...)`) because the other device changes it.
> Touch `loop()` calls `Wire.setClock(400000)`; keyboard `scan()` calls
> `Wire.setClock(100000)`.

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

1. Set I2C clock to 400 kHz
2. Configure INT pin as `INPUT_PULLUP`
3. Hardware reset via INT: LOW (1 ms) → HIGH (10 ms) → INPUT_PULLUP
4. Probe both addresses (0x5D, then 0x14)
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
| Bus Speed          | **100 kHz**                   |
| Protocol           | LilyGo T-Deck Keyboard_ESP32C3 (MIT) |
| Operating Mode     | **Raw mode** (bitmask per column) |
| Key mode           | Legacy fallback (ASCII)       |
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

- Raw mode: `Wire.requestFrom(0x55, 5)` returns one 7-bit row mask per column.
- Legacy key mode: `Wire.requestFrom(0x55, 1)` returns one byte:

| Value          | Meaning                        |
|----------------|--------------------------------|
| `0x00`         | No key pressed                 |
| `0x08`         | Backspace                      |
| `0x0D`         | Enter                          |
| `0x09`         | Tab                            |
| `0x0C`         | Channel-menu shortcut event    |
| `0x20`–`0x7E`  | ASCII printable character      |

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

### Host Raw-Mode Key Layers

- `Sym` opens the symbol layer; tapping `Sym` arms it for one key.
- `Alt` opens an on-screen character picker for the pressed base key; tapping
  `Alt` arms the picker for one key.
- `Mic` is a fast extended-character alias for common accented characters.
- `Alt+Space` emits the channel-menu shortcut event (`0x0C`).
- `Alt+B` remains handled by the keyboard MCU for backlight toggling.

### Backlight Control

| API Call                                          | Effect                          |
|----------------------------------------------------|---------------------------------|
| `sigurdos_keyboard_set_brightness(duty)` (0–255)     | Set immediate brightness        |
| `sigurdos_keyboard_set_default_brightness(duty)`     | Set brightness for Alt+B toggle |

- Minimum default brightness: **30** (below this yields no light on toggle)
- Default stored in NVS (`NodePrefs.kbd_backlight`, initial value 127)
- Backlight turns off with display auto-off; restored on wake

### Init Sequence

1. Set I2C clock to 100 kHz
2. Probe: request 1 byte from address 0x55 (must ACK)
3. Send `CMD_BRIGHTNESS` (0x01) with stored value
4. Send `CMD_DEFAULT_BRIGHTNESS` (0x02) with min(30) clamping
5. Send `CMD_MODE_RAW` (0x03) to expose Sym, Shift, Alt, and Mic layers

### Known Limitations

- Ctrl state is not available from the current keyboard matrix.
- Legacy key-mode fallback has best-effort modifier state only because the MCU
  sends pre-processed ASCII bytes instead of raw scancodes.

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

```c
raw_adc = analogRead(PIN_BAT_ADC);         // 0–4095
voltage_mv = (6600.0f * raw_adc) / 4096.0f; // 2× divider compensation
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
| Init Retries       | 3 attempts, 500 ms apart         |
| Capacity           | Exposed via `sigurdos_sdcard_capacity_bytes()` |

### Shared Bus Note

The SD card uses `FSPI` (SPI2_HOST) via a separate `SPIClass sd_spi` instance.
The display (LovyanGFX) and LoRa (RadioLib) also use SPI2_HOST. All three share
the same physical bus lines (40/38/41). The SD driver calls `sd_spi.begin()`
with the shared pins; the other drivers manage their own bus configuration.

### Init Order

SD card must be initialised **after** the LoRa radio, because the LoRa/SPI init
(`mesh::init()`) sets up the shared bus pins and calls `lora_spi.begin()`.
If the SD card is initialised first with unconfigured pins, FATFS returns
`FR_NOT_READY`.

### API

| Function                             | Purpose                   |
|--------------------------------------|---------------------------|
| `sigurdos_sdcard_init()`              | Mount SD card (3 retries) |
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
2. Initialise SPI bus via `lora_spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI)`
3. Call `radio_module.std_init(&lora_spi)`
4. Apply radio parameters: `setFrequency`, `setBandwidth`, `setSpreadingFactor`,
   `setCodingRate`, `setOutputPower`

### Deep Sleep Wake

- DIO1 is used as wake source from deep sleep
- `rtc_gpio_pulldown_en` configured and `ESP_EXT1_WAKEUP_ANY_HIGH` enabled
- LoRa NSS pin held via `rtc_gpio_hold_en` during sleep

---

## 12. Buzzer

| Property | Value              |
|----------|--------------------|
| Pin      | **46**             |
| Type     | Active-low buzzer  |
| Default  | HIGH (off)         |

> The buzzer is driven as a simple GPIO output. Pull LOW to activate, HIGH to
> silence. No PWM tone generation is implemented in the current firmware.

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
   - **EXT1:** `PIN_LORA_DIO1` rising edge (incoming LoRa packet)
   - **Timer:** optional, if `secs > 0`
5. Call `esp_deep_sleep_start()`

### Wake Reasons

| Source            | Mechanism                  | `_startup_reason`    |
|-------------------|----------------------------|----------------------|
| LoRa packet       | DIO1 rising edge (EXT1)    | `BD_STARTUP_RX_PACKET` |
| Timer             | RTC timer                  | `BD_STARTUP_NORMAL`  |
| Power-on / reset  | ESP reset                  | `BD_STARTUP_NORMAL`  |

### Wake Recovery

- `rtc_gpio_hold_dis` on NSS
- `rtc_gpio_deinit` on DIO1
- Display re-initialised with rotation(1) and full black fill

---

## Appendix A — Boot Sequence

```
1. Serial.begin(115200)
2. TDeckBoard::begin()
     → PIN_PERIPH_PWR HIGH
     → Trackball GPIO INPUT
     → LoRa DIO1 INPUT_PULLUP
     → ADC resolution 12-bit
     → Wire.begin(18, 8)
     → Deep sleep wake detection
3. sigurdos_battery_init()
4. SPIFFS.begin()
5. sigurdos_gps_init()
6. sigurdos_display_init()
     → LovyanGFX init (rotation 1, 320×240)
     → LVGL init
     → Touch init (GT911 I2C)
     → Keyboard init (ESP32-C3 I2C)
     → Trackball init (GPIO)
7. sigurdos::mesh::init()
     → LoRa SPI bus init
     → SX1262 hard reset + std_init
     → Radio config from prefs or defaults
     → MeshCore SigurdMeshV2 init
8. sigurdos::ui::init()
9. sigurdos_sdcard_init()
10. sigurdos_map_init()
```

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
