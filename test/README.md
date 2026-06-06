# SigurdOS T-Deck Test Suite

This directory contains the host-side GoogleTest suite for the PlatformIO
`native_test` environment. These tests are meant to catch regressions before a
firmware flash, while still being clear about what must be verified on a real
T-Deck.

## Validation Levels

Use the smallest useful validation first, then expand before opening a PR.

```bash
# Run one focused native test target.
pio test -e native_test -f test_gps

# Run the full host-side native suite.
pio test -e native_test

# Build the firmware image for the LilyGo T-Deck target.
pio run -e SigurdOS_TDeck
```

Every PR should state which validation was run. If a change needs a remote test
fixture or physical T-Deck and it was not available, say that explicitly in the
PR body:

- `Remote test: not run; no remote T-Deck fixture is attached.`
- `Physical hardware test: not run; no LilyGo T-Deck hardware is attached.`

## Test Structure

```text
test/
|-- mocks/                    Native Arduino/LVGL/MeshCore/driver mocks
|-- test_battery/             Battery conversion and ADC math
|-- test_build/               Header inclusion and cross-module sanity checks
|-- test_channel_menu/        Alt+C channel menu actions and private scopes
|-- test_channel_validation/  Channel name validation and sanitising
|-- test_chat_truncation/     UTF-8 safe chat truncation
|-- test_emoji/               Emoji font, lookup, fallback, and data checks
|-- test_gps/                 NMEA parsing, coordinates, checksums, fix fields
|-- test_home_screen/         Home tile routing contract
|-- test_keyboard/            Keyboard scan, event, brightness, injection logic
|-- test_layout/              Screen layout overlap regression checks
|-- test_map/                 Map projection, tile math, tile cache behavior
|-- test_mesh_messaging/      Message queues, contacts, responses, LPP parsing
|-- test_mesh_wrapper/        Public mesh API contracts and return ranges
|-- test_navigation/          Navigation stack and back-swipe behavior
|-- test_pins/                GPIO ranges, conflicts, and board pin sanity
|-- test_prefs/               Preferences defaults and native mock persistence
|-- test_regions/             Region structs, binary layout, key derivation
|-- test_sdcard/              SD card state, path checks, size formatting
|-- test_terminal/            Terminal line cap behavior
|-- test_theme/               Theme contrast and color distinctness
|-- test_touch/               GT911 coordinate parsing and screen mapping
|-- test_trackball/           Trackball debounce, direction, and click events
```

## Mocks

The native environment uses `test/mocks` plus selected source files from
`platformio.ini`'s `native_test` source filter.

- `Arduino.h` provides time, GPIO, analog, serial, SPI, and Wire stubs.
- `lvgl.h` provides lightweight LVGL object and style stubs.
- `RadioLib.h`, `MeshCore.h`, and `mesh_helpers.h` provide radio/mesh compile
  surfaces without hardware.
- `mock_prefs.cpp`, `mock_mesh_wrapper.cpp`, and `mock_fonts.cpp` provide native
  link targets for firmware modules that normally depend on hardware or assets.

Use `arduino_mock::reset()` in `SetUp()` when a test mutates mock time, GPIO,
I2C, or serial state.

## Adding Tests

1. Create `test/test_<area>/main.cpp` and `test/test_<area>/test_<area>.cpp`.
2. Include `gtest/gtest.h` and the smallest production header needed.
3. Keep behavior tests close to the module under test.
4. Prefer contract tests for public enums, structs, buffer sizes, and function
   signatures when hardware behavior cannot be exercised natively.
5. Use `ASSERT_*` for fatal preconditions and `EXPECT_*` for ordinary checks.
6. Run the focused target, then the full native suite when the change can affect
   shared behavior.

## Native vs. Hardware

Native tests are useful for parser logic, bounds checks, queue behavior,
contracts, layouts, and mockable state machines. They do not prove:

- LoRa RF behavior or regional compliance.
- Real keyboard, touch, display, GPS, SD card, WiFi, or power-management timing.
- MeshCore interop with another device.
- Remote-test controller behavior unless the remote fixture is actually used.

For those areas, include a physical or remote test note in the PR body, even
when the note is "not run".
