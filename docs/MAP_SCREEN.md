# Map Screen

The Map screen renders **offline map tiles** from the SD card onto an LVGL canvas, enabling fully-offline geographic awareness on the LilyGo T-Deck. It supports pan, zoom, GPS position overlay, and auto-centers on the available tile set — no network required after the tiles are downloaded.

---

## Source Files

| File | Purpose |
|------|---------|
| `src/app/map_renderer.h` | Public API — init, render, pan, zoom, reparent, deinit, pixel-to-lat/lon |
| `src/app/map_renderer.cpp` | Full implementation — Web Mercator math, tile loading, draw pipeline, tile discovery, metadata parsing |
| `src/app/tile_cache.h` | Positive/negative cache structs, LRU API, and load-budget policy |
| `src/app/tile_cache.cpp` | LRU cache plus wrap-safe missing-tile expiry/eviction |
| `src/app/lodepng_alloc.cpp` | lodepng memory allocator with PSRAM-fallback (for PNG decode) |
| `scripts/download_maps.py` | Python tile downloader — fetches PNG tiles from OSM/CyclOSM/Carto servers |
| `test/test_map/test_map.cpp` | Tile math, LRU/negative cache, budget, expiry, and path tests |

---

## UI Entry Point

The Map screen is invoked from the **MAP tile** on the home screen:

| Trigger | Function | What happens |
|---------|----------|--------------|
| Home "MAP" tile | `navigate_to(Screen::Map)` → `map_screen_show()` | Opens the offline map view with pan/zoom controls |

### `map_screen_show()` (`src/ui/screens/screen_map.cpp`)

```
┌──────────────────────────────────┐
│ ←  Map                     14:32│  ← top bar with back button, title, time
├──────────────────────────────────┤
│                                  │
│   ┌──────────┬──────────┐       │
│   │  tile    │  tile    │   +   │  ← zoom in/out buttons
│   │          │          │       │
│   ├──────────┼──────────┤   -   │  ← 32×32 pixel buttons, bottom-right
│   │          │  ┼       │       │
│   │  tile    │  (cross) │       │  ← center crosshair (#5865F2, 50% opacity)
│   └──────────┴──────────┘       │
│                                  │
├──────────────────────────────────┤
│ SigurdOS T-Deck   ▂▄▆█       72%  │  ← bottom bar: device name, signal, battery
└──────────────────────────────────┘
```

**What it does:**
1. Creates a full screen via `make_screen_full("Map")`
2. Calls `sigurdos_map_init()` — allocates the 153KB draw buffer, discovers tiles
3. Calls `sigurdos_map_reparent(scr)` — creates/attaches the LVGL canvas
4. Calls `sigurdos_map_render()` — draws the initial tile grid
5. Wires a transparent, clickable overlay for **drag-to-pan** (throttled to 200ms between renders)
6. Adds **zoom buttons** (`+` and `-`, 32×32px, bottom-right, pixel-themed)
7. Schedules up to three screen-owned 250ms warmup renders while tiles remain
   budget-deferred; navigation cancels the timer safely

---

## Map Renderer Architecture

### Initialization

```cpp
void sigurdos_map_init();
void sigurdos_map_reparent(lv_obj_t* new_parent);
void sigurdos_map_deinit();
```

1. **`sigurdos_map_init()`** — allocates the pixel draw buffer and resets caches:
   - Draw buffer: `TFT_WIDTH × TFT_HEIGHT × 2` = **320 × 240 × 2 = 153,600 bytes** (RGB565)
   - Allocates from **PSRAM** first (`MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`), with a DRAM fallback
   - Tile discovery remains deferred until the first Map screen visit
   - Sets `initialized = true`

2. **`sigurdos_map_reparent(new_parent)`** — creates or reparents the LVGL canvas:
   - Creates `lv_canvas_create(new_parent)` with the real screen as parent (not `nullptr`, which in LVGL v9 would create a separate screen object)
   - Sets canvas size to `TFT_WIDTH × TFT_HEIGHT` and attaches the RGB565 pixel buffer
   - Registers a `LV_EVENT_DELETE` callback on the parent to auto-call `sigurdos_map_deinit()`

