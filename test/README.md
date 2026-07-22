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
|-- mocks/                          Native Arduino/LVGL/MeshCore/driver mocks
|-- test_battery/                   Battery conversion and ADC math
|-- test_advert_blob/               Advert persistence size and key-path invariants
|-- test_atomic_file/               Validated atomic replacement and fault recovery
|-- test_ble_frame_queue/           BLE host-to-loop frame handoff, incl. thread stress
|-- test_boot_watchdog/             Setup/runtime watchdog policy and deadline bounds
|-- test_build/                     Header inclusion and cross-module sanity checks
|-- test_build_info/                Firmware version string and build info defaults
|-- test_buzzer/                    Buzzer notification patterns and duration bounds
|-- test_channel_menu/              Channel menu actions and private scopes
|-- test_channel_store/             Transactional two-bank channel persistence
|-- test_channel_validation/        Channel name validation and sanitising
|-- test_chat_config/               Chat message cap clamping and config bounds
|-- test_chat_history_store/        Legacy `/msgs` migration, recovery, and unified-store dedup
|-- test_chat_message_buffer/       Per-channel buffer alloc fallback, eviction, remap handoff
|-- test_chat_truncation/           UTF-8 safe chat truncation
|-- test_companion_mesh_policy/     Companion message filtering and routing policy
|-- test_companion_protocol/        Companion protocol frame shapes and sync behavior
|-- test_contact_paging/            Contact list paging bounds and page clamp logic
|-- test_contact_store/             Contact persistence format, magic header, bounds checks
|-- test_control_parser/            Strict terminal control-command tokenization
|-- test_controller/                RF parameter parsing and validation
|-- test_debug/                     Debug level bounds and stubs
|-- test_display_buffer_policy/     LVGL render-buffer allocation and fallback policy
|-- test_display_retry_state/       Deferred display/input initialization retry state
|-- test_emoji/                     Emoji font, lookup, fallback, and data checks
|-- test_emoji_fallback/            Emoji font fallback wrapper registration and writable copies
|-- test_emoji_integrity/           Emoji font index coverage and uniqueness
|-- test_flood_scope_state/         Flood-scope key and route selection state
|-- test_github_ota_contract/       GitHub OTA state enum, buffer capacities, and plan fallback
|-- test_gps/                       NMEA parsing, coordinates, checksums, fix fields
|-- test_hal_contract/              HAL lifecycle, power, display, and GPS API stability
|-- test_hal_oom/                   HAL allocation-failure and recovery behavior
|-- test_home_screen/               Home tile routing contract
|-- test_i2c_bus/                   Shared I2C probing, configuration, and bus recovery
|-- test_identity_store/            Atomic, checksummed identity persistence
|-- test_input_contract/            Trackball, keyboard, and input event encoding stability
|-- test_keyboard/                  Keyboard scan, event, brightness, injection logic
|-- test_keyboard_layouts/          Layout mappings, shifted digits, and cycle gestures
|-- test_launcher_env/              Launcher detection, partition probing, false-positive guards
|-- test_layout/                    Screen layout overlap regression checks
|-- test_list_virtualization/       Bounded newest-first list window and page math
|-- test_lodepng_alloc/             LodePNG PSRAM allocator with DRAM fallback
|-- test_log/                       Logging macro levels and compile-time gating
|-- test_main_loop/                 Cooperative service dispatch and ordering contract
|-- test_map/                       Map projection, LRU/negative cache, and load budgets
|-- test_map_renderer/              Map renderer constants, zoom validation, and tile math
|-- test_mesh_contract/             Mesh advert types, contact flags, and buffer capacity stability
|-- test_mesh_messaging/            Message queues, contacts, responses, LPP parsing
|-- test_mesh_wrapper/              Public mesh API contracts and return ranges
|-- test_mesh_wrapper_internal/     Wrapper seam helpers: scope-key hex codec, DM conversation key
|-- test_message_store/             Message append, dedup, rotation, and persistence
|-- test_navigation/                Navigation stack and back-swipe behavior
|-- test_navigation_contract/       Screen enum stability and screen inventory checks
|-- test_notifications/             Alert priority, mention matching, expiry, and resource thresholds
|-- test_onboarding/                Onboarding date/time validation and leap year rules
|-- test_ota_auth/                  OTA authentication, URL, and certificate policy
|-- test_ota_boot_health/           OTA boot-health confirmation and rollback deadlines
|-- test_path_autoadd/              Received-path contact auto-add policy
|-- test_path_codec/                Path byte encoding and decoding boundaries
|-- test_pins/                      GPIO ranges, conflicts, and board pin sanity
|-- test_prefs/                     Preferences defaults and native mock persistence
|-- test_prefs_defaults/            Radio, identity, and UI preference default values
|-- test_qr_show/                   QR code version sizing, buffer sizing, and scale fitting
|-- test_radio_profiles/            Regional radio profile selection and persistence
|-- test_regions/                   Region structs, binary layout, key derivation
|-- test_responsive/                Responsive layout column offset distribution
|-- test_sdcard/                    SD card state, path checks, size formatting
|-- test_screen_lifetime/           Screen delete guard: tracked pointer nulling, timer teardown
|-- test_storage/                   SPIFFS mount, erased-partition recovery, and failure policy
|-- test_tdeck_board/               Board power thresholds and shutdown logic
|-- test_telemetry_collectors/      Telemetry task watermark and buffer null-safety
|-- test_telemetry_crash/           Crash backtrace capacity and bounded count
|-- test_telemetry_drift/           Telemetry timing drift and rollover handling
|-- test_telemetry_hb_ring/         Heartbeat ring buffer wrap, read, and retention
|-- test_telemetry_input/           Telemetry input sampling and direction validation
|-- test_telemetry_packet_log/      Telemetry packet log field formatting
|-- test_telemetry_protocol/        Telemetry record emission and field encoding
|-- test_terminal/                  Terminal line cap behavior
|-- test_theme/                     Theme contrast and color distinctness
|-- test_time_state/                UTC epoch vectors and time-source/age tracking
|-- test_touch/                     GT911 coordinate parsing and screen mapping
|-- test_trackball/                 Trackball debounce, direction, and click events
|-- test_transport_key_store/       Private-region transport key persistence and bounds
|-- test_ui_contract/               UI screen show APIs and screen function stability
|-- test_ui_lifecycle/              LVGL timer ownership and display timeout normalization
|-- test_ui_timing/                 Splash screen timing and millisecond rollover
|-- test_wifi_scan/                 Wi-Fi scan AP count, sorting, and input validation
|-- test_wifi_sta/                  Wi-Fi validation upload and reconnect state machines
```

The catalog is checked in CI against the target names produced by
`pio test -e native_test --list-tests`. The success message reports the current
derived count; do not add a hand-maintained total here.

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
