# Logging Macros

> **Source**: `src/diagnostics/log.h`
> **Test**: `test/test_log/main.cpp`

The logging system provides three lightweight, printf-style macros (`SIG_LOGE`, `SIG_LOGW`, `SIG_LOGD`) that wrap `Serial.printf()` with automatic severity prefixing and newline appending. The debug-level macro (`SIG_LOGD`) compiles out entirely in release builds, eliminating any runtime overhead for debug diagnostics in production firmware.

---

## Table of Contents

- [Source Files](#source-files)
- [Macros Reference](#macros-reference)
  - [Severity Levels](#severity-levels)
  - [Format Conventions](#format-conventions)
  - [Newline Behaviour](#newline-behaviour)
  - [Macro Expansion Examples](#macro-expansion-examples)
- [Compile-time Gating (`SIGURDOS_DEBUG`)](#compile-time-gating-sigurdos_debug)
  - [Debug Build Environments](#debug-build-environments)
- [Migration Guide (from raw `Serial.printf`)](#migration-guide-from-raw-serialprintf)
  - [Migration Checklist](#migration-checklist)
  - [Before / After Examples](#before--after-examples)
- [Test Coverage](#test-coverage)
  - [Running the Tests](#running-the-tests)
- [Pitfalls](#pitfalls)

---

## Source Files

| File | Role |
|------|------|
| `src/diagnostics/log.h` | Macro definitions — the single header to include |
| `test/test_log/main.cpp` | GoogleTest unit tests for all three macros |
| `test/mocks/Arduino.h` | Mock `HardwareSerial` with `mock_tx_output()` and `mock_reset()` |
| `platformio.ini` | `[env:native_test]` (no `SIGURDOS_DEBUG`), `[env:SigurdOS_TDeck_debug]` (sets `-D SIGURDOS_DEBUG=1`) |

---

## Macros Reference

All three macros are defined in a single header and share the same signature:

```cpp
#include <Arduino.h>   // for Serial.printf

#define SIG_LOGE(fmt, ...) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__)
#define SIG_LOGW(fmt, ...) Serial.printf("[W] " fmt "\n", ##__VA_ARGS__)

#if defined(SIGURDOS_DEBUG)
#define SIG_LOGD(fmt, ...) Serial.printf("[D] " fmt "\n", ##__VA_ARGS__)
#else
#define SIG_LOGD(fmt, ...) do { } while (0)
#endif
```

### Severity Levels

| Macro | Prefix | Severity | Purpose |
|-------|--------|----------|---------|
| `SIG_LOGE(fmt, ...)` | `[E]` | **Error** | Unrecoverable faults, hardware failures, configuration errors. Always compiled in. |
| `SIG_LOGW(fmt, ...)` | `[W]` | **Warning** | Recoverable issues, unexpected states, degraded operation. Always compiled in. |
| `SIG_LOGD(fmt, ...)` | `[D]` | **Debug** | Development diagnostics, verbose state dumps, trace logging. Compiles out unless `SIGURDOS_DEBUG` is defined. |

### Format Conventions

- **printf-style**: All macros accept a format string and variadic arguments identical to `printf()` / `Serial.printf()`.
- **Subsystem prefix**: Production code prefixes log messages with a subsystem tag in square brackets inside the format string:

```cpp
SIG_LOGW("[ota] REFUSED: OTA not available under bmorcelli/Launcher ...");
SIG_LOGD("[wifi-sta] connecting to %s...", ssid);
SIG_LOGD("[wifi-sta] connected! (%d dBm)", s_rssi);
```

| Subsystem Tag | File(s) |
|---------------|---------|
| `[ota]` | `src/hal/wifi_ota.cpp` |
| `[wifi-sta]` | `src/hal/wifi_ota.cpp` |

- **No trailing newline in format string**: The macros append `"\n"` automatically. Do **not** include a `\n` in the format string — doing so produces double-spaced output.

### Newline Behaviour

Every macro invocation **always** appends a single `\n` to the output. This is a deliberate design choice:

- Each call produces exactly one line on the serial output.
- There is no `SIG_LOG` (no-newline) variant — multi-part messages requiring a single line must be formatted with `snprintf` into a buffer first, then logged with a single macro call.
- The `do { } while (0)` no-op stub for release-build `SIG_LOGD` consumes exactly one statement, so control flow (if/else without braces) is safe.

### Macro Expansion Examples

| Invocation | Output (serial) |
|------------|-----------------|
| `SIG_LOGE("failure %d", 7)` | `[E] failure 7` |
| `SIG_LOGW("warning")` | `[W] warning` |
| `SIG_LOGD("debug %s", "message")` (debug build) | `[D] debug message` |
| `SIG_LOGD("debug %s", "message")` (release build) | *(no output — compiled out)* |

---

## Compile-time Gating (`SIGURDOS_DEBUG`)

`SIG_LOGD` is guarded by the preprocessor macro `SIGURDOS_DEBUG`:

```cpp
#if defined(SIGURDOS_DEBUG)
#define SIG_LOGD(fmt, ...) Serial.printf("[D] " fmt "\n", ##__VA_ARGS__)
#else
#define SIG_LOGD(fmt, ...) do { } while (0)
#endif
```

- **Defined**: `SIG_LOGD` produces real serial output (debug/build-info builds).
- **Not defined**: `SIG_LOGD` expands to a no-op — zero code size, zero execution time. The format string and arguments are **not evaluated**, so side-effecting expressions inside `SIG_LOGD` calls will not execute in release builds.

### Debug Build Environments

The debug build environment `[env:SigurdOS_TDeck_debug]` (defined in `platformio.ini`) sets `SIGURDOS_DEBUG` and other debug flags:

```ini
[env:SigurdOS_TDeck_debug]
extends = env:SigurdOS_TDeck
build_flags =
  ${env:SigurdOS_TDeck.build_flags}
  -D SIGURDOS_DEBUG=1
  -D CORE_DEBUG_LEVEL=5
  -D SIGURDOS_CRASH_RING=1
  -D SIGURDOS_DEBUG_DISPLAY=1
  -D SIGURDOS_DEBUG_MESH=1
  -D SIGURDOS_DEBUG_UI=1
  -D SIGURDOS_DEBUG_MAP=1
  -D SIGURDOS_DEBUG_DIAG=1
```

Related per-feature debug environments (each extends `[env:SigurdOS_TDeck]` and enables its own `SIGURDOS_DEBUG_*` flag):

| Environment | Debug Area |
|-------------|------------|
| `SigurdOS_TDeck_debug` | Full debug (all features + `SIGURDOS_DEBUG=1`) |
| `SigurdOS_TDeck_debug_869` | Same as debug, but on 869 MHz EU band |
| `SigurdOS_TDeck_debug_display` | Display-only debug (`SIGURDOS_DEBUG_DISPLAY=1`) |
| `SigurdOS_TDeck_debug_mesh` | Mesh-only debug (`SIGURDOS_DEBUG_MESH=1`) |
| `SigurdOS_TDeck_debug_ui` | UI-only debug (`SIGURDOS_DEBUG_UI=1`) |
| `SigurdOS_TDeck_debug_map` | Map-only debug (`SIGURDOS_DEBUG_MAP=1`) |
| `SigurdOS_TDeck_debug_diag` | Diagnostics-only debug (`SIGURDOS_DEBUG_DIAG=1`) |
| `SigurdOS_TDeck_telemetry` | Telemetry (extends debug) |

**Important**: The `[env:native_test]` environment does **not** define `SIGURDOS_DEBUG`. This means `SIG_LOGD` compiles out during native tests, which is why the test at `test/test_log/main.cpp` explicitly checks both branches with `#if defined(SIGURDOS_DEBUG)`.

---

## Migration Guide (from raw `Serial.printf`)

Prior to the introduction of these macros, production code used raw `Serial.printf()` calls. The macros provide consistent severity labelling, compile-time removal of debug output, and a single point of control for log output conventions.

### Migration Checklist

1. **Replace all raw `Serial.printf` calls** in non-test source files with the appropriate macro:
   - Error conditions → `SIG_LOGE`
   - Warnings / recoverable issues → `SIG_LOGW`
   - Development diagnostics → `SIG_LOGD`
2. **Remove trailing `\n`** from format strings — the macro adds it automatically.
3. **Add a subsystem tag** (e.g. `[ota]`, `[wifi-sta]`) inside the format string for log origin identification.
4. **Do not use `SIG_LOGD` for messages that must always appear** — it compiles out in release builds. Use `SIG_LOGW` or `SIG_LOGE` instead.
5. **Keep `#include "diagnostics/log.h"`** in every file that uses the macros. The header is lightweight (no extra dependencies beyond `<Arduino.h>`).

### Before / After Examples

**Before (raw `Serial.printf`):**
```cpp
Serial.printf("WiFi connected! (%d dBm)\n", rssi);
Serial.printf("[ota] Update failed: %s\n", Update.errorString());
Serial.printf("RSSI: %d SNR: %.1f\n", lastRSSI, lastSNR);
```

**After (logging macros):**
```cpp
SIG_LOGD("[wifi-sta] connected! (%d dBm)", rssi);
SIG_LOGW("[ota] Update failed: %s", Update.errorString());
SIG_LOGD("RSSI: %d SNR: %.1f", lastRSSI, lastSNR);
```

---

## Test Coverage

A dedicated GoogleTest module lives at `test/test_log/main.cpp` and covers two test cases:

**Test 1: `LogMacrosTest::ErrorAndWarningAddLevelPrefixAndNewline`**

Verifies that `SIG_LOGE` and `SIG_LOGW` always emit output with the correct level prefix and trailing newline:

```cpp
Serial.mock_reset();
SIG_LOGE("failure %d", 7);
SIG_LOGW("warning");
EXPECT_EQ("[E] failure 7\n[W] warning\n", Serial.mock_tx_output());
```

**Test 2: `LogMacrosTest::DebugLogsCompileOutUnlessDebugBuild`**

Verifies that `SIG_LOGD` only produces output in debug builds:

```cpp
Serial.mock_reset();
SIG_LOGD("debug %s", "message");
#if defined(SIGURDOS_DEBUG)
EXPECT_EQ("[D] debug message\n", Serial.mock_tx_output());
#else
EXPECT_TRUE(Serial.mock_tx_output().empty());
#endif
```

The test uses `#if defined(SIGURDOS_DEBUG)` at compile time to assert the correct behaviour for the current build configuration — it passes in both debug and release builds.

### Running the Tests

```bash
# Run all native tests (includes test_log)
pio test -e native_test -v

# Run only the log tests
pio test -e native_test -f test_log -v
```

**Note**: The native test environment does not define `SIGURDOS_DEBUG`, so `SIG_LOGD` compiles out and the test verifies empty output. To test `SIG_LOGD` live, flash a debug build:

```bash
pio run -e SigurdOS_TDeck_debug
```

---

## Pitfalls

### 1. `SIG_LOGD` Evaluates Arguments Only in Debug Builds

The release-build no-op (`do { } while (0)`) means that **side effects inside a `SIG_LOGD` call are silently discarded** in release builds:

```cpp
// BAD — counter never increments in release builds
SIG_LOGD("packet #%d", packet_count++);

// GOOD — increment outside the macro
packet_count++;
SIG_LOGD("packet #%d", packet_count);
```

### 2. No Newline-Free Variant

There is no `SIG_LOG` macro (without automatic `\n`). If you need to build a line piece by piece (e.g. printing a hex dump), use `snprintf()` into a local buffer and pass it to a single macro call. Attempting to simulate no-newline output by omitting the format string produces an empty line:

```cpp
// Does NOT work — prints "[E] \n" followed by "[E] continuation\n"
SIG_LOGE("");       // → [E]
SIG_LOGE("continuation");
```

### 3. Double Newlines

Because the macro appends `\n` automatically, including `\n` in the format string produces blank lines:

```cpp
// Produces: [W] warning\n\n  (double-spaced)
SIG_LOGW("warning\n");
```

### 4. Include Order

The macros depend on `Serial.printf` from `<Arduino.h>`. The `log.h` header includes `<Arduino.h>` itself, so any file that includes `log.h` is safe. However, if a source file uses the macros without including `log.h` (relying on a transitive include), a future refactor could break compilation. Always include `diagnostics/log.h` explicitly.

### 5. `SIG_LOGE` / `SIG_LOGW` Are Always Compiled In

These two macros always produce serial output. On battery-powered devices with long uptimes, excessive warning/error logging can consume UART bandwidth and contribute to interrupt overhead. Use them judiciously — prefer `SIG_LOGD` for high-frequency or verbose diagnostics.

### 6. No Timestamp, No Severity Filtering at Runtime

The macros are simple text pre-processor expansions — they do not add timestamps, module names, or thread IDs. Runtime filtering (e.g., "only show errors") is not supported. If runtime filtering is needed, wrap the macros in a logging function that checks a global severity mask before calling `Serial.printf`.

### 7. Test Mock Limitations

The `HardwareSerial` mock in `test/mocks/Arduino.h` captures all `printf` output into a `std::string` buffer. This is sufficient for asserting output content, but it does not simulate UART buffer overruns, interrupt timing, or hardware flow control. Integration testing on real hardware is recommended for timing-sensitive logging scenarios.

---

## Summary

| Macro | Prefix | Always compiled? | Use for |
|-------|--------|-----------------|---------|
| `SIG_LOGE(fmt, ...)` | `[E]` | ✅ Yes | Errors, unrecoverable faults |
| `SIG_LOGW(fmt, ...)` | `[W]` | ✅ Yes | Warnings, recoverable issues |
| `SIG_LOGD(fmt, ...)` | `[D]` | ❌ Only with `SIGURDOS_DEBUG` | Development diagnostics, verbose trace |