3. **`sigurdos_map_deinit()`** — frees all resources:
   - Frees each cache entry's pixel buffer via `map_free()`
   - Frees the canvas pixel buffer
   - Nulls pointers and resets `initialized = false`

### PSRAM Draw Buffer

The canvas pixel buffer is the **primary working surface** for all map rendering:

| Detail | Value |
|--------|-------|
| Size | 153,600 bytes (320 × 240 × 2) |
| Format | RGB565 (16-bit per pixel) |
| Allocation priority | DRAM → PSRAM fallback |
| Purpose | LVGL canvas pixel buffer, written directly by `draw_tile_from_cache()` |
| Lifetime | Allocated in `sigurdos_map_init()`, freed in `sigurdos_map_deinit()` |

The `map_alloc()` / `map_free()` helpers attempt PSRAM first (for large allocations) with a DRAM fallback:

```cpp
void* map_alloc(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}
```

### Web Mercator Coordinate System

The firmware uses standard **slippy-map / Web Mercator (EPSG:3857)** tile math, identical to OpenStreetMap, Google Maps, and most web map providers.

| Function | Purpose | Formula |
|----------|---------|---------|
| `lon_to_tile_x(lon, z)` | Longitude → tile X | `(lon + 180) / 360 × 2^z` |
| `lat_to_tile_y(lat, z)` | Latitude → tile Y | `(1 − ln(tan(lat_rad) + 1/cos(lat_rad)) / π) / 2 × 2^z` |
| `tile_x_to_lon(tx, z)` | Tile X → longitude | `tx / 2^z × 360 − 180` |
| `tile_y_to_lat(ty, z)` | Tile Y → latitude | `atan(sinh(π × (1 − 2×ty / 2^z))) × 180 / π` |

**Coordinate bounds:**

| Bound | Value |
|-------|-------|
| Max latitude | 85.0511° (Web Mercator limit) |
| Min latitude | −85.0511° |
| Max longitude | 180.0° |
| Min longitude | −180.0° |
| Zoom levels | 0–18 |
| Tiles per dimension at zoom z | `2^z` |

Map state is stored in module-level globals:

```cpp
static double center_lat = 51.5074;  // London
static double center_lon = -0.1278;
static int    zoom_level = 10;
```

### Rendering Pipeline

`sigurdos_map_render()` is the core rendering function:

```
sigurdos_map_render()
├── Guard: initialized && canvas && pixels
├── lv_canvas_fill_bg() — fill with ocean blue (#0f3460)
├── lv_canvas_init_layer()
│
├── For each tile in the visible grid:
│   ├── Compute tile_x, tile_y from center + grid offset
│   ├── Clamp to zoom-level bounds [0, 2^z)
│   ├── Check tile coverage (skip tiles outside SD card bounds)
│   ├── Compute screen_x, screen_y position
│   ├── If on-screen:
│   │   ├── load_tile(zoom, tile_x, tile_y, budget)
│   │   │   ├── Check LRU cache → hit (no budget token)
│   │   │   ├── Check 30s missing-tile cache → hit (no SD access/token)
│   │   │   ├── No token → defer to the next bounded warmup render
│   │   │   └── Consume token: read PNG from SD, decode via lodepng
│   │   │         → RGBA→RGB565 conversion → store in cache
│   │   └── draw_tile_from_cache(ct, screen_x, screen_y)
│   │         → memcpy rows with screen-edge clipping
│   └── Fallback: checkerboard placeholder; confirmed missing/corrupt tiles
│         also receive a red X while budget-deferred tiles remain neutral
│
├── If no tiles loaded at all:
│   └── Draw status label ("No map tiles found" or debug summary)
│
├── Draw center crosshair (#5865F2, 50% opacity, 12px arms)
├── lv_canvas_finish_layer()
└── lv_obj_invalidate()
```

