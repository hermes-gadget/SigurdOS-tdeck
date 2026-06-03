# SigurdOS T-Deck — Comprehensive Telemetry Expansion Plan

## Production-Grade Debug Architecture for AI Agent Visibility

---

## Table of Contents

1. [Overview & Design Principles](#1-overview--design-principles)
2. [New Data Structures & Memory Budget](#2-new-data-structures--memory-budget)
3. [Gap-by-Gap Implementation Plan](#3-gap-by-gap-implementation-plan)
4. [New Telemetry Record Types & Wire Format](#4-new-telemetry-record-types--wire-format)
5. [Protocol Extension: New Tag & Key Constants](#5-protocol-extension-new-tag--key-constants)
6. [Integration Points in Existing Code](#6-integration-points-in-existing-code)
7. [The "Full Device State" Command — Agent Initial Sync](#7-the-full-device-state-command--agent-initial-sync)
8. [Crash Capture Subsystem](#8-crash-capture-subsystem)
9. [Ring Buffer System (hb-ring)](#9-ring-buffer-system-hb-ring)
10. [Priority Ordering](#10-priority-ordering)
11. [How the AI Agent Consumes This Data — "Agent View"](#11-how-the-ai-agent-consumes-this-data--agent-view)
12. [File-by-File Implementation Checklist](#12-file-by-file-implementation-checklist)

---

## 1. Overview & Design Principles

### Guiding Principles

1. **Zero DRAM overhead when disabled** — all additions gated behind `SIGURDOS_TELEMETRY`
2. **No heap allocation in hot paths** — all emission via `Serial.print()`, pre-formatted static buffers
3. **PSRAM for large buffers** — crash backtrace ring, hb-ring history, packet log
4. **Diff-based where possible** — avoid repeating unchanged data
5. **Event-driven, not polled** — input events push telemetry records; heartbeat pulls system state
6. **Agent-consumable** — every record is a single line of `@tag|key=value|...\n`
7. **Proactive alerting** — drift thresholds, watchdog events, battery critical, display wake/sleep transitions all emit `@alert`

### File Architecture

```
src/diagnostics/
├── telemetry.h                 # API header (extend with new commands)
├── telemetry.cpp               # Engine core (extend with new collectors)
├── telemetry_protocol.h        # Tag/key constants (add new)
├── telemetry_protocol.cpp      # Protocol emitters (add new)
├── telemetry_crash.h           # NEW — crash capture subsystem
├── telemetry_crash.cpp         # NEW — panic handler, backtrace, RTC storage
├── telemetry_hb_ring.h         # NEW — heartbeat ring buffer in PSRAM
├── telemetry_hb_ring.cpp       # NEW — ring buffer implementation
├── telemetry_collectors.h      # NEW — peripheral state collectors
├── telemetry_collectors.cpp    # NEW — GPS, SD, WiFi, LoRa, temp collectors
└── telemetry_input.h           # NEW — input event capture
└── telemetry_input.cpp         # NEW — keyboard/touch/trackball event capture

src/test/
└── test_controller.cpp         # Extend with new commands

src/hal/
├── display.cpp                 # Add telemetry hooks
├── keyboard.cpp                # Add telemetry hooks
├── touch.cpp                   # Add telemetry hooks
├── trackball.cpp               # Add telemetry hooks

src/ui/
└── navigation.cpp              # Add telemetry hooks on screen transitions

src/mesh/
└── mesh_wrapper.cpp            # Add telemetry hooks on packet events
```

---

## 2. New Data Structures & Memory Budget

### 2.1 DRAM Budget

| Structure | Size | Location | Description |
|-----------|------|----------|-------------|
| `s_prev` (existing) | ~22 bytes | DRAM | Diff snapshot |
| 3× DriftDetector (existing) | ~48 bytes | DRAM | Heap/PSRAM/RSSI drift |
| s_last_loop_us | 4 bytes | DRAM | Loop timing capture |
| s_lvgl_evq_depth | 2 bytes | DRAM | LVGL event queue snapshot |
| s_lvgl_render_count | 4 bytes | DRAM | Render counter |
| s_display_on | 1 byte | DRAM | Display power state |
| s_display_transitions | 2 bytes | DRAM | Wake/sleep transition counter |
| s_active_screen | 1 byte | DRAM | Current Screen enum value |
| s_last_screen | 1 byte | DRAM | Previous Screen enum value |
| s_screen_birth | 4 bytes | DRAM | ms when current screen was entered |
| s_last_key_event | 2 bytes | DRAM | Last keyboard key code |
| s_last_key_ms | 4 bytes | DRAM | Timestamp of last key event |
| s_touch_count | 2 bytes | DRAM | Total touch events since boot |
| s_trackball_count | 2 bytes | DRAM | Total trackball events since boot |
| s_wake_count | 2 bytes | DRAM | Display wake count |
| s_sleep_count | 2 bytes | DRAM | Display sleep count |
| s_wdt_triggered | 1 byte | DRAM | Watchdog event flag |
| s_nvs_stats_last | 8 bytes | DRAM | Cached NVS usage stats |
| **Total new DRAM** | **~38 bytes** | | |
| Existing DRAM | ~70 bytes | | |
| **Grand total DRAM** | **~108 bytes** | | |

### 2.2 PSRAM Budget (Optional, gated behind PSRAM availability)

| Structure | Size | Description |
|-----------|------|-------------|
| hb-ring buffer | 6 KB | 120 entries × ~50 bytes each |
| Crash backtrace buffer | 1 KB | 8 entries × 128 bytes |
| Mesh packet log (extended) | 4 KB | 64 entries × 64 bytes |
| Input event log | 2 KB | 64 entries × 32 bytes |
| **Total PSRAM** | **~13 KB** | |

### 2.3 New Struct Definitions

```cpp
// ── src/diagnostics/telemetry_collectors.h ──
#pragma once
#include <cstdint>

namespace sigurdos {
namespace telemetry {

// GPS state snapshot (polled on heartbeat tick)
struct GpsSnapshot {
    bool     has_fix;
    uint8_t  fix_quality;     // 0=none, 1=GPS, 2=DGPS, 4=RTK
    uint8_t  satellites;
    float    latitude;
    float    longitude;
    float    altitude_m;
    float    speed_kn;
    float    heading;
};

// SD card state snapshot
struct SdCardSnapshot {
    bool     mounted;
    uint64_t capacity_bytes;
    uint64_t free_bytes;
};

// WiFi state snapshot
struct WifiSnapshot {
    bool     sta_connected;
    int      sta_rssi;
    bool     ota_active;
};

// LoRa radio config snapshot
struct LoRaSnapshot {
    float    frequency_mhz;
    float    bandwidth_khz;
    uint8_t  spreading_factor;
    uint8_t  coding_rate;
    int8_t   tx_power_dbm;
    uint32_t total_tx_airtime_ms;
    uint32_t total_rx_airtime_ms;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t rx_errors;
    uint32_t crc_errors;       // NEW — CRC error counter
    uint32_t duty_cycle_pct;   // NEW — current duty cycle usage
};

// NVS (preferences) usage snapshot
struct NvsSnapshot {
    uint32_t used_bytes;
    uint32_t total_bytes;
    uint32_t entry_count;
};

// Task stack watermark snapshot (populated per-task)
struct TaskWatermark {
    char     name[24];         // Task name from FreeRTOS
    uint32_t stack_hwm;        // Stack high water mark in bytes
};

// Drift detector configuration block (for the query dump)
struct DriftState {
    int32_t  baseline;
    int32_t  current;
    int32_t  min_val;
    int32_t  max_val;
    int32_t  delta;
    int32_t  threshold;
    const char* trend;
    bool     active;
};

} // namespace telemetry
} // namespace sigurdos
```

```cpp
// ── src/diagnostics/telemetry_input.h ──
#pragma once
#include <cstdint>
#include "../hal/trackball.h"

namespace sigurdos {
namespace telemetry {

// Input event types
enum class InputEventType : uint8_t {
    None = 0,
    Keyboard,
    Touch,
    Trackball,
    DisplayWake,
    DisplaySleep,
};

// Captured input event
struct InputEvent {
    uint32_t      timestamp_ms;
    InputEventType type;
    union {
        struct { uint8_t key_code; } kbd;
        struct { int x; int y; bool pressed; } touch;
        struct { SigurdOSTrackballEvent dir; } trackball;
    };
    bool     consumed;  // set when reported via telemetry
};

} // namespace telemetry
} // namespace sigurdos
```

---

## 3. Gap-by-Gap Implementation Plan

### Gap 1: Crash Capture — `cmd_crash()` fully stubbed

**Files to create:**
- `src/diagnostics/telemetry_crash.h`
- `src/diagnostics/telemetry_crash.cpp`

**Implementation:**

```cpp
// telemetry_crash.h
#pragma once
#include <cstdint>

namespace sigurdos {
namespace telemetry {

// Maximum backtrace depth
static constexpr uint32_t CRASH_BT_DEPTH = 8;

// Crash record stored in RTC_NOINIT memory
struct CrashRecord {
    uint32_t magic;              // validation magic 0xDEADBEEF
    uint32_t reset_reason;       // esp_reset_reason_t
    uint32_t panic_addr;         // PC at panic
    uint32_t backtrace[CRASH_BT_DEPTH]; // up to 8 return addresses
    uint32_t bt_depth;           // valid entries in backtrace[]
    uint32_t free_heap_at_crash;
    uint32_t free_psram_at_crash;
    uint32_t uptime_at_crash_s;
    char     description[64];    // panic message / description
    uint32_t timestamp_epoch;    // RTC time if synced
};

// Called from panic handler to capture crash
__attribute__((noinline))
void capture_crash(uint32_t panic_addr,
                   const uint32_t* bt, uint32_t bt_depth,
                   const char* desc);

// Report crash (called from cmd_crash("report"))
void report_crash();

// Clear crash record
void clear_crash();

// Returns true if a crash record is valid
bool has_crash_record();

// Return pointer to crash record (for query)
const CrashRecord* get_crash_record();

} // namespace telemetry
} // namespace sigurdos
```

```cpp
// telemetry_crash.cpp — key logic pseudocode
#include "telemetry_crash.h"
#include "telemetry_protocol.h"
#include <esp_system.h>
#include <rom/ets_sys.h>        // ets_printf
#include <esp_panic.h>          // esp_set_breakpoint_if_jtag

namespace sigurdos {
namespace telemetry {

// RTC_NOINIT — survives deep sleep, reset on power-on
static RTC_NOINIT_ATTR CrashRecord s_crash_record;

static constexpr uint32_t CRASH_MAGIC       = 0xDEADBEEF;
static constexpr uint32_t CRASH_MAGIC_CLEAR = 0;

// Panic handler to install
static void IRAM_ATTR panic_handler(panic_info_t* info) {
    // 1. Fill s_crash_record
    s_crash_record.magic = CRASH_MAGIC;
    s_crash_record.reset_reason = esp_reset_reason();
    s_crash_record.panic_addr = (uint32_t)info->core_frame->pc;
    s_crash_record.free_heap_at_crash = esp_get_free_heap_size();
    s_crash_record.free_psram_at_crash = esp_get_free_internal_heap_size(); // approximate
    s_crash_record.uptime_at_crash_s = millis() / 1000;

    // 2. Extract backtrace
    // Use the built-in backtrace from the panic context
    uint32_t bt[CRASH_BT_DEPTH];
    size_t bt_depth = 0;
    // Walk frame pointer chain or use info->core_frame->backtrace
    // (ESP-IDF provides this in panic_info via print_backtrace)
    // Simplified: capture return addresses from stack frames
    for (int i = 0; i < CRASH_BT_DEPTH && i < info->core_frame->backtrace_len; i++) {
        s_crash_record.backtrace[i] = info->core_frame->backtrace[i];
        bt_depth++;
    }
    s_crash_record.bt_depth = bt_depth;

    // 3. Copy description
    const char* desc = panic_info_get_description(info);
    if (desc) {
        strncpy(s_crash_record.description, desc, sizeof(s_crash_record.description) - 1);
        s_crash_record.description[sizeof(s_crash_record.description) - 1] = '\0';
    }

    // 4. Try to emit over serial before the system resets
    ets_printf("\n*** PANIC: %s\n", desc);
    ets_printf("*** PC: 0x%08x  BT: ", info->core_frame->pc);
    for (size_t i = 0; i < bt_depth; i++) {
        ets_printf("%s0x%08x", i > 0 ? " " : "", s_crash_record.backtrace[i]);
    }
    ets_printf("\n*** Crash recorded in RTC memory. Reboot.\n");
}

void init_crash_handler() {
    // Install panic handler (ESP-IDF panic handler hook)
    esp_panic_set_handler(panic_handler);
}

void capture_crash(...) { /* see above */ }

void report_crash() {
    if (!has_crash_record()) {
        emit_record1_s(tag::CRASH, key::DESC, "no crash recorded");
        return;
    }

    const CrashRecord* cr = &s_crash_record;

    emit_tag(tag::CRASH);
    emit_sep();
    emit_kv_s(key::REASON, get_reset_reason_str(cr->reset_reason));
    emit_sep();
    emit_kv_u(key::PC, cr->panic_addr);
    emit_sep();
    emit_kv_u(key::H, cr->free_heap_at_crash);
    emit_sep();
    emit_kv_u(key::P, cr->free_psram_at_crash);
    emit_sep();
    emit_kv_u(key::T, cr->uptime_at_crash_s);
    emit_sep();
    emit_kv_s(key::DESC, cr->description);
    emit_end();

    // Emit backtrace entries
    for (uint32_t i = 0; i < cr->bt_depth; i++) {
        emit_tag(tag::BT);
        emit_sep();
        emit_kv_u(key::I, i);
        emit_sep();
        emit_kv_u(key::PC, cr->backtrace[i]);
        emit_end();
    }
}

} // namespace telemetry
} // namespace sigurdos
```

**Wire format:**
```
@crash|reason=panic_abort|pc=0x400d1234|h=123456|p=567890|t=120|desc=abort() called
@bt|i=0|pc=0x400d1234
@bt|i=1|pc=0x400d5678
@bt|i=2|pc=0x400e90ab
```

**Integration:**
- In `telemetry.h`: add `void init_crash_handler();`
- In `telemetry::init()`: call `init_crash_handler()` after crash record check
- In `main.cpp`: no change needed (already calls telemetry::init)
- In `cmd_crash("report")`: call `report_crash()`

### Gap 2: LVGL Event Queue — key `evq` never populated

**File to modify:** `src/diagnostics/telemetry.cpp`

**Implementation:**

LVGL v9 provides `lv_event_get_*` iteration APIs. We can count pending events using the LVGL internal event queue or track events via our own counter in the LVGL indev handlers.

```cpp
// In telemetry.cpp, add heartbeat collector
static uint16_t collect_lvgl_evq_depth() {
    // Approach 1: Check LVGL indev event count
    // Approach 2: Use our own incrementing counter from indev callbacks
    // Approach 3: Count lv_event_send calls by hooking into LVGL
    //
    // Best practical approach for embedded: count queued events by
    // incrementing a counter each time input is dispatched to LVGL
    // and decrementing in LVGL's ready callback.

    // For MVP, use a static counter maintained by input handlers
    extern uint16_t g_lvgl_pending_events;
    return g_lvgl_pending_events;
}

// In emit_full_heartbeat(), add:
static uint16_t evq = collect_lvgl_evq_depth();
if (evq > 0) {
    emit_sep();
    emit_kv_u(key::EVQ, evq);
}
```

**Better approach:** Add a lightweight event counter in `display.cpp` where LVGL processes input:

```cpp
// In src/hal/display.cpp, add:
#if SIGURDOS_TELEMETRY
#include "../diagnostics/telemetry.h"
static volatile uint16_t s_lvgl_pending_events = 0;

// This gets called after lv_task_handler / lv_timer_handler in display loop
void sigurdos_display_evq_tick() {
    // LVGL processes events in its timer handler
    // We can measure pending by counting indev state
    lv_indev_t* indev = lv_indev_get_next(NULL);
    uint16_t count = 0;
    while (indev) {
        if (indev->state == LV_INDEV_STATE_PR) count++;
        indev = lv_indev_get_next(indev);
    }
    s_lvgl_pending_events = count;
}
uint16_t sigurdos_display_get_evq_depth() {
    return s_lvgl_pending_events;
}
#endif
```

**Wire format update in heartbeat:**
```
@hb|t=120|h=123456|evq=3|...
```

### Gap 3: LVGL Render Count — key `rd` never used

**File to modify:** `src/diagnostics/telemetry.cpp`, `src/hal/display.cpp`

**Implementation:**

```cpp
// In display.cpp — add render counter
static uint32_t s_render_count = 0;

// Hook into LVGL flush callback:
// In lv_port_disp_init or the flush callback:
void my_disp_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map) {
#if SIGURDOS_TELEMETRY
    s_render_count++;
#endif
    // ... actual flush ...
}

// Expose for telemetry
uint32_t sigurdos_display_get_render_count() {
    return s_render_count;
}
```

**In telemetry.cpp heartbeat:**
```cpp
// In emit_full_heartbeat()
#if SIGURDOS_TELEMETRY
    emit_sep();
    emit_kv_u(key::RENDERS, sigurdos_display_get_render_count());
#endif
```

**Wire format:**
```
@hb|t=120|h=123456|rd=42|...
```

### Gap 4: Loop Timing — key `loop_us` never populated

**File to modify:** `src/diagnostics/telemetry.cpp`, `src/main.cpp`

**Implementation:**

```cpp
// In telemetry.h, add:
void report_loop_timing(uint32_t elapsed_us);

// In telemetry.cpp:
static uint32_t s_last_loop_us = 0;
static uint32_t s_max_loop_us = 0;

void report_loop_timing(uint32_t elapsed_us) {
    s_last_loop_us = elapsed_us;
    if (elapsed_us > s_max_loop_us) s_max_loop_us = elapsed_us;
}
```

**In main.cpp:**
```cpp
void loop() {
    uint32_t loop_start = micros();
    // ... existing loop body ...
    
#if SIGURDOS_TELEMETRY
    uint32_t elapsed = micros() - loop_start;
    sigurdos::telemetry::report_loop_timing(elapsed);
    sigurdos::telemetry::loop();
#endif
}
```

**In heartbeat:**
```cpp
emit_sep();
emit_kv_u(key::LOOP_US, s_last_loop_us);
emit_sep();
emit_kv_u(key::PEAK_US, s_max_loop_us);  // NEW key
```

**Add to protocol:**
```cpp
// In telemetry_protocol.h, add:
extern const char PEAK_US[];  // "peak_us" — max loop time
```

### Gap 5: Screen State Tracking

**Files to modify:**
- `src/ui/navigation.cpp` — add telemetry hook calls
- `src/diagnostics/telemetry.cpp` — add screen state collectors

**Implementation:**

```cpp
// In navigation.cpp, add include:
#if SIGURDOS_TELEMETRY
#include "../diagnostics/telemetry.h"
#endif

// In navigate_to() — right after `current = screen;`:
#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_screen_transition(
        current,         // previous screen (pushed to history)
        screen,          // new screen
        millis()         // timestamp
    );
#endif

// In go_back() — right after `current = target;`:
#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_screen_transition(
        Screen::COUNT,   // denote "back" action
        target,
        millis()
    );
#endif
```

**In telemetry.cpp:**
```cpp
static Screen s_active_screen = Screen::Home;
static Screen s_last_screen = Screen::Home;
static uint32_t s_screen_enter_ms = 0;
static uint32_t s_screen_transitions = 0;

void report_screen_transition(Screen from, Screen to, uint32_t now_ms) {
    s_last_screen = s_active_screen;
    s_active_screen = to;
    s_screen_enter_ms = now_ms;
    s_screen_transitions++;
    
    // Emit alert for screen change
    emit_tag(tag::ALERT);
    emit_sep();
    emit_kv_s(key::DESC, "screen_change");
    emit_sep();
    emit_kv_s(key::CMD, screen_name_str(to));     // "Chat", "Home", etc.
    // NEW keys for screen tracking
    emit_sep();
    emit_kv_s("from", screen_name_str(from));
    emit_end();
}
```

**Add new protocol keys:**
```cpp
// telemetry_protocol.h
extern const char SCREEN[];      // "scr" — current screen name
extern const char SCREEN_MS[];   // "sc_ms" — ms on current screen
extern const char SCREEN_CNT[];  // "sc_cnt" — total transitions
```

**Wire format:**
```
@alert|desc=screen_change|cmd=Chat|from=Home
@hb|...|scr=Chat|sc_ms=45000|sc_cnt=12|...
```

### Gap 6: Keyboard Input Logging

**Files to modify:**
- `src/hal/keyboard.cpp` — add telemetry hook on key event
- `src/diagnostics/telemetry_input.cpp` — capture and report

**Implementation:**

```cpp
// In keyboard.cpp — in sigurdos_keyboard_scan() or where key is consumed:
#if SIGURDOS_TELEMETRY
#include "../diagnostics/telemetry_input.h"
// After detecting a new key:
sigurdos::telemetry::report_key_event(key_code);
#endif
```

**In telemetry_input.cpp:**
```cpp
void report_key_event(uint8_t key_code) {
    s_last_key_code = key_code;
    s_last_key_ms = millis();
    s_key_event_count++;
    
    // Only emit @pins record for non-repeating printable keys (noise reduction)
    // Emit every 5th key event or when it's a modifier/navigation key
    if (key_code < 0x20 || key_code > 0x7E || (s_key_event_count % 5 == 0)) {
        emit_tag(tag::PINS);
        emit_sep();
        emit_kv_u("key", key_code);
        emit_sep();
        emit_kv_u("kcnt", s_key_event_count);
        emit_end();
    }
}
```

**Wire format:**
```
@pins|key=65|kcnt=23
@pins|key=0x0D|kcnt=24
```

**In heartbeat:** Include last key info:
```
@hb|...|lastkey=65|krate=12.5|...
```

### Gap 7: Touch Event Tracking

**Files to modify:**
- `src/hal/touch.cpp` — add telemetry hook
- `src/diagnostics/telemetry_input.cpp` — capture and report

**Implementation:**

```cpp
// In touch.cpp — in sigurdos_touch_loop() or sigurdos_touch_get():
#if SIGURDOS_TELEMETRY
#include "../diagnostics/telemetry_input.h"
// When a new touch event is detected (pressed state transition):
if (was_pressed != now_pressed) {
    sigurdos::telemetry::report_touch_event(x, y, now_pressed);
}
#endif
```

```cpp
void report_touch_event(int x, int y, bool pressed) {
    s_touch_count++;
    s_last_touch_x = x;
    s_last_touch_y = y;
    s_last_touch_ms = millis();
    
    // Emit @pins record for touch events (sampled — every 10th touch)
    // Or emit all on press, none on release to reduce noise
    if (pressed) {
        emit_tag(tag::PINS);
        emit_sep();
        emit_kv_s("type", "touch");
        emit_sep();
        emit_kv("x", x);
        emit_sep();
        emit_kv("y", y);
        emit_sep();
        emit_kv_u("tcnt", s_touch_count);
        emit_end();
    }
}
```

**Wire format:**
```
@pins|type=touch|x=160|y=120|tcnt=5
```

### Gap 8: Trackball Event Tracking

**Files to modify:**
- `src/hal/trackball.cpp` — add telemetry hook
- `src/diagnostics/telemetry_input.cpp` — capture and report

**Implementation:**

```cpp
// In trackball.cpp — in sigurdos_trackball_next_event() or after event dequeued:
#if SIGURDOS_TELEMETRY
#include "../diagnostics/telemetry_input.h"
sigurdos::telemetry::report_trackball_event(event);
#endif
```

```cpp
void report_trackball_event(SigurdOSTrackballEvent event) {
    if (event == SigurdOSTrackballEvent::None) return;
    
    s_trackball_count++;
    
    // Emit @pins for trackball (sampled — every 5th event or on Click)
    if (s_trackball_count % 5 == 0 || event == SigurdOSTrackballEvent::Click) {
        emit_tag(tag::PINS);
        emit_sep();
        emit_kv_s("type", "trackball");
        emit_sep();
        emit_kv_s("dir", trackball_event_name(event));
        emit_sep();
        emit_kv_u("tb_cnt", s_trackball_count);
        emit_end();
    }
}
```

**Wire format:**
```
@pins|type=trackball|dir=Up|tb_cnt=12
@pins|type=trackball|dir=Click|tb_cnt=15
```

### Gap 9: WiFi/BLE State Tracking

**Files to modify:**
- `src/diagnostics/telemetry_collectors.h` — structs
- `src/diagnostics/telemetry_collectors.cpp` — collectors
- `src/diagnostics/telemetry.cpp` — call collectors in heartbeat

**Implementation:**

```cpp
// telemetry_collectors.cpp
WifiSnapshot collect_wifi_state() {
    WifiSnapshot snap = {};
    snap.sta_connected = sigurdos::wifi_sta::isConnected();
    snap.sta_rssi = snap.sta_connected ? sigurdos::wifi_sta::getRSSI() : 0;
    snap.ota_active = sigurdos::ota::isActive();
    return snap;
}
```

**In heartbeat:**
```cpp
WifiSnapshot wifi = collect_wifi_state();
emit_sep();
emit_kv_u("wifi", wifi.sta_connected ? 1 : 0);
if (wifi.sta_connected) {
    emit_sep();
    emit_kv("rssi", wifi.sta_rssi);  // re-uses key::RSSI? Use "w_rssi" for WiFi RSSI
}
```

**New protocol keys:**
```cpp
extern const char WIFI[];      // "wifi" — 0/1
extern const char W_RSSI[];    // "w_rssi" — WiFi RSSI (dBm)
extern const char OTA[];       // "ota" — OTA active 0/1
```

### Gap 10: Temperature — ESP32 Internal Temp Sensor

**Files to modify:**
- `src/diagnostics/telemetry_collectors.cpp`

**Implementation:**

ESP32-S3 has an internal temperature sensor accessible via `temperature_sensor` HAL.

```cpp
float read_internal_temp_c() {
    // Using ESP32-S3 temperature sensor HAL
    static temperature_sensor_handle_t temp_sensor = NULL;
    if (temp_sensor == NULL) {
        temperature_sensor_config_t cfg = {
            .range_min = -10,
            .range_max = 80,
        };
        temperature_sensor_install(&cfg, &temp_sensor);
        temperature_sensor_enable(temp_sensor);
    }
    float temp_c = 0;
    temperature_sensor_get_celsius(temp_sensor, &temp_c);
    return temp_c;
}
```

**Wire format in heartbeat:**
```
@hb|...|temp=32.5|...
```

**New protocol key:**
```cpp
extern const char TEMP[];  // "temp" — temperature in °C × 10 as int (325 = 32.5°C)
```

### Gap 11: NVS Stats

**Files to modify:**
- `src/diagnostics/telemetry_collectors.cpp`

**Implementation:**

```cpp
NvsSnapshot collect_nvs_usage() {
    NvsSnapshot snap = {};
    nvs_stats_t stats;
    esp_err_t err = nvs_get_stats(NULL, &stats);
    if (err == ESP_OK) {
        snap.used_bytes = stats.used_entries * 16;  // each entry is ~16 bytes
        snap.total_bytes = stats.total_entries * 16;
        snap.entry_count = stats.used_entries;
    }
    return snap;
}
```

**Wire format:**
```
@heap|...|nvs_used=4096|nvs_total=16384|nvs_entries=32|...
```

**New protocol keys:**
```cpp
extern const char NVS_USED[];   // "nvs_used"
extern const char NVS_TOTAL[];  // "nvs_total"
```

### Gap 12: Mesh Packet Counters — Per-Channel Breakdown

**Files to modify:**
- `src/mesh/mesh_wrapper.cpp` — add per-channel counters
- `src/diagnostics/telemetry.cpp` — emit new data

**Implementation:**

```cpp
// In mesh_wrapper.cpp, extend packet stats
struct ChannelStats {
    char     name[32];
    uint32_t tx_flood;
    uint32_t tx_direct;
    uint32_t rx_flood;
    uint32_t rx_direct;
};

static constexpr int MAX_CHANNEL_STATS = 16;
static ChannelStats s_channel_stats[MAX_CHANNEL_STATS] = {};
static int s_channel_stats_count = 0;

// Called from SigurdMeshV2 message handlers
void telemetry_on_channel_rx(const char* channel_name) {
    for (int i = 0; i < s_channel_stats_count; i++) {
        if (strcmp(s_channel_stats[i].name, channel_name) == 0) {
            s_channel_stats[i].rx_flood++;
            return;
        }
    }
    if (s_channel_stats_count < MAX_CHANNEL_STATS) {
        strncpy(s_channel_stats[s_channel_stats_count].name, channel_name, 31);
        s_channel_stats[s_channel_stats_count].rx_flood = 1;
        s_channel_stats_count++;
    }
}
```

**Wire format on query:**
```
@mesh|rssi=-85|snr=82|noise=-110|pk_tx=42|pk_rx=128|... 
@mesh_chan|chan=general|rx=45|tx=12
@mesh_chan|chan=lobby|rx=83|tx=30
```

### Gap 13: Task Stack Watermarks Per Task

**Files to create:** `src/diagnostics/telemetry_collectors.cpp`

**Implementation:**

```cpp
static constexpr int MAX_TRACKED_TASKS = 16;

uint8_t collect_task_watermarks(TaskWatermark* out, uint8_t max) {
    uint8_t count = 0;
    TaskHandle_t task_array[MAX_TRACKED_TASKS];
    uint32_t task_count = uxTaskGetSystemState(task_array, MAX_TRACKED_TASKS, NULL);
    
    for (uint32_t i = 0; i < task_count && count < max; i++) {
        TaskStatus_t status;
        vTaskGetInfo(task_array[i], &status, pdTRUE, eInvalid);
        strncpy(out[count].name, status.pcTaskName, sizeof(out[0].name) - 1);
        out[count].stack_hwm = status.usStackHighWaterMark * 4; // convert words to bytes
        count++;
    }
    return count;
}
```

**Wire format (new `@task` records):**
```
@task|i=0|name=main|stack=2048
@task|i=1|name=lvgl|stack=512
@task|i=2|name=mesh|stack=768
```

### Gap 14: Display Sleep/Wake Transitions

**Files to modify:**
- `src/hal/display.cpp` — add telemetry hooks
- `src/diagnostics/telemetry.cpp` — emit @alert records

**Implementation:**

```cpp
// In display.cpp
#if SIGURDOS_TELEMETRY
#include "../diagnostics/telemetry.h"

void sigurdos_display_set_on(bool on) {
    s_display_on = on;
}

// In sigurdos_display_wake():
#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_display_wake();
#endif

// In auto-off (where display goes to sleep):
#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_display_sleep();
#endif
#endif
```

```cpp
// In telemetry.cpp
void report_display_wake() {
    s_wake_count++;
    emit_tag(tag::ALERT);
    emit_sep();
    emit_kv_s(key::DESC, "display_wake");
    emit_sep();
    emit_kv_u("wake_cnt", s_wake_count);
    emit_end();
}

void report_display_sleep() {
    s_sleep_count++;
    emit_tag(tag::ALERT);
    emit_sep();
    emit_kv_s(key::DESC, "display_sleep");
    emit_sep();
    emit_kv_u("sleep_cnt", s_sleep_count);
    emit_end();
}
```

**Wire format:**
```
@alert|desc=display_wake|wake_cnt=5
@alert|desc=display_sleep|sleep_cnt=5
```

**In heartbeat:**
```
@hb|...|disp=1|disp_w=5|disp_s=4|...
```

### Gap 15: Watchdog/TWDT State

**Files to modify:**
- `src/diagnostics/telemetry_collectors.cpp`

**Implementation:**

ESP-IDF provides Task WatchDog Timer (TWDT) status.

```cpp
bool check_wdt_triggered() {
    // ESP-IDF: esp_task_wdt_status() on each task
    // Or use a timer-based check
    // Simple approach: track if any task has been starved
    static uint32_t last_wdt_check = 0;
    uint32_t now = millis();
    if (now - last_wdt_check > 10000) {
        last_wdt_check = now;
        // Check if main loop is within expected timing
        // If s_last_loop_us > 500000 (500ms), something is stuck
        if (s_last_loop_us > 500000) {
            return true;
        }
    }
    return false;
}
```

**Wire format on alert:**
```
@alert|desc=wdt_pending|loop_us=523000
```

### Gap 16: SD Card State

**Files to modify:**
- `src/diagnostics/telemetry_collectors.cpp`

**Implementation:**

```cpp
SdCardSnapshot collect_sdcard_state() {
    SdCardSnapshot snap = {};
    snap.mounted = sigurdos_sdcard_mounted();
    if (snap.mounted) {
        snap.capacity_bytes = sigurdos_sdcard_capacity_bytes();
        snap.free_bytes = sigurdos_sdcard_free_bytes();
    }
    return snap;
}
```

**Wire format (in `@heap` or heartbeat):**
```
@hb|...|sd=1|sd_free=8192000|sd_cap=16000000|...
```

**New protocol key:**
```cpp
extern const char SD[];       // "sd" — 0/1 mounted
extern const char SD_FREE[];  // "sd_free" — free bytes
```

### Gap 17: GPS State

**Files to modify:**
- `src/diagnostics/telemetry_collectors.cpp`

**Implementation:**

```cpp
GpsSnapshot collect_gps_state() {
    GpsSnapshot snap = {};
    snap.has_fix = sigurdos_gps_has_fix();
    snap.fix_quality = sigurdos_gps_fix_quality();
    snap.satellites = sigurdos_gps_satellites();
    snap.latitude = sigurdos_gps_latitude();
    snap.longitude = sigurdos_gps_longitude();
    snap.altitude_m = sigurdos_gps_altitude_m();
    snap.speed_kn = sigurdos_gps_speed_kn();
    snap.heading = sigurdos_gps_heading();
    return snap;
}
```

**Wire format:**
```
@gps|fix=1|qual=1|sv=8|lat=51.5074|lon=-0.1278|alt=45.0|spd=0.5|hdg=180.0
```

**New tag:** `extern const char GPS[];  // "gps"`

### Gap 18: LoRa Radio State

**Files to modify:**
- `src/diagnostics/telemetry_collectors.cpp`
- `src/mesh/mesh_wrapper.cpp` — expose radio config

**Implementation:**

```cpp
LoRaSnapshot collect_lora_state() {
    LoRaSnapshot snap = {};
    // Read from mesh wrapper
    const sigurdos::NodePrefs& p = sigurdos::prefs_get();
    snap.frequency_mhz = p.freq;
    snap.bandwidth_khz = p.bw;
    snap.spreading_factor = p.sf;
    snap.coding_rate = p.cr;
    snap.tx_power_dbm = p.tx_power_dbm;
    
    // Read runtime counters from mesh
    snap.total_tx_airtime_ms = sigurdos::mesh::getTotalTxAirtimeMs();
    snap.total_rx_airtime_ms = sigurdos::mesh::getTotalRxAirtimeMs();
    snap.tx_packets = sigurdos::mesh::getNumSentFlood() + sigurdos::mesh::getNumSentDirect();
    snap.rx_packets = sigurdos::mesh::getNumRecvFlood() + sigurdos::mesh::getNumRecvDirect();
    snap.rx_errors = 0;  // Not yet exposed — add to mesh_wrapper
    snap.duty_cycle_pct = sigurdos::mesh::getRemainingTxBudget();
    return snap;
}
```

**Wire format:**
```
@radio|freq=869.525|bw=62.5|sf=10|cr=5|txp=22|air_tx=12345|air_rx=67890|crc_err=3|duty=15
```

**New tag:** `extern const char RADIO[];  // "radio"`

### Gap 19: hb-ring — Ring Buffer Implementation

**Files to create:** `src/diagnostics/telemetry_hb_ring.h/.cpp`

**Implementation:**

```cpp
// telemetry_hb_ring.h
#pragma once
#include <cstdint>

namespace sigurdos {
namespace telemetry {

static constexpr uint32_t HB_RING_SIZE = 120;  // ~120 entries

struct HbRingEntry {
    uint32_t t_s;        // uptime seconds
    uint32_t h;          // free heap
    uint32_t hm;         // min free heap
    uint32_t p;          // free PSRAM
    uint8_t  b;          // battery %
    int16_t  rssi;       // RSSI × 4
    uint16_t wt;         // widget count
    uint8_t  flags;      // diff vs full bit
};

void hb_ring_push(const HbRingEntry& entry);
bool hb_ring_get(uint32_t index, HbRingEntry* out);
uint32_t hb_ring_count();
void hb_ring_clear();

} // namespace telemetry
} // namespace sigurdos
```

```cpp
// telemetry_hb_ring.cpp — PSRAM storage with DRAM fallback
#include "telemetry_hb_ring.h"
#include <esp_heap_caps.h>

namespace sigurdos {
namespace telemetry {

static HbRingEntry* s_ring = nullptr;
static uint32_t s_ring_head = 0;  // next write position
static uint32_t s_ring_count = 0; // total entries written
static bool s_psram = false;

void hb_ring_init() {
    if (psramFound()) {
        s_ring = (HbRingEntry*)heap_caps_malloc(
            HB_RING_SIZE * sizeof(HbRingEntry), MALLOC_CAP_SPIRAM);
        s_psram = (s_ring != nullptr);
    }
    if (!s_ring) {
        s_ring = (HbRingEntry*)malloc(HB_RING_SIZE * sizeof(HbRingEntry));
    }
    memset(s_ring, 0, HB_RING_SIZE * sizeof(HbRingEntry));
}

void hb_ring_push(const HbRingEntry& entry) {
    if (!s_ring) return;
    s_ring[s_ring_head] = entry;
    s_ring_head = (s_ring_head + 1) % HB_RING_SIZE;
    s_ring_count++;
}

uint32_t hb_ring_count() { return s_ring_count; }

bool hb_ring_get(uint32_t index, HbRingEntry* out) {
    if (!s_ring || index >= s_ring_count) return false;
    uint32_t phys = s_ring_count < HB_RING_SIZE 
        ? index 
        : (s_ring_head + index) % HB_RING_SIZE;
    *out = s_ring[phys];
    return true;
}

// cmd_query("hb-ring") implementation
void cmd_hb_ring() {
    uint32_t n = hb_ring_count();
    uint32_t start_idx = n > 20 ? n - 20 : 0;  // last 20 entries
    
    emit_tag(tag::HB_RING);
    emit_sep();
    emit_kv_u(key::N, n);
    emit_end();
    
    for (uint32_t i = start_idx; i < n; i++) {
        HbRingEntry e;
        if (!hb_ring_get(i, &e)) break;
        
        emit_tag(tag::HB_RING);
        emit_sep();
        emit_kv_u(key::I, i);
        emit_sep();
        emit_kv_u(key::T, e.t_s);
        emit_sep();
        emit_kv_u(key::H, e.h);
        emit_sep();
        emit_kv_u(key::B, e.b);
        emit_sep();
        emit_kv("rssi", e.rssi / 4);
        emit_end();
    }
}

} // namespace telemetry
} // namespace sigurdos
```

**Wire format:**
```
@hb-ring|n=120
@hb-ring|i=0|t=5|h=280000|b=85|rssi=-75
@hb-ring|i=1|t=10|h=279000|b=85|rssi=-78
...
```

### Gap 20: @alert — Automatic Alerting System

**Files to modify:**
- `src/diagnostics/telemetry.cpp` — alert generation

**Implementation:**

The existing drift detection already triggers. Add more alert triggers:

```cpp
// In telemetry loop, after heartbeat:
static void check_alerts() {
    // 1. Battery critical
    uint8_t batt = sigurdos_battery_pct();
    if (batt < 15 && s_last_batt_alert > 300000) {  // don't spam, 5min cooldown
        s_last_batt_alert = now;
        emit_tag(tag::ALERT);
        emit_sep();
        emit_kv_s(key::DESC, "battery_critical");
        emit_sep();
        emit_kv_u(key::B, batt);
        emit_end();
    }
    
    // 2. Heap critically low
    uint32_t heap = ESP.getFreeHeap();
    if (heap < 32768) {  // 32KB
        emit_tag(tag::ALERT);
        emit_sep();
        emit_kv_s(key::DESC, "heap_critical");
        emit_sep();
        emit_kv_u(key::H, heap);
        emit_end();
    }
    
    // 3. Display state change
    bool disp_on = sigurdos_display_is_on();
    if (disp_on != s_last_display_on) {
        s_last_display_on = disp_on;
        emit_tag(tag::ALERT);
        emit_sep();
        emit_kv_s(key::DESC, disp_on ? "display_wake" : "display_sleep");
        emit_end();
    }
    
    // 4. Screen transition (handled in navigation hook)
    
    // 5. Loop overrun
    if (s_last_loop_us > 200000) {  // >200ms
        emit_tag(tag::ALERT);
        emit_sep();
        emit_kv_s(key::DESC, "loop_overrun");
        emit_sep();
        emit_kv_u(key::LOOP_US, s_last_loop_us);
        emit_end();
    }
}
```

### Gap 21: @bt — Backtrace Tag

**Files to modify:**
- Already exists in protocol; needs crash subsystem to emit it (see Gap 1)

**Implementation:** Covered in Gap 1 crash capture.

### Gap 22: @pins — Pin/GPIO State

**Files to modify:**
- `src/diagnostics/telemetry.cpp` — new collector

**Implementation:**

```cpp
void cmd_pins() {
    emit_tag(tag::PINS);
    emit_sep();
    emit_kv("tb_u", digitalRead(PIN_TRACKBALL_UP));
    emit_sep();
    emit_kv("tb_d", digitalRead(PIN_TRACKBALL_DOWN));
    emit_sep();
    emit_kv("tb_l", digitalRead(PIN_TRACKBALL_LEFT));
    emit_sep();
    emit_kv("tb_r", digitalRead(PIN_TRACKBALL_RIGHT));
    emit_sep();
    emit_kv("tb_btn", digitalRead(PIN_TRACKBALL_BTN));
    emit_sep();
    emit_kv("pwr", digitalRead(PIN_PERIPH_POWER));
    emit_sep();
    emit_kv("batt_adc", analogRead(PIN_BATTERY_ADC));
    emit_end();
}
```

**Wire format:**
```
@pins|tb_u=0|tb_d=0|tb_l=0|tb_r=0|tb_btn=0|pwr=1|batt_adc=2048
```

### Gap 23: @task — Per-Task Dump

**Files to modify:**
- `src/diagnostics/telemetry.cpp` — new command handler

**Implementation:**

```cpp
void cmd_task() {
    static constexpr uint32_t MAX_TASKS = 16;
    TaskStatus_t task_array[MAX_TASKS];
    uint32_t total_run_time;
    uint32_t task_count = uxTaskGetSystemState(task_array, MAX_TASKS, &total_run_time);
    
    for (uint32_t i = 0; i < task_count; i++) {
        emit_tag(tag::TASK);
        emit_sep();
        emit_kv_u(key::I, i);
        emit_sep();
        emit_kv_s("name", task_array[i].pcTaskName);
        emit_sep();
        emit_kv_u(key::STACK, task_array[i].usStackHighWaterMark * 4);
        emit_sep();
        emit_kv_u("prio", task_array[i].uxCurrentPriority);
        emit_sep();
        emit_kv("state", (int)task_array[i].eCurrentState);
        emit_end();
    }
}
```

**Wire format:**
```
@task|i=0|name=main|stack=2048|prio=1|state=1
@task|i=1|name=lv_timer|stack=512|prio=1|state=1
@task|i=2|name=mesh_rx|stack=768|prio=7|state=2
```

### Gap 24: No Command to Dump Full Device State

**Files to modify:**
- `src/diagnostics/telemetry.cpp` — new `cmd_query("full")` handler

**Implementation:**

```cpp
// In cmd_query(), add:
if (strcmp(arg, "full") == 0 || strcmp(arg, "state") == 0) {
    uint32_t n = 0;
    
    // 1. System info (@heap)
    emit_tag(tag::HEAP);
    emit_sep(); emit_kv_u(key::H, ESP.getFreeHeap());
    emit_sep(); emit_kv_u(key::HM, ESP.getMinFreeHeap());
    emit_sep(); emit_kv_u(key::P, ESP.getFreePsram());
    emit_sep(); emit_kv_u(key::PM, ESP.getMinFreePsram());
    emit_sep(); emit_kv_u(key::B, sigurdos_battery_pct());
    emit_sep(); emit_kv_u(key::MV, sigurdos_battery_mv());
    emit_sep(); emit_kv_u(key::T, uptime_s());
    emit_end(); n++;
    
    // 2. LVGL info (@lvgl)
    emit_tag(tag::LVGL);
    emit_sep(); emit_kv_u(key::WIDGETS, count_lvgl_widgets());
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    emit_sep(); emit_kv_u(key::P, mon.free_size);
    emit_sep(); emit_kv_u(key::B, mon.used_pct);
    emit_end(); n++;
    
    // 3. Mesh info (@mesh)
    emit_tag(tag::MESH);
    emit_sep(); emit_kv_u(key::RSSI, (uint32_t)sigurdos::mesh::getLastRSSI());
    emit_sep(); emit_kv_u(key::SNR, (uint32_t)(sigurdos::mesh::getLastSNR() * 10));
    emit_sep(); emit_kv_u(key::TX, sigurdos::mesh::getNumSentFlood() + sigurdos::mesh::getNumSentDirect());
    emit_sep(); emit_kv_u(key::RX, sigurdos::mesh::getNumRecvFlood() + sigurdos::mesh::getNumRecvDirect());
    emit_sep(); emit_kv_u(key::NOISE, (uint32_t)sigurdos::mesh::getNoiseFloor());
    emit_end(); n++;
    
    // 4. Radio config (@radio) — NEW
    emit_tag(tag::RADIO);
    emit_sep(); emit_kv_s("freq", prefs_get().freq);  // string format
    emit_sep(); emit_kv_u("sf", prefs_get().sf);
    emit_sep(); emit_kv_u("txp", prefs_get().tx_power_dbm);
    emit_end(); n++;
    
    // 5. Screen info (@alert style or new @screen tag)
    emit_tag("screen");
    emit_sep(); emit_kv_s("name", screen_name_str(current_screen()));
    emit_sep(); emit_kv_u("ms", millis() - s_screen_enter_ms);
    emit_sep(); emit_kv_u("cnt", s_screen_transitions);
    emit_end(); n++;
    
    // 6. GPS (@gps)
    GpsSnapshot gps = collect_gps_state();
    emit_tag(tag::GPS);
    emit_sep(); emit_kv_u("fix", gps.has_fix ? 1 : 0);
    emit_sep(); emit_kv_u("sv", gps.satellites);
    emit_end(); n++;
    
    // 7. SD card (@heap or new @sd tag)
    SdCardSnapshot sd = collect_sdcard_state();
    emit_tag("sd");
    emit_sep(); emit_kv_u("mnt", sd.mounted ? 1 : 0);
    emit_sep(); emit_kv_u("free", sd.free_bytes);
    emit_end(); n++;
    
    // 8. WiFi (@wifi)
    WifiSnapshot wifi = collect_wifi_state();
    emit_tag("wifi");
    emit_sep(); emit_kv_u("con", wifi.sta_connected ? 1 : 0);
    emit_sep(); emit_kv("rssi", wifi.sta_rssi);
    emit_sep(); emit_kv_u("ota", wifi.ota_active ? 1 : 0);
    emit_end(); n++;
    
    // 9. Crash report (if any) (@crash + @bt)
    if (has_crash_record()) {
        report_crash();
        n += 1 + get_crash_record()->bt_depth;
    }
    
    // 10. Drift status
    cmd_query("drift");  // emits 1-3 @drift records
    n += 3;
    
    return;  // skip the generic state query that follows
}
```

**Wire format — one-shot "full" dump:**
```
@ok|cmd=full|cost_us=0
@heap|h=280000|hm=260000|p=7340032|pm=7000000|b=85|mv=4100|t=120
@lvgl|wt=42|p=32000|b=45
@mesh|rssi=-75|snr=82|tx=42|rx=128|noise=-110
@radio|freq=869.525|sf=10|txp=22
@screen|name=Chat|ms=45000|cnt=12
@gps|fix=1|sv=8
@sd|mnt=1|free=8192000
@wifi|con=1|rssi=-65|ota=0
@crash|reason=panic_abort|pc=0x400d1234|...
@bt|i=0|pc=0x400d1234
@drift|metric=heap|baseline=280000|current=275000|delta=5000|trend=falling
@end|cmd=full|n=12
@ok|cmd=full|cost_us=12345
```

### Gap 25: Mesh Message Content Logging

**Files to modify:**
- `src/mesh/mesh_wrapper.cpp` — add content capture hook
- `src/diagnostics/telemetry_collectors.cpp` — storage and emission

**Implementation:**

```cpp
// In mesh_wrapper.cpp — extend PacketLogEntry with content
struct ExtendedPacketLog : public PacketLogEntry {
    char     content[128];  // first 128 chars of message content
    char     channel[32];   // channel name (for channel messages)
};

static constexpr int MAX_PACKET_LOG = 64;
static ExtendedPacketLog s_packet_log[MAX_PACKET_LOG];
static int s_packet_log_head = 0;
static int s_packet_log_count = 0;

// Called from SigurdMeshV2 when a message is received
void telemetry_on_rx_packet(const char* source, int rssi, float snr, 
                            const char* type, const char* content,
                            const char* channel) {
    int idx = s_packet_log_head % MAX_PACKET_LOG;
    ExtendedPacketLog& e = s_packet_log[idx];
    e.timestamp = millis();
    strncpy(e.source, source, sizeof(e.source) - 1);
    e.rssi = rssi;
    e.snr = snr;
    strncpy(e.type, type, sizeof(e.type) - 1);
    if (content) {
        strncpy(e.content, content, sizeof(e.content) - 1);
        e.content[sizeof(e.content) - 1] = '\0';
    } else {
        e.content[0] = '\0';
    }
    if (channel) {
        strncpy(e.channel, channel, sizeof(e.channel) - 1);
        e.channel[sizeof(e.channel) - 1] = '\0';
    } else {
        e.channel[0] = '\0';
    }
    s_packet_log_head++;
    s_packet_log_count++;
}

// Query: "query pktlog" or "query mesh" extended
int get_packet_log(ExtendedPacketLog* out, int max, int offset) {
    int available = s_packet_log_count > MAX_PACKET_LOG 
        ? MAX_PACKET_LOG : s_packet_log_count;
    if (offset >= available) return 0;
    int count = 0;
    for (int i = offset; i < available && count < max; i++) {
        int phys = s_packet_log_count < MAX_PACKET_LOG
            ? i
            : (s_packet_log_head + i) % MAX_PACKET_LOG;
        out[count] = s_packet_log[phys];
        count++;
    }
    return count;
}
```

**Wire format:**
```
@pkt|i=0|src=Alice|chan=general|rssi=-75|snr=82|type=chat|text=Hello+world!
@pkt|i=1|src=Bob|chan=lobby|rssi=-80|snr=75|type=chat|text=Hey+there
```

**New tag:** `extern const char PKT[];  // "pkt"`

---

## 4. New Telemetry Record Types & Wire Format

### Summary of All Record Types

| Tag | Purpose | When Emitted | Gaps Covered |
|-----|---------|-------------|--------------|
| `@hb` / `@hb+` | Full/diff heartbeat | Every N ms | G1-G23 data rolled up |
| `@drift` | Memory/signal drift | When threshold exceeded | Existing |
| `@alert` | Notable events | On event (screen, display, battery, loop) | G20, G14, G15 |
| `@crash` | Crash report | On query | G1 |
| `@bt` | Backtrace entry | After @crash | G21, G1 |
| `@pins` | GPIO/input state | On query + sampled input events | G22, G6, G7, G8 |
| `@task` | Per-task stack HWM | On query | G23, G13 |
| `@hb-ring` | Heartbeat history | On query | G19 |
| `@gps` | GPS fix data | On query, heartbeat (periodic) | G17 |
| `@radio` | LoRa radio config | On query, heartbeat (periodic) | G18 |
| `@screen` | Current screen | On query | G5 |
| `@pkt` | Packet content log | On query | G25 |
| `@wifi` | WiFi state | On query | G9 |
| `@sd` | SD card state | On query | G16 |
| `@nvs` | NVS usage | On query | G11 |
| `@end` | Multi-line response end | After query response | Existing |

### Heartbeat Record (Enhanced) — The "Vitals" Record

```
@hb|t=120|h=280000|hm=260000|p=7340032|pm=7000000|b=85|mv=4100|
    rssi=-75|snr=82|wt=42|evq=3|rd=128|loop_us=4520|peak_us=12340|
    scr=Chat|sc_ms=45000|sc_cnt=12|disp=1|disp_w=5|disp_s=3|
    sd=1|wifi=1|w_rssi=-65|temp=325|nvs_used=4096|nvs_total=16384
```

(The line above is shown wrapped for readability — it would be one line in the wire protocol.)

---

## 5. Protocol Extension: New Tag & Key Constants

### New Tags to Add

```cpp
// telemetry_protocol.h — additions
namespace tag {
    extern const char GPS[];       // @gps
    extern const char RADIO[];     // @radio
    extern const char SCREEN[];    // @screen
    extern const char PKT[];       // @pkt — packet content log
    extern const char WIFI[];      // @wifi
    extern const char SD[];        // @sd
    extern const char NVS[];       // @nvs
}
```

### New Keys to Add

```cpp
// telemetry_protocol.h — additions
namespace key {
    extern const char PEAK_US[];      // "peak_us" — max loop time (µs)
    extern const char SCREEN[];       // "scr" — screen name
    extern const char SCREEN_MS[];    // "sc_ms" — ms on current screen
    extern const char SCREEN_CNT[];   // "sc_cnt" — total screen transitions
    extern const char DISP[];         // "disp" — display on/off
    extern const char DISP_WAKE[];    // "disp_w" — wake count
    extern const char DISP_SLEEP[];   // "disp_s" — sleep count
    extern const char TEMP[];         // "temp" — internal temp × 10
    extern const char GPS_FIX[];      // "fix" — has fix
    extern const char GPS_SV[];       // "sv" — satellite count
    extern const char GPS_LAT[];      // "lat" — latitude
    extern const char GPS_LON[];      // "lon" — longitude
    extern const char GPS_ALT[];      // "alt" — altitude meters
    extern const char SD_MOUNT[];     // "mnt" — SD mounted
    extern const char SD_FREE[];      // "free" — SD free bytes
    extern const char WIFI_CONN[];    // "con" — WiFi connected
    extern const char W_RSSI[];       // "w_rssi" — WiFi RSSI
    extern const char RADIO_FREQ[];   // "freq" — frequency MHz
    extern const char RADIO_SF[];     // "sf" — spreading factor
    extern const char RADIO_BW[];     // "bw" — bandwidth kHz
    extern const char RADIO_TXP[];    // "txp" — TX power dBm
    extern const char RADIO_AIRTXTOTAL[]; // "air_tx" — total TX airtime ms
    extern const char RADIO_AIRRXTOTAL[]; // "air_rx" — total RX airtime ms
    extern const char RADIO_DUTY[];   // "duty" — duty cycle %
    extern const char NVS_USED[];     // "nvs_used"
    extern const char NVS_TOTAL[];    // "nvs_total"
    extern const char PKT_SRC[];      // "src" — packet source
    extern const char PKT_CHAN[];     // "chan" — packet channel
    extern const char PKT_TEXT[];     // "text" — packet message content
    extern const char TASK_NAME[];    // "name" — task name
    extern const char TASK_PRIO[];    // "prio" — task priority
    extern const char TASK_STATE[];   // "state" — task state
    extern const char INPUT_TYPE[];   // "type" — input type (touch/trackball/key)
    extern const char INPUT_DIR[];    // "dir" — trackball direction string
}
```

---

## 6. Integration Points in Existing Code

### 6.1 `src/main.cpp` — Loop Timing Hook

```cpp
void loop() {
    uint32_t loop_start = micros();    // ADD
    
    // ... existing body ...
    
#if SIGURDOS_TELEMETRY
    uint32_t elapsed = micros() - loop_start;   // ADD
    sigurdos::telemetry::report_loop_timing(elapsed);  // ADD
    sigurdos::telemetry::loop();
#endif
}
```

### 6.2 `src/ui/navigation.cpp` — Screen Transition Hook

In both `navigate_to()` and `go_back()`, add:
```cpp
#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_screen_transition(
        previous, screen, millis());
#endif
```

### 6.3 `src/hal/display.cpp` — Render Count, Display State, LVGL Event Queue

- Count flushes in the LVGL flush callback (`s_render_count++`)
- Call `telemetry::report_display_wake()` in `sigurdos_display_wake()`
- Call `telemetry::report_display_sleep()` in auto-off path
- Track `sigurdos_display_is_on()` state

### 6.4 `src/hal/keyboard.cpp` — Key Event Capture

In `sigurdos_keyboard_scan()` or `sigurdos_keyboard_get_key()`, after detecting a new key:
```cpp
#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_key_event(key_code);
#endif
```

### 6.5 `src/hal/touch.cpp` — Touch Event Capture

In `sigurdos_touch_loop()`, on press/release state change:
```cpp
#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_touch_event(x, y, pressed);
#endif
```

### 6.6 `src/hal/trackball.cpp` — Trackball Event Capture

In `sigurdos_trackball_next_event()`, after dequeue:
```cpp
#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_trackball_event(event);
#endif
```

### 6.7 `src/mesh/mesh_wrapper.cpp` — Packet Content Logging

In the message reception path (where `pollMessages` populates a new message), add:
```cpp
#if SIGURDOS_TELEMETRY
    sigurdos::telemetry::report_rx_packet(source, rssi, snr, type, text, channel);
#endif
```

### 6.8 `src/test/test_controller.cpp` — New Commands

Extend the command dispatch with:
```cpp
} else if (strcmp(cmd, "pins") == 0) {
    sigurdos::telemetry::cmd_pins(arg);
} else if (strcmp(cmd, "task") == 0) {
    sigurdos::telemetry::cmd_task(arg);
} else if (strcmp(cmd, "pktlog") == 0) {
    sigurdos::telemetry::cmd_pktlog(arg);
} else if (strcmp(cmd, "gps") == 0) {
    sigurdos::telemetry::cmd_gps(arg);
} else if (strcmp(cmd, "radio") == 0) {
    sigurdos::telemetry::cmd_radio(arg);
} else if (strcmp(cmd, "full") == 0) {
    sigurdos::telemetry::cmd_query("full");
} else if (strcmp(cmd, "wifi") == 0) {
    sigurdos::telemetry::cmd_wifi(arg);
}
```

---

## 7. The "Full Device State" Command — Agent Initial Sync

### New `query full` Command

The single most important addition for the AI agent. One command dumps the complete device state.

**Agent usage pattern:**
```
> query full
@ok|cmd=full|cost_us=0
@heap|h=280000|hm=260000|p=7340032|pm=7000000|b=85|mv=4100|t=120|nvs_used=4096|nvs_total=16384|temp=325
@lvgl|wt=42|p=32000|b=45|rd=128|evq=0
@mesh|rssi=-75|snr=82|tx=42|rx=128|noise=-110
@radio|freq=869.525|bw=62.5|sf=10|cr=5|txp=22|air_tx=12345|air_rx=67890|duty=15
@screen|name=Chat|sc_ms=45000|sc_cnt=12
@wifi|con=1|rssi=-65|ota=0
@gps|fix=1|qual=1|sv=8|lat=51.5074|lon=-0.1278|alt=45.0
@sd|mnt=1|free=8192000|cap=16000000
@crash|reason=none|desc=no crash recorded
@drift|metric=heap|baseline=280000|current=275000|delta=5000|trend=falling
@drift|metric=psram|baseline=7340032|current=7340000|delta=32|trend=stable
@drift|metric=rssi|baseline=-75|current=-78|delta=3|trend=stable
@task|i=0|name=main|stack=2048|prio=1|state=1
@task|i=1|name=lv_timer|stack=512|prio=1|state=1
@task|i=2|name=mesh_rx|stack=768|prio=7|state=2
@task|i=3|name=IDLE|stack=256|prio=0|state=2
@task|i=4|name=Tmr Svc|stack=400|prio=1|state=3
@pins|tb_u=0|tb_d=0|tb_l=0|tb_r=0|tb_btn=0|pwr=1|batt_adc=2048
@end|cmd=full|n=17
@ok|cmd=full|cost_us=12345
```

**The agent receives this as a structured response and can parse all device state in one shot.**

---

## 8. Crash Capture Subsystem

### Subsystem Architecture

```
┌─────────────────────────────┐
│      Crash Occurs           │
│  (panic_abort, null ptr,    │
│   assert, watchdog, etc.)   │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  panic_handler() (IRAM)     │
│  - Fills CrashRecord in     │
│    RTC_NOINIT memory        │
│  - Emits via ets_printf()   │
│    for serial visibility    │
│  - Returns, system resets   │
└──────────┬──────────────────┘
           │
           │   (device reboots)
           ▼
┌─────────────────────────────┐
│  telemetry::init()          │
│  - Checks CrashRecord.magic │
│  - If valid, emits @crash   │
│    + @bt lines on next boot │
│  - Clears magic after read  │
└─────────────────────────────┘
```

### ESP32-S3 Panic Handler Installation

```cpp
// In telemetry_crash.cpp
#include "esp_private/panic_reason.h"  // or <esp_panic.h>

static void IRAM_ATTR ATTR_ATTR_PANIC_ENTRY
crash_panic_handler(panic_info_t* info) {
    // Save to RTC memory
    s_crash_record.magic = CRASH_MAGIC;
    s_crash_record.reset_reason = (uint32_t)esp_reset_reason();
    
    // Extract PC
    s_crash_record.panic_addr = (uint32_t)info->core_frame->pc;
    
    // Extract backtrace (ESP-IDF provides backtrace in panic_info)
    size_t depth = 0;
    for (int i = 0; i < CRASH_BT_DEPTH; i++) {
        if (info->core_frame->backtrace[i] == 0) break;
        s_crash_record.backtrace[i] = info->core_frame->backtrace[i];
        depth++;
    }
    s_crash_record.bt_depth = depth;
    
    // Memory stats
    s_crash_record.free_heap_at_crash = esp_get_free_heap_size();
    s_crash_record.uptime_at_crash_s = millis() / 1000;
    
    // Description from panic info
    const char* desc = "unknown";
    if (info->description) desc = info->description;
    strncpy(s_crash_record.description, desc, sizeof(s_crash_record.description) - 1);
    
    // Emit to serial (ets_printf is IRAM-safe)
    ets_printf("\n*** CRASH: %s\n", s_crash_record.description);
    ets_printf("*** PC: 0x%08x  BT:", s_crash_record.panic_addr);
    for (size_t i = 0; i < depth; i++) {
        ets_printf(" 0x%08x", s_crash_record.backtrace[i]);
    }
    ets_printf("\n");
}

void init_crash_handler() {
    // Check for previous crash on boot
    if (s_crash_record.magic == CRASH_MAGIC) {
        // Crash record survived reboot — emit it
        report_crash();
        clear_crash();
    }
    
    // Install panic handler
    esp_set_breakpoint_if_jtag(false);  // ensure panic handler runs
    // ESP-IDF 5.x uses esp_panic_set_handler
    esp_panic_set_handler(crash_panic_handler);
}
```

### Why RTC_NOINIT_ATTR?

- Survives warm reboots (panic → reset)
- Cleared on power cycle (vs RTC_DATA_ATTR which survives deep sleep)
- Only 128 bytes used minimal RTC memory

---

## 9. Ring Buffer System (hb-ring)

### Architecture

```
┌─────────────────────────────────────┐
│          hb-ring Buffer             │
│  PSRAM (preferred) or DRAM fallback │
│  120 entries × 20 bytes = 2.4KB     │
├─────────────────────────────────────┤
│ [0] t=5  h=280000  b=85  rssi=-75   │
│ [1] t=10 h=279500  b=85  rssi=-78   │
│ [2] t=15 h=279000  b=84  rssi=-80   │
│ ...                                 │
│ [119] t=600 h=275000 b=80 rssi=-72  │
└─────────────────────────────────────┘
         ▲                │
         │  push on each  │
         │  heartbeat     │
         │                ▼
    ┌────────┐    ┌──────────────┐
    │ Drift  │    │  hb-ring     │
    │ Detect │    │  Query       │
    └────────┘    └──────────────┘
```

The ring buffer allows the AI agent to query the last 120 heartbeats (10 minutes at 5s intervals) and look for trends, spikes, or anomalies.

---

## 10. Priority Ordering

### Phase 1: Foundation (Day 1) — "Immediate Agent Visibility"
1. **Crash capture** (Gap 1) — RTC crash record + panic handler
2. **Full state dump** (Gap 24) — `query full` command
3. **Screen tracking** (Gap 5) — navigation hooks
4. **Loop timing** (Gap 4) — `loop_us` in heartbeat
5. **Display sleep/wake** (Gap 14) — track display transitions

### Phase 2: System Collectors (Day 2) — "Peripheral Awareness"
6. **WiFi/BLE state** (Gap 9) — WiFi STA status
7. **SD card state** (Gap 16) — mount + free space
8. **GPS state** (Gap 17) — fix + satellite count
9. **LoRa radio state** (Gap 18) — config + airtime
10. **Task watermarks** (Gap 13) — per-task stack HWM
11. **Temperature** (Gap 10) — internal temp sensor
12. **NVS stats** (Gap 11) — preferences storage usage

### Phase 3: Input Events (Day 3) — "What the User Does"
13. **Keyboard input logging** (Gap 6) — key events
14. **Touch events** (Gap 7) — touch coordinates
15. **Trackball events** (Gap 8) — direction + click
16. **LVGL event queue depth** (Gap 2) — `evq` populated
17. **LVGL render count** (Gap 3) — `rd` populated

### Phase 4: Advanced (Day 4) — "Deep Diagnostics"
18. **hb-ring buffer** (Gap 19) — heartbeat history
19. **Automatic alerting** (Gap 20) — @alert triggers
20. **@bt backtrace emission** (Gap 21) — crash subsystem
21. **@pins GPIO dump** (Gap 22) — `query pins`
22. **@task dump** (Gap 23) — `query task`
23. **Mesh packet content logging** (Gap 25) — `@pkt` records
24. **Per-channel mesh counters** (Gap 12) — channel breakdown
25. **Watchdog state** (Gap 15) — WDT detection

### Phase 5: Polish (Day 5) — "Production Ready"
- Add new build envs in `platformio.ini`
- Write tests for each new function
- Document the agent protocol
- On-device testing and tuning

---

## 11. How the AI Agent Consumes This Data — "Agent View"

### 11.1 Connection Model

The AI agent connects over USB CDC serial (115200 baud) and sends commands via the test controller. All responses are line-based `@tag|key=value|...\n` records.

### 11.2 Agent Onboarding Sequence

```
→ query full                          // Agent initial sync: dumps everything
← @heap|...@lvgl|...@mesh|...@wifi|...@gps|...@sd|...@screen|...@end|cmd=full|n=17

→ drift                               // Check for memory leaks or signal drift
← @drift|metric=heap|current=280000|delta=5000|trend=falling

→ telemetry on                        // Start periodic heartbeat stream (5s interval)
← @ok|cmd=telemetry on|cost_us=0
← @hb|t=5|h=280000|...                // Heartbeat every 5s
← @hb|t=10|h=279000|...
← @alert|desc=display_wake|...        // Alert on display state change
← @drift|metric=heap|...              // Alert on drift threshold
```

### 11.3 Agent Decision Flow

The agent maintains a mental model of device health:

1. **Initial sync** (`query full`) — Creates baseline of all 17+ state dimensions
2. **Heartbeat stream** (every 5s) — Tracks changes in vitals
3. **Drift alerts** — Memory leaks, signal degradation, battery drain
4. **Input events** — User is navigating, typing, or idle
5. **Crash reports** — Panic analysis
6. **On-demand probes** — `query lvgl`, `query heap`, `query pins` as needed

### 11.4 Agent-Friendly Data Formats

- **All timestamps**: uptime seconds (relative, monotonic) — always comparable
- **All distances**: ±dBm (RSSI), ±dB (SNR), µs (loop timing), bytes (heap)
- **All states**: 0/1 for booleans, integers for enums
- **Screen names**: Human-readable strings matching UI (`Chat`, `Home`, `Settings`)
- **Temperature**: ×10 as integer (325 = 32.5°C) — no float parsing needed

### 11.5 Example Agent Debugging Session

```
# Bug: Screen goes black and doesn't respond
→ query full
← @screen|name=Chat|sc_ms=30000|sc_cnt=12
← @hb|disp=1|...                      // Display was on
← @hb|disp=0|...                      // Now disp=0! Auto-off triggered
← @alert|desc=display_sleep|...       // Confirmed: display went to sleep

→ query heap                          // Check for memory issues
← @heap|h=250000|...                  // Normal heap — not memory related

→ telemetry off                       // Stop periodic noise
→ crash report                        // Check for crash
← @crash|desc=no crash recorded

# Agent conclusion: Display auto-off timer expired.
# User had been on Chat screen for 30s without input.
# Auto-off timeout was 30s (default).
# → Agent can guide user to Settings > Display to increase timeout.
```

---

## 12. File-by-File Implementation Checklist

### New Files to Create

| File | Phase | Purpose |
|------|-------|---------|
| `src/diagnostics/telemetry_crash.h` | P1 | Crash record struct, panic handler API |
| `src/diagnostics/telemetry_crash.cpp` | P1 | RTC crash storage, panic handler, report |
| `src/diagnostics/telemetry_hb_ring.h` | P4 | Ring buffer struct, API |
| `src/diagnostics/telemetry_hb_ring.cpp` | P4 | PSRAM/DRAM ring buffer |
| `src/diagnostics/telemetry_collectors.h` | P2 | Peripheral struct definitions |
| `src/diagnostics/telemetry_collectors.cpp` | P2 | GPS, SD, WiFi, LoRa, temp, NVS collectors |
| `src/diagnostics/telemetry_input.h` | P3 | Input event types and capture API |
| `src/diagnostics/telemetry_input.cpp` | P3 | Keyboard/touch/trackball capture |

### Existing Files to Modify

| File | Phase | Change |
|------|-------|--------|
| `src/diagnostics/telemetry.h` | All | Add new function declarations |
| `src/diagnostics/telemetry.cpp` | All | Add loop_timing, screen, display, alerts, full dump |
| `src/diagnostics/telemetry_protocol.h` | All | Add new tag and key constants |
| `src/diagnostics/telemetry_protocol.cpp` | All | Add constant string definitions |
| `src/main.cpp` | P1 | Add loop timing capture |
| `src/ui/navigation.cpp` | P1 | Add screen transition hook |
| `src/hal/display.cpp` | P1, P3 | Add render count, display state hooks |
| `src/hal/keyboard.cpp` | P3 | Add key event hook |
| `src/hal/touch.cpp` | P3 | Add touch event hook |
| `src/hal/trackball.cpp` | P3 | Add trackball event hook |
| `src/mesh/mesh_wrapper.cpp` | P2, P4 | Add packet content capture, per-channel stats |
| `src/test/test_controller.cpp` | P1-P4 | Add new command dispatches |
| `src/diagnostics/debug_cfg.h` | P1 | Review telemetry build flag defaults |
| `platformio.ini` | P5 | Add new build env variants |

---

## Appendix A: Summary of Wire Protocol Changes

### New Tags (6)
```
@gps       — GPS fix data
@radio     — LoRa radio config & counters
@screen    — Active screen info
@pkt       — Packet content log entry
@wifi      — WiFi state
@sd        — SD card state
@nvs       — NVS storage usage
```

### New Keys (28)
```
peak_us     scr         sc_ms       sc_cnt
disp        disp_w      disp_s      temp
fix         qual        sv          lat
lon         alt         spd         hd
mnt         cap         free        con
w_rssi      freq        sf          bw
txp         duty        air_tx      air_rx
nvs_used    nvs_total   src         chan
text        name        prio        state
type        dir         key         kcnt
tcnt        tb_cnt      wake_cnt    sleep_cnt
```

### New Commands (6)
```
query full      — Complete device state (agent onboarding)
pins            — GPIO state dump
task            — Per-task stack watermarks
pktlog          — Packet content log
gps             — GPS details
radio           — LoRa radio config & counters
```
