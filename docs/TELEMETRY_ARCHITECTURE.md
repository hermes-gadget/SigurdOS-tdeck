# SigurdOS T-Deck Telemetry & Agent Debug System

> Architecture document: structured telemetry, crash capture, diff-based updates,
> and interactive agent queries for Hermes AI agent integration.

**Status:** Design  
**Target firmware:** SigurdOS T-Deck (ESP32-S3, LVGL v9, MeshCore)  
**Branch:** `dev`  
**Author:** Hermes Agent  
**Date:** 2026-06-03

---

## Table of Contents

1. [Design Principles](#1-design-principles)
2. [Architecture Overview](#2-architecture-overview)
3. [Telemetry Protocol](#3-telemetry-protocol)
4. [Module Breakdown](#4-module-breakdown)
5. [Crash Capture System](#5-crash-capture-system)
6. [Agent Query Interface](#6-agent-query-interface)
7. [Periodic Heartbeat & Drift/Leak Detection](#7-periodic-heartbeat--driftleak-detection)
8. [LVGL Introspection Extensions](#8-lvgl-introspection-extensions)
9. [Mesh/Radio Introspection Extensions](#9-meshradio-introspection-extensions)
10. [Build Configuration & DRAM Budget](#10-build-configuration--dram-budget)
11. [Integration Plan](#11-integration-plan)
12. [Appendix: Serial Examples](#12-appendix-serial-examples)

---

## 1. Design Principles

| # | Principle | Rationale |
|---|-----------|-----------|
| 1 | **Machine-parseable output only** | No freeform `printf` strings. Every line is a structured record that Hermes can parse without regex heuristics. Use a simple tagged-field format (see §3). |
| 2 | **Always-on without DRAM overflow** | Must compile and link in < 200 bytes of DRAM (bss/data) beyond the release build baseline. Current `SIGURDOS_DEBUG=1` overflows by 328 bytes — the new system must fit. |
| 3 | **Diff-based to save bandwidth** | Full telemetry snapshots are expensive at 115200 baud. Send only what changed since the last poll. Full snapshots on-demand only. |
| 4 | **Crash context survives reboot** | Backtrace, registers, and last N heartbeats persist in RTC_NOINIT_ATTR / NVS so the agent can query them after a crash+restart. |
| 5 | **Queryable** | Agent sends a short command; device responds with structured data. No ambiguous output. |
| 6 | **Zero-cost when disabled** | All telemetry code compiles away to inline stubs (existing `debug.h` pattern) when the telemetry build flag is not set. |
| 7 | **PSRAM-preferring for large buffers** | Heartbeat ring buffer, packet log, and snapshot frames live in PSRAM. Only fixed-size control structures live in DRAM. |

---

## 2. Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Hermes AI Agent                            │
│       (Python script / LLM process via Serial)               │
└──────────────┬──────────────────────────────────────┘
               │ USB CDC Serial @ 115200 baud
               │ Two-way: agent sends queries, device telemetry streams
┌──────────────▼──────────────────────────────────────┐
│              Serial Protocol Layer (§3)               │
│   test_controller.cpp → dispatch("query <cmd>")      │
└──────┬──────────────────────────────────┬───────────┘
       │ query                          │ stream
┌──────▼──────────┐   ┌─────────────────▼───────────┐
│  TelemetryQuery  │   │  TelemetryEngine            │
│  (§6)            │   │  (§4.1)                     │
│  - snaphot query │   │  - 5s heartbeat tick       │
│  - diff query    │   │  - diff computation        │
│  - crash report  │   │  - ring buffer (PSRAM)      │
│  - lvgl inspect  │   │  - drift/leak detection    │
│  - radio stats   │   │  - crash log save          │
└──────────────────┘   └──────┬─────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
┌───────▼────────┐   ┌───────▼───────┐   ┌─────────▼─────────┐
│  CrashCapture   │   │  LVGLMonitor  │   │  RadioMonitor     │
│  (§5)           │   │  (§8)        │   │  (§9)             │
│  - panic handler│   │  - widget    │   │  - packet rate    │
│  - backtrace    │   │    count     │   │  - RSSI/SNR drift  │
│  - RTC_NOINIT   │   │  - render    │   │  - duty cycle     │
│  - NVS backup   │   │    time      │   │  - bus errors     │
│  - heartbeat    │   │  - event q   │   │  - airtime        │
│    ring buffer  │   │  - mem       │   │  - spurious ints  │
└─────────────────┘   └──────────────┘   └──────────────────┘
```

### Memory Layout Strategy

| Segment | Target | Contents | Size est. |
|---------|--------|----------|-----------|
| **DRAM (.bss/.data)** | Minimise | Fixed-size control state, mutexes, pointers | ~144 bytes |
| **PSRAM (heap)** | Bulk data | Heartbeat ring buffer (256 entries), crash log (4KB), LVGL snapshots | ~36 KB |
| **RTC_NOINIT** | Persist | Crash header + backtrace (32 entries of call_pc), reboot reason | ~200 bytes |
| **NVS** | Long-term | Most recent crash dump + timestamps | ~2 KB |

**DRAM budget breakdown (new code only):**

| Symbol | Type | Size | Notes |
|--------|------|------|-------|
| `s_engine` | `TelemetryEngine` instance | 48 | Static global, minimal control state |
| `s_telemetry_mutex` | `SemaphoreHandle_t` | 4 | FreeRTOS mutex |
| `s_crash_info` | `CrashInfo*` | 4 | Ptr to PSRAM allocation |
| `s_crash_rtc` | `CrashRtcHeader` | ~88 | `RTC_NOINIT_ATTR`, NOT in DRAM |
| | **Total new DRAM** | **~56** | Well under 328-byte overflow margin |

> **Why this fits:** The current `SIGURDOS_DEBUG=1` build overflows by 328 bytes
> because it links in the entire `debug.cpp` plus all the LVGL serialization
> overhead and `printf` format strings from the existing dump functions. The
> new system uses PSRAM for all variable-length data, moves format strings to
> flash (PROGMEM), and keeps only bare-minimum control structures in DRAM.

---

## 3. Telemetry Protocol

### 3.1 Wire Format

**Tagged Field Format** — every line emitted by the telemetry system follows:

```
@<tag>[|<key>=<value>]...\n
```

| Component | Description |
|-----------|-------------|
| `@` | Start of record marker (unique — no other output starts with `@`) |
| `<tag>` | Record type identifier (2–12 alphanumeric chars, no spaces) |
| `|` | Field separator |
| `<key>=<value>` | Typed key-value pairs. Values are URL-encoded if needed. `*` for null/empty. |

No freeform `printf` lines. Every telemetry emission starts with `@`.

### 3.2 Record Types

#### Heartbeat / Periodic

| Tag | Fields | Description |
|-----|--------|-------------|
| `@hb` | `t`, `h`, `hm`, `p`, `pm`, `b`, `tm`, `feat`, `tk` | Full periodic heartbeat |
| `@hb+` | `t`, `h`, `p`, `b` | Diff heartbeat (only changed fields) |
| `@heap` | `f`, `m`, `ma`, `pf`, `pm`, `pma` | Memory snapshot |
| `@stack` | `task`, `hwm` | Per-task stack high-water mark |
| `@batt` | `mv`, `pct`, `cur` | Battery status |
| `@pins` | `tb_up`, `tb_dn`, `tb_lt`, `tb_rt`, `tb_btn` | GPIO pin states |
| `@lvgl` | `wt`, `rl`, `evq`, `rd`, `mem`, `rend` | LVGL render/memory stats |
| `@mesh` | `rssi`, `snr`, `noise`, `pk_tx`, `pk_rx`, `pd`, `air`, `err` | Radio/mesh state |
| `@i2c` | `dev`, `errs` | I2C bus health |
| `@spi` | `dev`, `errs` | SPI bus health |
| `@perf` | `loop_us`, `render_us`, `lora_us` | Task timing |
| `@drift` | `metric`, `baseline`, `current`, `delta`, `trend` | Drift/leak alert |

#### Crash Records

| Tag | Fields | Description |
|-----|--------|-------------|
| `@crash` | `reason`, `desc`, `pc`, `ps`, `a0`–`a3`, `sar`, `reboot_cnt` | Crash header |
| `@bt` | `i`, `pc`, `sp` | Backtrace entry (0 = crash site) |
| `@task` | `name`, `sp`, `stack_base`, `stack_size` | Task at crash time |
| `@hb-ring` | `i`, `t`, `h`, `p`, `b`, `rssi`, `wt`, `evq`, `loop_us` | Heartbeat ring buffer dump |

#### Query Responses

| Tag | Fields | Description |
|-----|--------|-------------|
| `@ok` | `cmd`, `cost_us` | Query acknowledged, response follows |
| `@err` | `cmd`, `desc` | Query rejected |
| `@data` | `key`, `val`... | Multi-line data response |
| `@end` | `cmd`, `n` | End of multi-line response |

### 3.3 Diff-Based Updates

The agent enables diff mode via `telemetry diff 1`. In diff mode, the device:

1. Maintains a **previous snapshot** struct (in DRAM, ~80 bytes).
2. On each heartbeat tick, compares current values against snapshot.
3. Emits `@hb+` (diff) instead of `@hb` (full) when only a subset of fields changed.
4. Emits `@hb` (full) every N ticks (configurable, default 12 = every 60s) as a sync point.

**Diff comparison rules:**

| Field | Diff strategy | Notes |
|-------|---------------|-------|
| `t` (uptime) | Always included | Needed for timestamp alignment |
| `h` (free heap) | ±512 bytes threshold | Avoids noise from normal allocation |
| `hm` (min heap) | Only if changed | |
| `p` (free psram) | ±1024 bytes threshold | |
| `b` (battery %) | ±2% threshold | |
| `rssi` | ±3 dBm threshold | |
| `snr` | ±1.0 dB threshold | |
| `wt` (widget count) | Only if changed | |
| `evq` (event queue) | Only if changed | |
| Task stack HWM | Only if changed | |

### 3.4 State Snapshot Struct

```cpp
// In PSRAM (reduces DRAM pressure)
struct TelemetrySnapshot {
    uint32_t uptime_s;
    uint32_t free_heap;
    uint32_t min_heap;
    uint32_t max_alloc_heap;
    uint32_t free_psram;
    uint32_t min_psram;
    uint32_t max_alloc_psram;
    uint16_t batt_mv;
    uint8_t  batt_pct;
    int16_t  last_rssi;       // dBm * 4 (fixed-point)
    int16_t  last_snr;        // SNR * 4
    int16_t  noise_floor;     // dBm * 4
    uint16_t widget_count;
    uint16_t lvgl_event_queue_depth;
    uint32_t total_renders;
    uint32_t total_loop_us;
    uint32_t total_lora_us;
    uint32_t mesh_tx_packets;
    uint32_t mesh_rx_packets;
    uint32_t mesh_rx_errors;
    uint32_t lvgl_mem_free;
    uint8_t  lvgl_mem_frag_pct;
    uint8_t  lvgl_mem_used_pct;
    uint16_t task_count;
    uint8_t  tb_up : 1, tb_dn : 1, tb_lt : 1, tb_rt : 1, tb_btn : 1;
};
```

---

## 4. Module Breakdown

### 4.1 New Files

| File | Purpose | DRAM cost |
|------|---------|-----------|
| `src/diagnostics/telemetry.h` | Public API + inline stubs | 0 (if `SIGURDOS_TELEMETRY=0`) |
| `src/diagnostics/telemetry.cpp` | TelemetryEngine implementation | ~48 bytes (static instance) |
| `src/diagnostics/telemetry_protocol.h` | Wire format helpers, tag constants, serialisation | 0 |
| `src/diagnostics/telemetry_protocol.cpp` | Key-value serialiser, diff comparator | 0 |
| `src/diagnostics/crash_capture.h` | Public API for crash handler | 0 |
| `src/diagnostics/crash_capture.cpp` | panic handler, RTC storage, backtrace walker | ~12 bytes (semaphore) |
| `src/diagnostics/monitor_lvgl.h` | LVGL introspection API | 0 |
| `src/diagnostics/monitor_lvgl.cpp` | Widget counter, event queue depth, render timer | 0 |
| `src/diagnostics/monitor_radio.h` | Radio/mesh monitor API | 0 |
| `src/diagnostics/monitor_radio.cpp` | Packet rate, RSSI drift, bus health | 0 |

### 4.2 Modified Files

| File | Change | Reason |
|------|--------|--------|
| `src/diagnostics/debug.h` | Add `telemetry_` stubs alongside existing stubs | Coexistence with new system |
| `src/diagnostics/debug.cpp` | Deprecate in favour of telemetry; or gate behind `SIGURDOS_TELEMETRY=0` | Old system still used for per-feature debug |
| `src/diagnostics/debug_cfg.h` | Add `SIGURDOS_TELEMETRY` flag, `SIGURDOS_TELEMETRY_HEARTBEAT_MS` | Build config |
| `src/test/test_controller.cpp` | Add `telemetry`, `query`, `crash`, `heap`, `lvgl-dump`, `radio-stats`, `drift` commands | Agent query interface |
| `src/test/test_controller.h` | Declare `sigurdos_test_controller_exec()` for agent use | Already exists |
| `src/main.cpp` | Call `telemetry_init()` and `telemetry_loop()` in boot sequence | Integration |
| `platformio.ini` | New env `SigurdOS_TDeck_telemetry`, update `SigurdOS_TDeck_debug` | Build config |
| `src/hal/battery.h/cpp` | Add `sigurdos_battery_current_ma()` if available | Current draw monitoring |

### 4.3 Key Data Structures

**TelemetryEngine (PSRAM):**
```cpp
struct TelemetryEngine {
    // ── Configuration ──
    uint16_t heartbeat_ms;           // default 5000
    uint8_t  full_sync_interval;     // every N ticks, send full @hb (default 12)
    bool     diff_enabled;           // agent sets via "telemetry diff 1|0"
    uint8_t  features_mask;          // bitmask: which subsystems are monitored
    
    // ── Snapshot & diff state ──
    TelemetrySnapshot current;       // current tick values
    TelemetrySnapshot previous;      // for diff comparison
    
    // ── Ring buffer (PSRAM) ──
    HeartbeatEntry* ring;            // circular buffer, 256 entries
    uint16_t ring_head;              // next write index
    uint16_t ring_count;             // entries in ring
    uint32_t ring_last_tick_ms;      // last write time
    
    // ── Timing ──
    uint32_t last_tick_ms;
    uint32_t tick_count;
    uint32_t loop_start_us;
    uint32_t total_loop_us;
    uint32_t loop_samples;
    
    // ── Drift detectors ──
    DriftDetector heap_drift;        // tracks heap decline over window
    DriftDetector psram_drift;
    DriftDetector rssi_drift;
    DriftDetector stack_drift[8];    // per-task (max 8 tasks tracked)
};
```

**HeartbeatEntry (PSMRAM ring buffer):**
```cpp
struct HeartbeatEntry {
    uint32_t tick_ms;       // timestamp at capture
    uint32_t free_heap;
    uint32_t free_psram;
    uint8_t  batt_pct;
    int16_t  rssi;          // dBm * 4
    uint16_t widget_count;
    uint16_t lvgl_event_queue_depth;
    uint32_t loop_us;       // last loop duration in µs
    uint32_t mesh_tx_packets;
    uint32_t mesh_rx_packets;
    uint16_t stack_hwm[4];  // HWM for up to 4 key tasks
};
// 4 bytes + 2*4 + 1 + 2 + 2 + 2 + 4 + 4 + 4 + 4*2 = 39 bytes
// 256 entries = ~10 KB PSRAM
```

**DriftDetector:**
```cpp
struct DriftDetector {
    int32_t baseline;       // value at window start
    int32_t current;        // latest value
    int32_t min;            // min in window
    int32_t max;            // max in window
    uint32_t window_start_ms;
    uint32_t window_duration_ms; // default 60000 (60s window)
    uint8_t  trend;         // 0=stable, 1=rising, 2=falling, 3=erratic
    
    // Returns delta = baseline - current (+ve = leak)
    int32_t delta() const { return baseline - current; }
    bool is_leak(int32_t threshold) const { return delta() > threshold; }
};
```

**CrashRtcHeader (RTC_NOINIT_ATTR, survives reboot):**
```cpp
struct RTC_NOINIT_ATTR CrashRtcHeader {
    uint32_t magic;          // 0xDEADBEEF (valid indicator)
    uint32_t crash_count;    // total crashes seen
    uint32_t reboot_count;   // total reboots seen (incremented on boot)
    esp_reset_reason_t reset_reason;
    uint32_t crash_time_ms;  // uptime at crash
    uint32_t backtrace_len;  // number of valid backtrace entries
    uint32_t backtrace[32];  // PC values from backtrace
    uint32_t excause;        // EXCCAUSE register
    uint32_t epc1;           // EPC1 register
    uint32_t epc2;           // EPC2 register
    uint32_t epc3;           // EPC3 register
    uint32_t excvaddr;       // EXCVADDR register
    uint32_t depc;           // DEPC register
    uint32_t a0, a1, a2, a3; // key CPU registers at crash
    uint32_t ps;             // PS register
    uint32_t sar;            // SAR register
    char     task_name[16];  // name of crashed task
    uint32_t crc32;          // integrity check
};
// ~200 bytes total
```

---

## 5. Crash Capture System

### 5.1 Architecture

The crash capture system intercepts ESP32-S3 panics (exceptions, abort(), assertions)
and preserves context across reboot using three tiers of storage:

```
Panic/Exception
    │
    ▼
┌──────────────────────────┐
│  esp_panic_handler()      │
│  (installed via hook)     │
│                           │
│  1. FreeRTOS task info     │
│  2. Xtensa backtrace walk │
│  3. Save to RTC_NOINIT     │
│  4. Save copy to NVS       │
│  5. Save heartbeat ring    │
│     to NVS                 │
│  6. LED SOS blink          │
│  7. esp_restart()          │
└──────────────────────────┘
```

**Why RTC_NOINIT + NVS two-tier:**

| Tier | Location | Persistence | Size | Speed | Purpose |
|------|----------|-------------|------|-------|---------|
| Primary | `RTC_NOINIT_ATTR` | Reset, not deep sleep | ~200 B | Instant | Immediate crash data, always available after reboot |
| Backup | NVS partition | Permanent | ~4 KB | Slower | Full heartbeat ring + long-term crash log |

On boot, the telemetry system checks `RTC_NOINIT_ATTR` magic word. If valid, it
reads the crash header, logs an `@crash` record to serial, then optionally
writes the full ring buffer to NVS for agent query.

### 5.2 ESP32-S3 Core Dump Integration

ESP-IDF has built-in core dump support (`CONFIG_ESP_COREDUMP_TO_FLASH` or
`CONFIG_ESP_COREDUMP_TO_UART`). For maximum compatibility:

1. **Preferred path:** Enable `CONFIG_ESP_COREDUMP_TO_FLASH` in sdkconfig.
   On boot, use `esp_core_dump_image_get()` to retrieve the saved dump.
   The telemetry system extracts backtrace + registers from it.
2. **Fallback path (no sdkconfig):** Install a custom `esp_panic_handler()` via
   `esp_set_panic_handler()`. Walk the Xtensa backtrace using the ABI-defined
   frame pointer chain.
3. **Lightweight path:** Use `__attribute__((section(".rtc_noinit")))` for quick
   data on reboot without the full core dump overhead.

**Recommended approach for DRAM-constrained builds:**
- Enable core dump to flash (not UART — that uses memory for formatting).
- On boot, call `esp_core_dump_image_get()` once.
- If valid, extract the backtrace and registers, store summary in RTC_NOINIT,
  then erase the core dump image to prevent re-triggering.
- Do NOT link in the full `esp_core_dump_elf` library, which is large.

### 5.3 Custom Panic Handler (Lightweight)

For builds without sdkconfig/CoreDump:

```cpp
// crash_capture.cpp
#include "esp_private/panic_reason.h"  // or use xtensa-debug-module registers directly

extern "C" void __real_esp_panic_handler(void* info);
extern "C" void __wrap_esp_panic_handler(void* info) {
    // Save backtrace from Xtensa debug registers
    XtensaFrame* frame;
    asm volatile("mov %0, a0\n" : "=r"(frame));
    
    CrashRtcHeader* h = get_crash_rtc();
    h->magic = 0xDEADBEEF;
    h->crash_time_ms = millis();
    h->reset_reason = esp_reset_reason();
    h->backtrace_len = extract_backtrace(h->backtrace, 32);
    
    // Save CPU registers from panic info
    panic_info_t* p = (panic_info_t*)info;
    h->excause = p->excause;
    h->epc1 = p->epc1;
    // ... etc
    
    // Copy heartbeat ring (from PSRAM) to RTC if space allows
    // or prepare for NVS write
    
    // Call original handler (or just restart)
    esp_restart();
}
```

But **linker wrapping** directly in Arduino is fragile. A simpler approach:

```cpp
// In setup(), register as late panic handler
esp_panic_handler_t prev = esp_set_panic_handler(my_panic_handler);
// my_panic_handler does both original work and our save
```

### 5.4 Xtensa Backtrace Extraction

```cpp
static uint32_t extract_backtrace(uint32_t* out, uint32_t max) {
    // Walk Xtensa ABI frame chain:
    // - a0 = return address
    // - a1 = stack frame pointer
    // Frame layout: { a0, a1, ...saved regs... }
    uint32_t count = 0;
    uint32_t sp;
    uint32_t pc;
    
    asm volatile("mov %0, a0\n" : "=r"(pc));
    asm volatile("mov %0, a1\n" : "=r"(sp));
    
    while (count < max && sp >= 0x3FCC0000 && sp <= 0x3FCE0000) {
        out[count++] = pc;
        pc = *(uint32_t*)(sp);
        sp = *(uint32_t*)(sp + 4);
        if (pc == 0 || pc == 0xDEAD) break;
    }
    return count;
}
```

### 5.5 Boot-Time Crash Report

In `telemetry_init()`:

```cpp
void telemetry_init() {
    CrashRtcHeader* h = (CrashRtcHeader*)CRASH_RTC_ADDR;
    if (h->magic == 0xDEADBEEF) {
        // Validate CRC
        if (validate_crc32(h)) {
            // Emit crash report to serial
            telemetry_emit("@crash|reason=%d|desc=%s|bt_len=%u|count=%u",
                h->reset_reason, reset_reason_str(h->reset_reason),
                h->backtrace_len, h->crash_count);
            for (uint32_t i = 0; i < h->backtrace_len; i++) {
                telemetry_emit("@bt|i=%u|pc=0x%08x", i, h->backtrace[i]);
            }
            // Emit saved heartbeat ring entries around crash time
            // ...
            
            // Save to NVS for long-term persistence
            save_crash_to_nvs(h);
            
            // Clear magic to prevent re-report
            h->magic = 0;
        }
    }
}
```

---

## 6. Agent Query Interface

### 6.1 New Test Controller Commands

The existing `test_controller.cpp` dispatch loop is extended with the following
commands, all prefixed with `telemetry` or direct query verbs:

| Command | Args | Description | Response |
|---------|------|-------------|----------|
| `telemetry on|off` | — | Enable/disable telemetry streaming | `@ok|cmd=telemetry` |
| `telemetry level <1-3>` | level | Set verbosity (1=minimal, 2=normal, 3=verbose) | `@ok|cmd=telemetry.lev` |
| `telemetry diff 1|0` | on/off | Enable/disable diff-based updates | `@ok|cmd=telemetry.diff` |
| `telemetry freq <ms>` | interval | Set heartbeat interval in ms (min 1000) | `@ok|cmd=telemetry.freq` |
| `query state` | — | Full device state snapshot (one-time) | `@hb` + `@heap` + `@batt` + `@mesh` + ... + `@end` |
| `query heap` | — | Memory state snapshot | `@heap` + `@end` |
| `query batt` | — | Battery state | `@batt|...` + `@end` |
| `query mesh` | — | Radio/mesh state | `@mesh|...` + `@end` |
| `query lvgl` | — | LVGL state | `@lvgl|...` + `@end` |
| `query crash` | — | Last crash report (survived reboot) | `@crash|...` + `@bt|...` + `@end` |
| `query hb-ring` | — | Full heartbeat ring buffer | `@hb-ring|...` × N + `@end` |
| `query i2c` | — | I2C bus health | `@i2c|...` + `@end` |
| `query drift` | — | Current drift/leak detection summary | `@drift|...` × N + `@end` |
| `query stack` | — | Per-task stack watermarks | `@stack|...` × N + `@end` |
| `query widgets` | — | Visible LVGL widget list (names, types, coords) | `@widget|...` × N + `@end` |
| `query tree` | — | Full LVGL widget tree (structured) | deprecated — use `tree` cmd |
| `drift reset` | — | Reset all drift detection baselines | `@ok|cmd=drift.reset` |
| `crash clear` | — | Clear saved crash data from RTC/NVS | `@ok|cmd=crash.clear` |

### 6.2 Response Format for Queries

All query responses follow the same pattern:

```
@ok|cmd=<command>|cost_us=<microseconds>
@data|key=value|...
@data|key2=value2|...
@end|cmd=<command>|n=<count
```

### 6.3 Agent Integration Example

```
# Agent queries device state:
> query state
< @ok|cmd=state|cost_us=312
< @hb|t=8452|h=142536|hm=128904|p=6123456|pm=5987234|b=87|tm=34.2|tk=rllrd
< @heap|f=142536|m=128904|ma=65536|pf=6123456|pm=5987234|pma=2097152
< @batt|mv=4123|pct=87
< @mesh|rssi=-89|snr=8.5|noise=-112|tx=142|rx=3891|err=3
< @lvgl|wt=47|rl=0|evq=0|rd=5|mem_free=48236|rend=18923
< @perf|loop_us=4231|render_us=1823|lora_us=892
< @stack|task=main|hwm=2048|task=lvgl|hwm=1024|task=mesh|hwm=512
< @drift|metric=heap|baseline=145000|current=142536|delta=2464|trend=falling
< @end|cmd=state|n=9
```

### 6.4 Agent Query via `test_controller_exec()`

The existing `sigurdos_test_controller_exec()` function already accepts arbitrary
command strings. The agent (Hermes) can call:

```cpp
// Hermes-side equivalent: send "query state\n" over Serial
sigurdos_test_controller_exec("query state");
```

No changes needed to the Hermes integration layer — same serial channel, same
dispatch. The agent just learns the new command vocabulary.

---

## 7. Periodic Heartbeat & Drift/Leak Detection

### 7.1 Heartbeat Engine

**File:** `src/diagnostics/telemetry.cpp`

```cpp
void TelemetryEngine::tick() {
    uint32_t now = millis();
    if (now - last_tick_ms < (uint32_t)heartbeat_ms) return;
    last_tick_ms = now;
    tick_count++;
    
    // 1. Capture current state
    capture_snapshot(&current);
    
    // 2. Store in ring buffer (always, even if not streaming)
    store_ring(current);
    
    // 3. Run drift/leak detectors
    detect_drift();
    
    // 4. Check thresholds (cross-task leak, stack HWM drops, RSSI degradation)
    check_thresholds();
    
    // 5. Emit heartbeat
    if (diff_enabled && tick_count % full_sync_interval != 0) {
        emit_diff();
    } else {
        emit_full();
    }
    
    // 6. Copy previous for next diff
    memcpy(&previous, &current, sizeof(current));
}
```

### 7.2 Drift Detection Algorithm

```cpp
void TelemetryEngine::detect_drift() {
    uint32_t now_ms = millis();
    
    // Heap drift (memory leak detection)
    if (heap_drift.current > current.free_heap) {
        // Heap decreased
        int32_t delta = heap_drift.current - current.free_heap;
        heap_drift.min = current.free_heap;
    }
    heap_drift.current = current.free_heap;
    
    // If window elapsed, check delta
    if (now_ms - heap_drift.window_start_ms >= heap_drift.window_duration_ms) {
        int32_t delta = heap_drift.baseline - current.free_heap;
        if (delta > HEAP_LEAK_THRESHOLD) {  // e.g., 4096 bytes
            telemetry_emit_alert("@drift|metric=heap|baseline=%u|current=%u|delta=%d|trend=falling",
                heap_drift.baseline, current.free_heap, delta);
            report_drift("heap", delta);
        }
        // Reset window
        heap_drift.baseline = current.free_heap;
        heap_drift.window_start_ms = now_ms;
        heap_drift.trend = (delta > 512) ? 2 : 0; // 2=falling
    }
}
```

### 7.3 Threshold Alerts

When any value crosses a configurable threshold, the engine emits an immediate
alert (not waiting for the next heartbeat tick):

| Threshold | Default | Action |
|-----------|---------|--------|
| Free heap < | 32768 | `@alert|type=low_heap|val=<h>` |
| Free PSRAM < | 262144 | `@alert|type=low_psram|val=<p>` |
| Battery < | 10% | `@alert|type=low_batt|val=<b>` |
| Stack HWM < | 256 | `@alert|type=low_stack|task=<t>|hwm=<h>` |
| Widget count > | 200 | `@alert|type=widget_leak|count=<n>` |
| Mesh errors/s | > 10 | `@alert|type=mesh_errors|rate=<r>` |
| I2C NACKs/s | > 5 | `@alert|type=i2c_errors|dev=<d>` |
| Loop time > | 50000 µs | `@alert|type=slow_loop|us=<t>` |

### 7.4 Ring Buffer Pre-Crash Preservation

The heartbeat ring buffer (256 entries × ~40 bytes ≈ 10 KB in PSRAM) provides
a moving window of the last N heartbeats. On crash:

1. The panic handler freezes the ring (stops writing).
2. The ring base address is stored in RTC_NOINIT (4 bytes).
3. On reboot, the telemetry engine reads the ring from PSRAM.
4. If PSRAM contents are lost (power cycle), the NVS backup is used instead.
5. The agent can query `query hb-ring` to see the last 256 heartbeats leading
   up to the crash, enabling trend analysis.

---

## 8. LVGL Introspection Extensions

### 8.1 Widget Counter

```cpp
// monitor_lvgl.cpp
static uint16_t count_widgets_recursive(lv_obj_t* obj) {
    if (!obj) return 0;
    uint16_t count = 1;
    uint32_t children = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < children; i++) {
        count += count_widgets_recursive(lv_obj_get_child(obj, i));
    }
    return count;
}

uint16_t monitor_lvgl_widget_count() {
    lv_obj_t* scr = lv_scr_act();
    if (!scr) return 0;
    return count_widgets_recursive(scr);
}
```

### 8.2 Event Queue Depth

```cpp
// LVGL does not expose event queue depth directly, but we can track:
uint16_t monitor_lvgl_event_queue_depth() {
    // Use sentinel: attach a temporary timer or check pending size
    // LVGL v9: lv_event_send() returns LV_RES_OK if dispatched immediately;
    // if the queue is deep, dispatch is deferred.
    // Alternative: count lv_timer_handler() iterations that return non-zero.
    return lv_disp_get_inactive_time(nullptr) > 100 ? 0 : 0; // placeholder
}
```

**Better approach:** Add a counter that increments on `lv_event_send()` and
decrements after the event callback completes. Wrap LVGL event functions or
hook into `LV_EVENT_ALL`.

### 8.3 Render Timer

```cpp
// In telemetry_loop(), record time spent in lv_timer_handler()
uint32_t render_start = micros();
lv_timer_handler();
uint32_t render_us = micros() - render_start;

// Accumulate for heartbeat reporting
engine.accumulate_render_time(render_us);
```

### 8.4 Widget Tree Query (Structured)

The existing `dump_widget_tree()` outputs human-readable text. Replace with
structured output for agent consumption:

```
# Agent query:
> query widgets
< @ok|cmd=widgets|cost_us=4521
< @widget|i=0|type=obj|x=0|y=0|w=320|h=240|visible=1
< @widget|i=1|type=label|x=10|y=5|w=100|h=16|visible=1|text="SigurdOS"
< @widget|i=2|type=btn|x=280|y=5|w=30|h=20|visible=1
< @widget|i=3|type=label|x=285|y=8|w=20|h=14|visible=1|text="X"
< @widget|i=4|type=obj|x=0|y=22|w=320|h=198|visible=1
< @widget|i=5|type=list|x=2|y=24|w=316|h=194|visible=1
< @widget|i=6|type=label|x=4|y=26|w=312|h=16|visible=1|text="Channel: general"
...
< @end|cmd=widgets|n=47
```

### 8.5 LVGL Memory Monitor

The existing `lv_mem_monitor()` call is kept and reported via `@lvgl`:

```
@lvgl|wt=47|rl=0|evq=0|rd=5|mem_free=48236|mem_used=64|mem_frag=12|rend=18923
```

---

## 9. Mesh/Radio Introspection Extensions

### 9.1 Packet Error Rate

```cpp
// monitor_radio.cpp
static uint32_t prev_tx, prev_rx, prev_errs;
static uint32_t prev_tx_air, prev_rx_air;

void monitor_radio_tick(TelemetrySnapshot* snap) {
    snap->mesh_tx_packets = sigurdos::mesh::getNumSentFlood() + 
                            sigurdos::mesh::getNumSentDirect();
    snap->mesh_rx_packets = sigurdos::mesh::getNumRecvFlood() + 
                            sigurdos::mesh::getNumRecvDirect();
    snap->mesh_rx_errors = sigurdos::mesh::getQueueDropCount();
    // airtime from mesh_wrapper
}
```

### 9.2 RSSI/SNR Drift Detection

Track RSSI and SNR over time to detect RF degradation:

```cpp
void monitor_radio_drift(DriftDetector* rssi_drift, int16_t current_rssi) {
    rssi_drift->current = current_rssi;
    uint32_t now = millis();
    if (now - rssi_drift->window_start_ms >= rssi_drift->window_duration_ms) {
        int32_t delta = rssi_drift->baseline - current_rssi;
        if (delta > RSSI_DRIFT_THRESHOLD) {  // e.g., 12 dBm
            telemetry_emit_alert("@alert|type=rssi_drift|delta=%d", delta);
        }
        rssi_drift->baseline = current_rssi;
        rssi_drift->window_start_ms = now;
    }
}
```

### 9.3 Spurious Interrupt & Bus Health Monitoring

```cpp
// In monitor_radio.cpp — query I2C/SPI bus for recent NACKs
// Requires adding error counters to the I2C/SPI driver wrappers

// I2C bus: count nacks in keyboard.cpp and touch.cpp
// SPI bus: count errors in RadioLib callbacks

struct BusHealth {
    uint32_t i2c_nacks;
    uint32_t spi_errors;
    uint32_t radio_interrupts;      // total DIO1 interrupts
    uint32_t spurious_interrupts;   // DIO1 with no pending IRQ
};
```

### 9.4 Duty Cycle Monitoring

```cpp
uint32_t duty_cycle_pct = sigurdos::mesh::getRemainingTxBudget();
// Report as @mesh|duty=<pct>
```

---

## 10. Build Configuration & DRAM Budget

### 10.1 New Build Env

**In `platformio.ini`:**

```ini
; ── Telemetry build ─────────────────────────────────
; New telemetry system: structured output, crash capture, diff-based updates.
; Designed to fit in DRAM without SIGURDOS_DEBUG=1.
; Uses PSRAM for ring buffers, RTC_NOINIT for crash persistence.
[env:SigurdOS_TDeck_telemetry]
extends = env:SigurdOS_TDeck
build_flags =
  ${env:SigurdOS_TDeck.build_flags}
  -D SIGURDOS_TELEMETRY=1
  -D SIGURDOS_TELEMETRY_HEARTBEAT_MS=5000
  -D SIGURDOS_TELEMETRY_DIFF=1
  -D SIGURDOS_TELEMETRY_RING_SIZE=256
  -D SIGURDOS_DEBUG=0          ; explicitly disable old debug system
build_src_filter =
  ${env:SigurdOS_TDeck.build_src_filter}
  +<diagnostics/telemetry*.cpp>
  +<diagnostics/crash_capture*.cpp>
  +<diagnostics/monitor_lvgl*.cpp>
  +<diagnostics/monitor_radio*.cpp>

; ── Combined telemetry + remote test ────────────────
[env:SigurdOS_TDeck_telemetry_test]
extends = env:SigurdOS_TDeck_telemetry
build_flags =
  ${env:SigurdOS_TDeck_telemetry.build_flags}
  -D SIGURDOS_REMOTE_TEST=1
build_src_filter =
  ${env:SigurdOS_TDeck_telemetry.build_src_filter}
  +<test/*.cpp>

; ── Soak telemetry (long-running drift detection) ──
[env:SigurdOS_TDeck_soak_telemetry]
extends = env:SigurdOS_TDeck_telemetry
build_flags =
  ${env:SigurdOS_TDeck_telemetry.build_flags}
  -D SIGURDOS_TELEMETRY_HEARTBEAT_MS=10000
  -D SIGURDOS_TELEMETRY_DIFF=1
  -D SIGURDOS_SOAK_HEARTBEAT=1
  -D SIGURDOS_TELEMETRY_RING_SIZE=512
```

### 10.2 DRAM Budget Analysis

**Baseline `SigurdOS_TDeck` (release):**
```
.dram0.dummy:    62,524 bytes
.dram0.data:     23,788 bytes
.dram0.bss:     259,200 bytes
.dram0.heap_start: 0
Total DRAM:     345,512 bytes
```

**Current `SigurdOS_TDeck_debug` (overflows by 328 bytes):**
The `SIGURDOS_DEBUG=1` build adds:
- Full `debug.cpp` with all dump functions
- All Serial.printf format strings (in `.rodata`, some in `.data`)
- lvgl debug hooks, ESP-IDF log infrastructure
- Additional `.bss` for LVGL debug monitors

Total DRAM exceeds allocation → linker fails.

**New `SigurdOS_TDeck_telemetry` (fits):**

| Component | DRAM delta | Explanation |
|-----------|------------|-------------|
| `TelemetryEngine` static | +48 | 3 pointers + 3 uint32_t + 2 uint8_t |
| `TelemetrySnapshot` × 2 | +80 | For current/previous diff (small struct) |
| `CrashRtcHeader` | 0 | In RTC_NOINIT, NOT in DRAM |
| Heartbeat ring (256 × ~40 B) | 0 | PSRAM allocation |
| Mutex/semaphore | +4 | FreeRTOS semaphore handle |
| Packet counters | +12 | 3 uint32_t |
| Format strings | 0 | PROGMEM `PSTR()` |
| **Total new DRAM** | **~144** | Well within 328-byte slack |

**DRAM savings over `SIGURDOS_DEBUG=1`:**
- No `#include` of lvgl.h, LovyanGFX, trackball, keyboard, touch, mesh headers → avoids ~12 KB of template instantiation
- No LV_CONF_INCLUDE_SIMPLE lvgl debug hooks (~4 KB)
- No ESP-IDF esp_log_write overhead (~2 KB)
- Format strings in PROGMEM instead of `.data` (~1 KB)
- No lv_mem_monitor_t on stack in dump functions

**Total estimated DRAM saved vs. full debug build: ~4,500+ bytes.**

### 10.3 Compile Flags

New flags defined in `debug_cfg.h`:

```cpp
// Telemetry master switch
#ifndef SIGURDOS_TELEMETRY
#define SIGURDOS_TELEMETRY 0
#endif

// Heartbeat interval in milliseconds (default 5000)
#ifndef SIGURDOS_TELEMETRY_HEARTBEAT_MS
#define SIGURDOS_TELEMETRY_HEARTBEAT_MS 5000
#endif

// Enable diff-based updates by default
#ifndef SIGURDOS_TELEMETRY_DIFF
#define SIGURDOS_TELEMETRY_DIFF 0
#endif

// Heartbeat ring buffer size (in PSRAM)
#ifndef SIGURDOS_TELEMETRY_RING_SIZE
#define SIGURDOS_TELEMETRY_RING_SIZE 256
#endif
```

---

## 11. Integration Plan

### 11.1 Phase 1: Core Protocol & Data Structures (4 files)

| Step | File | Action |
|------|------|--------|
| 1.1 | `src/diagnostics/telemetry_protocol.h` | Create: wire format constants, `emit_kv()`, `emit_tag()` helpers |
| 1.2 | `src/diagnostics/telemetry_protocol.cpp` | Create: serial output functions using PROGMEM format strings |
| 1.3 | `src/diagnostics/telemetry.h` | Create: public API shell with inline stubs for `SIGURDOS_TELEMETRY=0` |
| 1.4 | `src/diagnostics/telemetry.cpp` | Create: `TelemetryEngine` class, heartbeat tick, diff comparator |

### 11.2 Phase 2: Crash Capture (3 files)

| Step | File | Action |
|------|------|--------|
| 2.1 | `src/diagnostics/crash_capture.h` | Create: `CrashRtcHeader` struct, panic handler API |
| 2.2 | `src/diagnostics/crash_capture.cpp` | Create: RTC_NOINIT storage, backtrace walker, panic handler, NVS save |
| 2.3 | `src/diagnostics/crash_capture.cpp` | Add: boot-time crash check in `telemetry_init()` |

### 11.3 Phase 3: Monitors (2 files)

| Step | File | Action |
|------|------|--------|
| 3.1 | `src/diagnostics/monitor_lvgl.cpp` | Create: widget counter, render timer, event queue proxy |
| 3.2 | `src/diagnostics/monitor_radio.cpp` | Create: packet rate, RSSI/SNR drift, bus health counters |

### 11.4 Phase 4: Agent Query Interface (3 files)

| Step | File | Action |
|------|------|--------|
| 4.1 | `src/test/test_controller.cpp` | Add: `telemetry`, `query`, `crash`, `drift`, `hb-ring` command dispatch |
| 4.2 | `src/test/test_controller.cpp` | Add: structured `@widget` output for `query widgets` |
| 4.3 | `src/test/test_controller.h` | Add: `cmd_telemetry()`, `cmd_query()` declarations |

### 11.5 Phase 5: Build Integration (3 files)

| Step | File | Action |
|------|------|--------|
| 5.1 | `src/diagnostics/debug_cfg.h` | Add: `SIGURDOS_TELEMETRY` and related flags |
| 5.2 | `platformio.ini` | Add: `SigurdOS_TDeck_telemetry`, `SigurdOS_TDeck_soak_telemetry` envs |
| 5.3 | `src/main.cpp` | Add: `#include "diagnostics/telemetry.h"` and `telemetry_init()`/`telemetry_loop()` in `setup()`/`loop()` |

### 11.6 Phase 6: Verification & Tuning

| Step | Action |
|------|--------|
| 6.1 | Build `SigurdOS_TDeck_telemetry` — verify no overflow |
| 6.2 | Build `SigurdOS_TDeck_telemetry_test` — verify + test controller |
| 6.3 | Build `SigurdOS_TDeck_soak_telemetry` — verify with longer interval |
| 6.4 | Flash to T-Deck, verify serial output with `cat /dev/ttyACM0` |
| 6.5 | Send `query state` from serial terminal, verify structured response |
| 6.6 | Trigger crash (e.g., `abort()` or null deref test), verify reboot report |
| 6.7 | Enable diff mode, verify bandwidth reduction |

### 11.7 Modifications to Existing Code

#### `src/diagnostics/debug_cfg.h`

Add after existing feature flags:

```cpp
// ── Telemetry system ───────────────────────────────
#ifndef SIGURDOS_TELEMETRY
#define SIGURDOS_TELEMETRY 0
#endif
#ifndef SIGURDOS_TELEMETRY_HEARTBEAT_MS
#define SIGURDOS_TELEMETRY_HEARTBEAT_MS 5000
#endif
#ifndef SIGURDOS_TELEMETRY_DIFF
#define SIGURDOS_TELEMETRY_DIFF 0
#endif
#ifndef SIGURDOS_TELEMETRY_RING_SIZE
#define SIGURDOS_TELEMETRY_RING_SIZE 256
#endif
```

#### `src/diagnostics/debug.h`

Make the existing `#if defined(SIGURDOS_DEBUG)` block mutually exclusive with
`SIGURDOS_TELEMETRY` so they don't conflict (the user picks one build env):

```cpp
#if defined(SIGURDOS_DEBUG) && SIGURDOS_DEBUG
// existing debug module (full printf-based)
#elif defined(SIGURDOS_TELEMETRY) && SIGURDOS_TELEMETRY
// telemetry module uses its own init/loop stubs
#else
// existing empty stubs
#endif
```

#### `src/main.cpp`

```cpp
// Near top:
#include "diagnostics/debug_cfg.h"
#if SIGURDOS_TELEMETRY
#include "diagnostics/telemetry.h"
#endif

// In setup(), after mesh init but before UI:
#if SIGURDOS_TELEMETRY
    telemetry_init();
#endif

// In loop(), at the end:
#if SIGURDOS_TELEMETRY
    telemetry_loop();
#endif
```

#### `src/test/test_controller.cpp`

Add new dispatch entries before the `else` fallback in `dispatch()`:

```cpp
} else if (strcmp(cmd, "telemetry") == 0) {
    cmd_telemetry(arg);
} else if (strcmp(cmd, "query") == 0) {
    cmd_query(arg);
} else if (strcmp(cmd, "crash") == 0) {
    cmd_crash_action(arg);
} else if (strcmp(cmd, "drift") == 0) {
    cmd_drift(arg);
```

---

## 12. Appendix: Serial Examples

### 12.1 Full Heartbeat (every 60s sync)

```
@hb|t=8452|h=142536|hm=128904|p=6123456|pm=5987234|b=87|tm=34.2|feat=3f|tk=rllrd
```

### 12.2 Diff Heartbeat (between syncs, only changed values)

```
@hb+|t=8457|h=142512|b=86
```

### 12.3 Crash Report on Boot

```
@crash|reason=4|desc=Panic|bt_len=8|count=3|reboot=12
@bt|i=0|pc=0x42008abc
@bt|i=1|pc=0x4200def0
@bt|i=2|pc=0x42012345
@bt|i=3|pc=0x42006789
@bt|i=4|pc=0x4200abcd
@bt|i=5|pc=0x4200ef01
@bt|i=6|pc=0x42002345
@bt|i=7|pc=0x42006789
@task|name=lv_timer|sp=0x3fcd1234|stack_base=0x3fcd0000|stack_size=4096
@hb-ring|i=0|t=8410|h=142536|p=6123456|b=87|rssi=-89|wt=47|evq=0|loop_us=4231
@hb-ring|i=1|t=8415|h=142512|p=6123456|b=86|rssi=-89|wt=47|evq=0|loop_us=4189
...
@hb-ring|i=8|t=8450|h=142498|p=6123000|b=86|rssi=-92|wt=47|evq=1|loop_us=12983
@end|cmd=crash|n=11
```

### 12.4 Agent Query: Memory State

```
> query heap
< @ok|cmd=heap|cost_us=45
< @heap|f=142536|m=128904|ma=65536|pf=6123456|pm=5987234|pma=2097152
< @lvgl|wt=47|mem_free=48236|mem_used=64|mem_frag=12
< @end|cmd=heap|n=3
```

### 12.5 Drift Alert

```
@drift|metric=heap|baseline=145000|current=140248|delta=4752|trend=falling
```

### 12.6 Threshold Alert

```
@alert|type=low_heap|val=28472|threshold=32768
```

---

## Summary

| Requirement | Solution |
|-------------|----------|
| Machine-parseable telemetry | `@tag|key=value` format, no freeform printf |
| Crash backtraces survive reboot | `RTC_NOINIT_ATTR` CrashRtcHeader + NVS backup |
| Leak/drift detection | DriftDetector with sliding window + configurable thresholds |
| Bandwidth-efficient updates | Diff-based `@hb+` vs full `@hb`, configurable sync interval |
| Fit in DRAM without `SIGURDOS_DEBUG=1` | ~144 bytes DRAM; ring buffers in PSRAM; stubs compile to zero |
| Interactive agent queries | Extended test_controller with `query`, `telemetry`, `crash` commands |