**Visible tile grid calculation:**
- Each tile is 256×256 pixels
- The display is 320×240 pixels
- Grid spans: `1 + 320/256 + 1 = 3` across, `1 + 240/256 + 1 = 2` down (plus a 1-tile margin on each side for panning)
- The render loop iterates from `tx = -1` to `tiles_across` and `ty = -1` to `tiles_down`, scanning **5 columns × 4 rows = up to 20 tile positions** per render pass
- Tiles that don't intersect the viewport (`sigurdos_map_tile_intersects_viewport`) are skipped; typically ~12 tiles are on-screen
- At most **2 uncached SD read/decode attempts** occur in one render. Positive
  cache hits and active negative-cache hits do not spend the budget.

### Tile Coverage System

Tile coverage is discovered by scanning the SD card directory structure:

```
tile_coverage[MAX_ZOOM + 1]   // one TileCoverage per zoom level
TileCoverage {
    bool valid;                // tiles exist at this zoom
    int  min_x, max_x;         // X range
    int  min_y, max_y;         // Y range
    int  sample_x, sample_y;   // center-most tile for initial view
}
```

**Discovery flow:**

1. `discover_tiles()` — scans `/sdcard/tiles/` directory for numeric zoom-level subdirectories (0–18)
2. `scan_zoom_coverage(z)` — for each zoom level:
   - Opens `/sdcard/tiles/{z}/`
   - Iterates numeric X subdirectories
   - `scan_y_range(z, x)` — reads `.png` files in each X directory, finds min/max Y
   - Determines a sample tile (closest to the center of the bounding box)
3. Sets `min_available_zoom` / `max_available_zoom` based on what's found
4. `load_metadata()` — after discovery, optionally reads `/sdcard/tiles/metadata.json`:
   - Parses the `"bounds"` array `[min_lat, min_lon, max_lat, max_lon]`
   - Overrides the initial center to the midpoint of the bounds
5. `clamp_view_to_coverage()` — prevents panning/zooming outside available tiles:
   - Clamps zoom level to available range
   - Clamps center lat/lon so at least half a tile margin remains visible

### Map Interaction API

| Function | Description |
|----------|-------------|
| `sigurdos_map_set_view(lat, lon, zoom)` | Jump to a specific coordinate and zoom |
| `sigurdos_map_get_lat()` | Get current center latitude |
| `sigurdos_map_get_lon()` | Get current center longitude |
| `sigurdos_map_get_zoom()` | Get current zoom level |
| `sigurdos_map_pan(dx, dy)` | Pan by screen pixel delta (Web Mercator-aware) |
| `sigurdos_map_zoom_in()` | Zoom in one level (clamped to coverage) |
| `sigurdos_map_zoom_out()` | Zoom out one level (clamped to coverage) |
| `sigurdos_map_pixel_to_latlon(px, py, &lat, &lon)` | Convert a screen pixel to lat/lon (for touch events) |
| `sigurdos_map_tiles_available()` | Quick check if the current view has a tile on SD |

**Pan delta math:**
```cpp
double tx = lon_to_tile_x(center_lon, zoom_level);
double ty = lat_to_tile_y(center_lat, zoom_level);
tx += dx / (double)TILE_SIZE;   // screen pixel delta → tile delta
ty += dy / (double)TILE_SIZE;
center_lon = tile_x_to_lon(tx, zoom_level);
center_lat = tile_y_to_lat(ty, zoom_level);
```

This properly handles Web Mercator's **non-linear latitude scaling** — screen y and tile y both increase downward, so the sign is consistent.

### Debug Diagnostics

When `SIGURDOS_DEBUG_MAP` is defined (in `src/diagnostics/debug_cfg.h`), the map renderer outputs:

- **Serial debug logging** — each tile load hit/miss + path, render calls, discovery output
- **Diagnostic overlay** — when no tiles load, shows a detailed status panel instead of the simple error message:
  ```
  SD:1 init:1 z:10
  cov:1 z:8-14
  load:ok 10/511/340 15234B
  root:ok tiles map
  z12:ok 1020 1021 1022
  x:1021 y:330-350
  open:ok
  ```
