# Regions — Companion Flood Scope Implementation Plan

> **For Hermes:** Execute via delegate_task subagents for complex protocol work.

**Goal:** Implement companion-side MeshCore region flood scoping — stamp outgoing floods with transport codes so region-aware repeaters contain traffic.

**Architecture:** Lightweight scope store (SPIFFS file), TransportKey integration via MeshCore library, `sendFloodScoped()` override in SigurdMeshV2, C-wrapper API, LVGL Regions screen.

**Tech Stack:** C++/PlatformIO, ESP32-S3, LVGL v9, MeshCore `TransportKey`, NVS for active region preference.

---

### Task 1: Add `active_region` field to NodePrefs

**Files:** `src/hal/prefs.h`, `src/hal/prefs.cpp`

Add `char active_region[31]` field. Default empty (wildcard/unscoped). Load/save via NVS key `"act_reg"`.

### Task 2: Create `src/mesh/regions.h` + `regions.cpp`

**New files.** Data model:
```cpp
struct SigurdRegion { char name[31]; uint8_t key[16]; };
#define SIGURD_MAX_REGIONS 8
```
Functions: `loadRegions()`, `saveRegions()` (SPIFFS `/regions.dat`), `deriveKey(name, out16)` for `#` public regions (SHA256).

### Task 3: Override `sendFloodScoped()` in SigurdMeshV2

**File:** `src/mesh/sigurd_mesh_v2.h`

Add `TransportKey _active_scope; bool _send_unscoped;` members. Override the two `sendFloodScoped()` virtuals to call `sendScopedImpl()` which stamps `transport_codes[0]` and calls `sendFlood(pkt, codes, delay)`. Adverts stay unscoped (call `sendFlood(pkt)` directly — already the case).

### Task 4: Public C API in `mesh_wrapper.h/cpp`

Add `listRegions()`, `addRegion()`, `removeRegion()`, `setActiveRegion()`, `getActiveRegion()`, `setSendUnscopedOnce()`.

### Task 5: Regions UI screen in `screens.cpp`

LVGL screen: list saved regions with active checked, "Add region" dialog (name + optional key), tap to set active, long-press to delete. "Public (unscoped)" entry. Follow theme conventions.

### Task 6: Native tests

`test/test_regions/` — golden vector for `deriveKey`, save/load round-trip, active region logic.

### Task 7: Build, device test, review, merge
