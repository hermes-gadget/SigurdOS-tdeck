# SlopOS T-Deck Test Suite

## Running Tests

```bash
# Run all 323 tests on native platform (no hardware needed)
pio test -e native_test -v

# Run specific test file
pio test -e native_test -f test_battery -v

# Run with verbose output
pio test -e native_test -vv
```

## Test Structure

```
test/
├── mocks/                     # Mock headers for native testing (no hardware needed)
│   ├── Arduino.h              # Mock Arduino core (millis, GPIO, Serial, SPI, Wire)
│   ├── lvgl.h                 # Mock LVGL v9 API (all UI objects stubbed)
│   ├── RadioLib.h             # Mock SX1262 radio driver
│   ├── LovyanGFX.hpp          # Mock LovyanGFX display driver
│   ├── MeshCore.h             # Mock MeshCore base classes
│   └── mesh_helpers.h         # Mock AutoDiscoverRTCClock, StdRNG
├── test_battery/              # Battery mV→% conversion, ADC math, edge cases
├── test_build/                # All headers compile together, cross-module consistency
├── test_chat_truncation/      # Chat message truncation, long text handling
├── test_emoji/                # Emoji font rendering, fallback logic, sizing
├── test_gps/                  # NMEA parsing, coordinate conversion, fix detection
├── test_home_screen/          # Home screen layout, icon grid, status bars
├── test_keyboard/             # Matrix scan, keymap, debounce, ghost detection
├── test_map/                  # Tile math (lat/lon→tile), zoom levels
├── test_mesh_messaging/       # Message queue, send/receive, channel ops
├── test_mesh_wrapper/         # Mesh API contract, return value ranges
├── test_navigation/           # Screen routing state machine, back nav
├── test_pins/                 # Pin conflicts, GPIO ranges, bus consistency
├── test_prefs/                # NodePrefs defaults and persistence behavior
├── test_sdcard/               # SPI init, mount, read/write, edge cases
├── test_terminal/             # Terminal buffer management, command parsing
├── test_theme/                # Color constants, distinctness, brightness
├── test_touch/                # GT911 coordinate mapping, multitouch, lifecycle
└── test_trackball/            # Trackball debounce, direction detection, click
```

## What's Tested

| Module | Tests | Coverage |
|--------|-------|----------|
| Build integration | 7 | Header inclusion, API existence, cross-module consistency |
| Chat truncation | 10 | Message truncation at max length, edge cases |
| Emoji | 22 | Emoji font rendering, fallback, sizing |
| GPS | 26 | NMEA parsing, coordinate conversion, fix detection |
| Home screen | 16 | Layout, icon grid, status bar interactions |
| Keyboard | 21 | Matrix scan, keymap, debounce, ghost detection, LVGL mapping |
| Map renderer | 25 | Tile math (lat/lon→tile), zoom levels, bounding box |
| Mesh messaging | 64 | Message queue, send/receive, channel ops, contact export |
| Mesh wrapper | 22 | API signatures, return ranges, unread count init |
| Navigation | 22 | Forward/back, history stack, deep nav, all pairs |
| Pin definitions | 9 | GPIO range, SPI/I2C conflicts, duplicates, LoRa params |
| Preferences | 3 | NodePrefs defaults, mock persistence, RX boosted gain setting |
| SD Card | 15 | SPI init, mount, read/write, directory listing, edge cases |
| Terminal | 7 | Buffer management, command parsing, line handling |
| Theme constants | 7 | Darkness, vibrancy, distinctness, readability |
| Touch (GT911) | 22 | Coordinate mapping, multitouch, press→release, edge cases |
| Trackball | 9 | Direction debounce, deadtime, click detection, idle calibration |

## Adding New Tests

1. Create `test/test_newthing/` directory with `main.cpp` + `test_newthing.cpp`
2. Include the header(s) under test + `gtest/gtest.h`
3. Use `arduino_mock::reset()` in SetUp for clean state
4. Follow AAA pattern: Arrange, Act, Assert
5. Use `EXPECT_*` for non-fatal assertions, `ASSERT_*` for fatal

### Mock Guidelines
- `arduino_mock::current_millis` — control time
- `arduino_mock::analog_values[pin]` — set ADC values
- `arduino_mock::pin_states[pin]` — set digital pin states
- All LVGL object creation returns dummy objects (no rendering)