- **`last_tile_status`** — tracks the most recent tile load operation result (128 char buffer)

---

## Tile Cache System

### Data Structure

```cpp
struct CachedTile {
    int       zoom;       // zoom level
    int       tx;         // tile X coordinate
    int       ty;         // tile Y coordinate
    uint16_t* pixels;     // decoded RGB565 pixel data (or nullptr if empty)
    uint64_t  last_used;  // monotonic clock stamp (set by lookup)
};

static constexpr int TILE_CACHE_SIZE = 4;
static CachedTile tile_cache[TILE_CACHE_SIZE];
static uint64_t   cache_clock = 0;
```

### LRU Eviction Algorithm

```
             tile_cache[4]    (array, no dynamic allocation)
             ┌─────┬─────┬─────┬─────┐
             │  0  │  1  │  2  │  3  │
             └─────┴─────┴─────┴─────┘
                 │     │     │     │
        pixels: ptr    ptr   ptr   ptr
       last_used: 42   17   103   89    (higher = more recently used)

Lookup:   linear scan → match on (zoom, tx, ty) → stamp ++clock
Evict:    find first nullptr slot, else LRU (lowest last_used)
```

**API:**

| Function | Purpose |
|----------|---------|
| `tile_cache_init(cache, count)` | Zero-initialises all entries (`pixels = nullptr`, `last_used = 0`) |
| `tile_cache_lookup(cache, count, zoom, tx, ty, &clock)` | Linear scan. On hit: updates `last_used` to `++(*clock)`, returns entry. On miss: returns `nullptr` (clock unchanged). |
| `tile_cache_evict_slot(cache, count)` | Returns the first empty slot (`pixels == nullptr`), or the entry with the lowest `last_used` (LRU). **Does NOT free memory** — the caller must free `slot->pixels` before overwriting it. |

### Missing-tile negative cache and load budget

- `MissingTileCacheEntry[24]` stores `(zoom, x, y, failed_at_ms)` without
  allocating heap memory.
- Entries expire after 30 seconds using unsigned subtraction, so expiry remains
  correct across `millis()` wraparound. A full cache evicts its oldest failure.
- Missing, corrupt, unreadable, or allocation-failed tiles enter the cache;
  re-entering Map or rediscovering an SD tile set clears stale misses.
- `TileLoadBudget` permits two uncached file/decode attempts per render. Cache
  hits and known misses are free, while excess candidates are deferred.
- `sigurdos_map_last_load_attempts()`, `sigurdos_map_last_negative_hits()`,
  `sigurdos_map_last_deferred_tiles()`, and
  `sigurdos_map_missing_cache_count()` expose bounded diagnostics.

### Memory Budget

| Allocation | Size per entry | Total (4 entries) | Location |
|------------|---------------|-------------------|----------|
| Decoded RGB565 pixels | 256 × 256 × 2 = 131,072 bytes | **524,288 bytes (~512 KB)** | PSRAM (with DRAM fallback via `map_alloc`) |
| PNG file buffer (ephemeral) | ~15–30 KB per tile load | Allocated per-load, freed after decode | PSRAM |
| Canvas draw buffer | 320 × 240 × 2 = 153,600 bytes | **153,600 bytes (~150 KB)** | PSRAM (DRAM fallback) |

**Total map-related PSRAM usage:** ~677 KB (cache + canvas fallback + transient decode buffers)

**Cache entry lifetime:** Entries are freed in `sigurdos_map_deinit()` or when the slot is evicted and overwritten (the caller frees `slot->pixels` before `map_alloc`-ing a new buffer).

### `uint64_t` Clock — Wrap Safety

The original implementation used `uint32_t` for the cache clock, which wraps after ~49.7 days at 1 kHz tick rate. This was **fixed in PR #107** by switching to `uint64_t`:

| Property | `uint32_t` (old) | `uint64_t` (current) |
|----------|------------------|----------------------|
| Max value | 4,294,967,295 | 18,446,744,073,709,551,615 |
| Time to wrap at 1 kHz | ~49.7 days | ~584 million years |
| Impact of wrap | LRU eviction broken (wrong entry selected) | No practical wrap |

The fix also extracted the tile cache into its own reusable module (`tile_cache.h` / `tile_cache.cpp`) with its own unit tests (see [Test Coverage](#test-coverage)).

---

## Tile Loading Pipeline

### From SD Card to Screen

```
SD Card: /sdcard/tiles/{z}/{x}/{y}.png
               │
               ▼
       positive cache? → draw
       negative cache? → missing marker
       no budget token? → defer
               │
               ▼
        fopen(path, "rb")
        fseek/ftell → validate size (≤ 196 KB)
        fread → png_buf in PSRAM
               │
               ▼
        lodepng_decode_memory()
        RGBA output  →  check w=256, h=256
               │
               ▼
        RGBA → RGB565 conversion
        (r>>3)<<11 | (g>>2)<<5 | (b>>3)
               │
               ▼
        tile_cache[slot].pixels = RGB565 buffer
        slot->zoom/tx/ty = coords
        slot->last_used = ++cache_clock
               │
               ▼
        draw_tile_from_cache(tile, screen_x, screen_y)
        → memcpy row-by-row with screen-edge clipping
```

### RGBA → RGB565 Conversion

```cpp
for (int y = 0; y < TILE_SIZE; y++) {
    for (int x = 0; x < TILE_SIZE; x++) {
        int i = (y * TILE_SIZE + x) * 4;        // RGBA byte index
        uint16_t r = rgba[i] >> 3;               // 8-bit → 5-bit
        uint16_t g = rgba[i + 1] >> 2;           // 8-bit → 6-bit
        uint16_t b = rgba[i + 2] >> 3;           // 8-bit → 5-bit
        slot->pixels[y * TILE_SIZE + x] = (r << 11) | (g << 5) | b;
    }
}
```

### Screen Clipping in `draw_tile_from_cache()`

Tiles that extend beyond screen edges are clipped per-pixel:

```
  screen_x, screen_y     (tile top-left on screen)
       │
       ▼
  ┌─────────────┬──────────────┐
  │  clipped    │              │
  │  (src_x>0)  │   visible    │
  ├─────────────┤   portion    │
  │             │              │
  │  clipped    │   clipped    │
  │  (src_y>0)  │   (src)      │
  └─────────────┴──────────────┘
       ◄─── TFT_WIDTH=320 ───►

  Clamp: x1,y1 ≥ 0, x2 ≤ 319, y2 ≤ 239
  Adjust src_x/src_y/src_w/src_h accordingly
  memcpy(dst_row, src_row, src_w * sizeof(uint16_t))  // row by row
```

---

## SD Card Tile Storage

### Directory Structure

```
/sdcard/
├── tiles/                  ← root tile directory
│   ├── 8/                 ← zoom level 8
│   │   ├── 127/           ← tile X = 127
│   │   │   ├── 85.png     ← tile Y = 85
│   │   │   ├── 86.png
│   │   │   └── ...
│   │   └── 128/
│   │       ├── 85.png
│   │       └── ...
│   ├── 10/                ← zoom level 10
│   │   └── ...
│   ├── 12/                ← zoom level 12
│   │   └── ...
│   └── metadata.json      ← auto-centering metadata
└── ... (other files)
```

**Path format:**
```
/sdcard/tiles/{z}/{x}/{y}.png
```

The mountpoint is defined in `src/hal/sdcard.h`:
```cpp
#define SIGURDOS_SD_MOUNTPOINT "/sdcard"
```

### Tile Format Requirements

| Property | Requirement |
|----------|-------------|
| Format | PNG (loaded via lodepng) |
| Dimensions | 256 × 256 pixels |
| Color | RGBA (8-bit per channel) — alpha discarded during conversion |
| Max file size | 196 KB (enforced by `load_tile()`) |
| Naming | Numeric Y basename only — `12.png` accepted, `12@2x.png` rejected |
| Structure | `{z}/{x}/{y}.png` — exactly one level of X subdirectories |

The `entry_is_png_tile()` function validates filenames: they must end in `.png` (case-insensitive) and the stem must be purely numeric.

### `metadata.json`

Optional file at `/sdcard/tiles/metadata.json` that overrides the initial map center:

```json
{
    "name": "london",
    "attribution": "© OpenStreetMap contributors",
    "bounds": [51.3, -0.5, 51.7, 0.3],
    "zoom_range": [10, 14],
    "format": "png",
    "tile_size": 256
}
```

The firmware only uses the `"bounds"` array: `[min_lat, min_lon, max_lat, max_lon]`. The center is set to the midpoint of the bounds. If `metadata.json` is missing, the map auto-centers on the sample tile found during tile discovery.

### Map Download Script

`scripts/download_maps.py` downloads tiles from public tile servers:

```bash
# Download Teesside area, zooms 8-14
python3 scripts/download_maps.py --name teesside \
    --lat1 54.45 --lon1 -1.45 --lat2 54.65 --lon2 -1.05 \
    --zoom 8 14

# Quick city download
python3 scripts/download_maps.py --city london --zoom 10 14

# UK national overview (zooms 5-8, CyclOSM tiles)
python3 scripts/download_maps.py --name uk \
    --lat1 49.8 --lon1 -8.5 --lat2 58.8 --lon2 1.8 \
    --zoom 5 8 --server cyclosm
```

**Supported tile servers:**

| Server | URL | Max Zoom |
|--------|-----|----------|
| `osm` | `tile.openstreetmap.org/{z}/{x}/{y}.png` | 19 |
| `cyclosm` | `{s}.tile-cyclosm.openstreetmap.fr/cyclosm/{z}/{x}/{y}.png` | 20 |
| `carto` | `{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}.png` | 19 |

Output: `maps-{name}/tiles/{z}/{x}/{y}.png` — copy the `tiles/` directory to the SD card root.

---

## Coordinate System for T-Deck (320×240)

### Viewport Geometry

```
  ┌────────────────────────────────────────────────┐
  │  TFT_WIDTH = 320 px                            │
  │                                                │
  │  ┌──────────────────────┬──────────────────────┐│
  │  │   tile (0,-1)        │   tile (1,-1)        ││  ← rows of 256×256 tiles
  │  │   screen_x = -96     │   screen_x = 160     ││
  │  │   screen_y = -128    │   screen_y = -128    ││
  │  ├──────────────────────┼──────────────────────┤│
  │  │   tile (0,0)         │◄─ tile (1,0)         ││  ← center tile aligned
  │  │   screen_x = -96     │   screen_x = 160     ││     so center_px=160
  │  │   screen_y = 112     │   screen_y = 112     ││     center_py=120
  │  ├──────────────────────┼──────────────────────┤│
  │  │   tile (0,1)         │   tile (1,1)         ││
  │  │   screen_x = -96     │   screen_x = 160     ││
  │  │   screen_y = 368     │   screen_y = 368     ││  ← off-screen (clipped)
  │  └──────────────────────┴──────────────────────┘│
  └────────────────────────────────────────────────┘
```

**At zoom level z:**
- `1 tile = 256 × 256 pixels` (world coordinate space)
- Display = `320 × 240 pixels`
- Tiles visible at any pan position: **6 tiles minimum** (2 columns + margin × 2 rows + margin)
- Grid iterated: `tiles_across = 1 + 320/256 + 1 = 3`, `tiles_down = 1 + 240/256 + 1 = 2`
- Total per render pass: up to `(3+2) × (2+2) = 20` tiles scanned, ~12 actually on-screen
- `center_px = 160`, `center_py = 120` (center of the display)

**Tile-to-screen positioning:**
```cpp
int screen_x = center_px + (tx * TILE_SIZE) - frac(center_tx) * TILE_SIZE;
int screen_y = center_py + (ty * TILE_SIZE) - frac(center_ty) * TILE_SIZE;
```

Where `center_tx` / `center_ty` are the floating-point tile coordinates of the map center, and `frac()` is the fractional part (for sub-tile pan positioning).

### SD Card Path Structure Summary

| Path | Description |
|------|-------------|
| `/sdcard/tiles/{z}/{x}/{y}.png` | Individual map tile |
| `/sdcard/tiles/metadata.json` | Optional centering metadata |
| `/sdcard` | SD card VFS mountpoint (via SPI+FATFS, CS=GPIO 39) |

---

## Test Coverage

The map system has **28 dedicated tests** in `test/test_map/test_map.cpp` — 14 tile-math tests plus 14 LRU cache tests:

### Tile Math Tests (`class MapTest`)

| Test | What it validates |
|------|-------------------|
| `LonZeroIsMercatorCenter` | Longitude 0 → tile x at center |
| `LatZeroIsMercatorEquator` | Latitude 0 → tile y at equator |
| `MercatorIsSymmetric` | 45°N / 45°S produce symmetric y values |
| `Zoom0HasOneTile` | 2^0 × 2^0 = 1 tile at zoom 0 |
| `Zoom1HasFourTiles` | 2^1 × 2^1 = 4 tiles at zoom 1 |
| `Zoom10Has1MTiles` | 2^10 × 2^10 = 1,048,576 tiles at zoom 10 |
| `NullIslandAtZoom0` | 0,0 maps to tile (0,0) at zoom 0 |
| `LondonAtZoom10` | London is at tile (511, ~340) at zoom 10 |
| `PoleClampedToValidRange` | 90° latitude clamped to tile y = 0 |
| `TileValidInRange` | Coordinates within [0, 2^z) are valid |
| `TileInvalidOutOfRange` | Coordinates outside [0, 2^z) are invalid |
| `TilePathFormat` | `/sdcard/tiles/10/512/340.png` format |
| `CenterPixelIsCenterLatLon` | Viewport center → in ≈ out lat/lon |
| `ScreenCornerMapsToValidCoord` | Even screen corners map to valid geo coordinates |

### LRU Cache Tests (`class TileCacheTest`)

| Test | What it validates |
|------|-------------------|
| `InitClearsAllSlots` | `tile_cache_init()` zeros every entry |
| `InitIgnoresInvalidInputs` | Null cache / non-positive count are no-ops |
| `LookupEmptyCacheReturnsNull` | Miss on empty cache returns nullptr, clock unchanged |
| `LookupRejectsInvalidInputs` | Null cache/clock or bad count returns nullptr |
| `LookupReturnsEntryOnHit` | Match on all 3 coordinates returns correct pointer |
| `LookupUpdatesLastUsed` | Hit updates `last_used` to current clock value |
| `LookupNoMatchReturnsNull` | Partial match (e.g. same zoom, different tx) is a miss |
| `EvictReturnsEmptySlotFirst` | Nullptr slots are preferred over eviction |
| `EvictRejectsInvalidInputs` | Null cache / bad count returns nullptr |
| `EvictReturnsFirstNullAmongOccupied` | First null slot is returned (not last) |
| `EvictFullCacheReturnsLRU` | When full, returns entry with lowest `last_used` |
| `EvictFullCacheReturnsLRUAfterLookups` | Recent lookups correctly re-order LRU chain |
| `EvictDoesNotFreeMemory` | Evict slot does NOT set `pixels = nullptr` — caller's job |
| `SearchSmallerCache` | Works correctly with non-standard cache sizes |

---

## Related Documents

| Document | Description |
|----------|-------------|
| [`FEATURES_OVERVIEW.md`](FEATURES_OVERVIEW.md) | Feature catalog with map renderer summary |
| [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md) | Bug history — includes LRU clock uint32→uint64 fix |
| [`scripts/download_maps.py`](../scripts/download_maps.py) | Tile download tool (full documentation in docstring) |
| [`src/hal/sdcard.h`](../src/hal/sdcard.h) | SD card mountpoint and filesystem API |
| [`test/test_map/test_map.cpp`](../test/test_map/test_map.cpp) | Unit tests for tile math and cache |
