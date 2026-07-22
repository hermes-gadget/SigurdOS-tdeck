# Home Screen

The Home screen is SigurdOS's main launcher — a 4×3 icon grid that provides access to all major features of the firmware. Inspired by feature-phone and handheld-console launchers, it presents a fixed set of 12 icon tiles arranged in a uniform grid with trackball-driven keyboard navigation, touch support, and full theme integration.

---

## Source Files

| File | Purpose |
|------|---------|
| `src/ui/home_screen.h` | Public API — `home_screen_create()`, `home_screen_show()`, `home_screen_handle_trackball()`, runtime update functions (battery, time, signal, channels) |
| `src/ui/home_screen.cpp` | Full implementation — top bar, bottom bar, adaptive icon grid, tile creation, selection rendering, trackball handler |
| `src/ui/responsive.h` | Display-size-agnostic layout constants — `TOP_BAR_H`, `BOT_BAR_H`, `CONTENT_H`, `compute_grid()`, `HASHTAG_LABEL_W()` |
| `src/ui/theme.h` | Pixel theme colours, helpers — `apply_dark_bg()`, `create_signal_dots()`, `rssi_to_dots()` |
| `src/ui/navigation.cpp` | Screen routing — `navigate_to(Screen)` dispatches to the target screen when a tile is activated |

---

## Layout Structure

The Home screen is composed of three stacked regions:

```
┌──────────────────────────────────┐
│ ≡  #general  #random  #help 14:32│  ← top bar (TOP_BAR_H px, BG_SECONDARY)
├──────────────────────────────────┤  ← divider (DIVIDER_H = 1px)
│ ┌─────┬─────┬─────┬─────┐        │
│ │CHATS│ DMs │ROOMS│CONTA│        │  ← icon grid
│ ├─────┼─────┼─────┼─────┤        │    (4 columns × 3 rows
│ │REPEA│ADVER│  MAP│TERMI│        │     filling CONTENT_H)
│ ├─────┼─────┼─────┼─────┤        │
│ │PACKE│SETTI│SETUP│SIGNA│        │
│ └─────┴─────┴─────┴─────┘        │
├──────────────────────────────────┤  ← divider (DIVIDER_H = 1px)
│ SigurdOS T-Deck   ▂▄▆█       72%  │  ← bottom bar (BOT_BAR_H px, BG_SECONDARY)
└──────────────────────────────────┘
```

### Top Bar

Created by `create_top_bar()` in `home_screen.cpp`. A horizontal bar at the top with:

| Element | Position | Details |
|---------|----------|---------|
| **≡ Hamburger icon** | Left-aligned (x=4) | `LV_SYMBOL_LIST`, styled in `TEXT_SECONDARY` (`#949BA4`) |
| **Channel hashtag snapshot** | Left of center (x=26) | Space-separated list of all known channels, auto-truncated via `LV_LABEL_LONG_DOT`. Built by `build_channel_string()` — prefixes names with `#` if they don't already have one. Shows `"no channels"` if none exist. Rendered in `montserrat_10` with `CHANNEL_HASH` cyan (`#00BFFF`). Width computed by `HASHTAG_LABEL_W()` = `DISPLAY_W - 60` |
| **24h time** | Right-aligned (x=-4) | Initially `"--:--"`, updated via `home_screen_update_time()`. Rendered in `montserrat_12`, `TEXT_PRIMARY` (`#F2F3F5`) |

Styling: `BG_SECONDARY` (`#181818`) background, zero padding, zero border width.

### Divider (above grid)

A 1px horizontal line at `CONTENT_Y` (immediately below the top bar). `BG_COLOR` = `DIVIDER` (`#2A2A2A`), no border.

### Icon Grid

Created by `create_icon_grid()` in `home_screen.cpp`. An adaptive grid container sized to `CONTENT_H` (display height minus bars and dividers). The grid is fully tiled with 12 icon tiles, no scrolling.

**Tile dimensions are computed dynamically**:
- `GRID_PAD` = 3px padding around and between tiles
- Column count determined by `compute_grid()` from `responsive.h` (see [Responsive Layout](#responsive-layout))
- Each tile's width = `(CONTENT_W - 2×GRID_PAD - (cols-1)×GRID_PAD) / cols`
- Each tile's height = `(CONTENT_H - 2×GRID_PAD - (rows-1)×GRID_PAD) / rows`
- Width is uniform across all columns to eliminate visual warping from the selection border
- Height distributes any remainder pixels across the first N rows

### Divider (above bottom bar)

A 1px horizontal line at `CONTENT_Y + CONTENT_H` (immediately below the grid). Same styling as the top divider.

### Bottom Bar

Created by `create_bottom_bar()` in `home_screen.cpp`. Slightly shorter than the top bar.

| Element | Position | Details |
|---------|----------|---------|
| **Device name** | Left-aligned (x=4) | From `mesh::getOwnName()`, `montserrat_10`, `TEXT_SECONDARY` (`#949BA4`) |
| **Signal dots** | Center (x=-20) | iOS-style 5-dot RSSI indicator from `create_signal_dots()` in `theme.h`. Active dots are `ACCENT` cyan filled; inactive dots are `TEXT_MUTED` outlines. Updated via `home_screen_update_signal()` which calls `rssi_to_dots()` |
| **Battery percentage** | Right-aligned (x=-4) | Initially `"--%"`, updated via `home_screen_update_battery()`. `montserrat_10`, `ACCENT` cyan normally, turns `ACCENT_RED` (`#ED4245`) below 20% |

Styling: `BG_SECONDARY` (`#181818`) background, zero padding, zero border width.

---

## The 12 Icon Tiles

Each tile is a zero-radius card (`apply_pixel_card`-like) containing a symbol and label. The `IconDef` struct defines each tile's identity:

```cpp
struct IconDef {
    const char* label;    // Display label (e.g. "CHATS")
    const char* symbol;   // LVGL symbol character (e.g. LV_SYMBOL_ENVELOPE)
    bool        badge;    // True = show a red notification badge dot
    Screen      target;   // Destination screen when activated
};
```

The 12 icons are defined in the `icons[]` array in `home_screen.cpp`:

| # | Label | Symbol | Badge | Target Screen | Description |
|---|-------|--------|-------|---------------|-------------|
| 0 | **CHATS** | `LV_SYMBOL_ENVELOPE` (✉) | ✅ Yes | `Screen::Chat` | Open the messaging interface — channel list, DMs, message history |
| 1 | **DMs** | `LV_SYMBOL_FILE` | No | `Screen::Chat` | Direct messages (same Chat screen, DM conversations) |
| 2 | **ROOMS** | `LV_SYMBOL_DIRECTORY` | No | `Screen::Contacts` | Room servers (listed on the Contacts screen) |
| 3 | **CONTACTS** | `LV_SYMBOL_CALL` (📞) | No | `Screen::Contacts` | Browse known mesh nodes, view contact details, start DMs |
| 4 | **REPEATERS** | `LV_SYMBOL_WIFI` (📶) | No | `Screen::Repeaters` | View infrastructure repeater nodes heard on the mesh |
| 5 | **ADVERTISE** | `LV_SYMBOL_BELL` | No | `Screen::Advertise` | Broadcast node presence / custom advertisements |
| 6 | **MAP** | `LV_SYMBOL_GPS` | No | `Screen::Map` | Open the offline PNG-tile map renderer |
| 7 | **TERMINAL** | `LV_SYMBOL_KEYBOARD` (⌨) | No | `Screen::Terminal` | Developer terminal / diagnostics console |
| 8 | **PACKETS** | `LV_SYMBOL_LIST` (☰) | No | `Screen::Heard` | Show heard-packet log |
| 9 | **SETTINGS** | `LV_SYMBOL_SETTINGS` (⚙) | No | `Screen::Settings` | Settings category hub (WiFi, Bluetooth, Radio, GPS, Display, System, Node Stats) |
| 10 | **SETUP** | `LV_SYMBOL_HOME` | No | `Screen::Onboarding` | First-boot / reset onboarding wizard |
| 11 | **SIGNAL** | `LV_SYMBOL_BARS` (▂▄▆█) | No | `Screen::Signal` | Signal strength viewer / RF metrics |

**Note:** Screens not on the grid are reached from within related screens (e.g. Trace from Contact Detail, the Settings sub-screens from the Settings hub) or via the remote-test `nav` command — all dispatched through `navigate_to()` in `src/ui/navigation.cpp`.

### Tile Rendering

Each tile is created by `create_icon_tile()` in `home_screen.cpp`:

1. **Background**: `BG_TERTIARY` (`#1E1E1E`), full opacity, zero radius
2. **Border**: 2px (`PIXEL_BORDER`), default colour `BG_PRIMARY` (`#0F0F0F`) — changes to `ACCENT` cyan when selected
3. **Padding**: 4px all around
4. **Pressed state**: `#2A2A2A` background (`LV_STATE_PRESSED`)
5. **Symbol**: LVGL symbol character, `montserrat_14`, `ACCENT` cyan, centred vertically at `y=-8` (slightly above midline)
6. **Label**: All-caps tile name, `montserrat_10`, `TEXT_PRIMARY` (`#F2F3F5`), centred at `y=12` (below the symbol)
7. **Badge** (CHATS only): An 18×12px zero-radius red counter (`ACCENT_RED` `#ED4245`), aligned top-right of the tile

### Badge (CHATS Tile)

Only the CHATS tile has `badge = true`. This creates an **18×12px red unread counter** in the top-right corner of the tile. `home_screen_update_badges()` reads `sigurdos::mesh::getUnreadMessageCount()`, hides the badge when the count is zero, and shows a capped numeric count (`99` max) when unread messages exist. Opening Chat resets the mesh unread counter.

---

## Trackball Navigation

The 5-direction trackball provides full keyboard-less navigation of the icon grid via `home_screen_handle_trackball()`.

### Movement Rules

| Event | Action | Wrap Behaviour |
|-------|--------|----------------|
| **Left** | Move selection to the previous tile in the same row | Wraps to the last tile of that row |
| **Right** | Move selection to the next tile in the same row | Wraps to the first tile of that row |
| **Up** | Move selection up one row (same column) | Wraps to the bottom row; skips past-the-end tiles if the last row has fewer columns |
| **Down** | Move selection down one row (same column) | Wraps to the top row; skips past-the-end tiles if the last row has fewer columns |
| **Click** | Activate the selected tile | Calls `navigate_to(icons[selected_icon].target)` |

### Active Grid Dimensions

The grid dimensions are computed based on available width:

```cpp
GridLayout compute_grid(int /*grid_pad*/ = 3) {
    if (CONTENT_W >= 300)      g.cols = 4;  // 320px landscape → 4 cols
    else if (CONTENT_W >= 230) g.cols = 3;  // smaller or portrait displays
    else if (CONTENT_W >= 170) g.cols = 2;
    else                       g.cols = 1;  // fallback
}
```

The row count is derived as `(12 + cols - 1) / cols` → 3 rows at 4 columns, 4 rows at 3 columns, etc.

### Column Wrapping at Last Row

When the last row has fewer tiles than `active_cols` (e.g. 4 columns × 3 rows = exactly 12 fills perfectly at 4 cols, but at 3 cols = 12/3 = 4 full rows), the Up/Down handlers skip non-existent tile indices by looping until they find a valid tile index.

---

## Selection & Highlight

Selection is rendered by `apply_selection()`:

- The **previously selected** tile gets its border colour reset to `BG_PRIMARY` (`#0F0F0F`)
- The **newly selected** tile gets its border colour set to `ACCENT` (`#00BFFF`)
- `force_full_tile_redraw(idx)` expands the dirty rectangle by 4px on each side and invalidates both the tile object and its extended area, ensuring the LVGL partial renderer redraws the entire tile including its border in one pass

**Initial selection**: On creation, `selected_icon` is set to 0 (CHATS) and `apply_selection()` is called immediately.

---

## Responsive Layout

All layout constants are computed at compile-time in `src/ui/responsive.h` using `constexpr` functions.

### Bar Heights

```cpp
constexpr int bar_height() {
    return (DISPLAY_H / 11 < 12) ? 12 : (DISPLAY_H / 11 > 28) ? 28 : DISPLAY_H / 11;
}
constexpr int TOP_BAR_H  = bar_height();       // ≈ 9% of display H, clamped [12, 28]
constexpr int BOT_BAR_H  = bar_height() - 2;   // slightly shorter than top bar
constexpr int DIVIDER_H  = 1;                  // thin divider line
```

### Content Area

```cpp
constexpr int CONTENT_Y  = TOP_BAR_H + DIVIDER_H;
constexpr int CONTENT_H  = DISPLAY_H - CONTENT_Y - DIVIDER_H - BOT_BAR_H;
constexpr int CONTENT_W  = DISPLAY_W;
```

### Values at 320×240 (T-Deck, landscape via rotation)

| Constant | Value | Description |
|----------|-------|-------------|
| `TOP_BAR_H` | 22px | Top bar height |
| `BOT_BAR_H` | 20px | Bottom bar height |
| `DIVIDER_H` | 1px | Divider line thickness |
| `CONTENT_Y` | 23px | Grid top edge (below top bar + divider) |
| `CONTENT_H` | 196px | Grid height (240 - 22 - 1 - 1 - 20) |
| `CONTENT_W` | 320px | Full display width (landscape) |
| Grid columns | 4 | `CONTENT_W = 320 ≥ 300` (landscape layout) |
| Grid rows | 3 | 12 tiles / 4 cols |
| Tile width | ~75px | `(320 - 6 - 9) / 4` with 3px gap |
| Tile height | ~62px | `(196 - 6 - 6) / 3` with 3px gap |

---

## Public API Reference

### `home_screen_create()`

Creates the Home screen with a **fade-in** animation (`LV_SCR_LOAD_ANIM_FADE_ON`, 300ms). Called during boot in `ui::init()` to establish the initial screen after the splash.

### `home_screen_show()`

Brings the Home screen to the foreground with a **slide-in from right** animation (`LV_SCR_LOAD_ANIM_MOVE_RIGHT`, 200ms). Called from `navigate_to(Screen::Home)` in `navigation.cpp`.

### Construction details (`build_home_screen()`)

Both `create()` and `show()` call the internal `build_home_screen()` function, which:

1. Clears all cached pointer references (`hashtag_label`, `time_label`, `batt_label`, `signal_label`, all `icon_tiles[]`) to prevent stale-pointer use
2. Clears the back-button reference via `screens_clear_back_btn()`
3. Creates a new full-screen LVGL object with `apply_dark_bg()` (`BG_PRIMARY` `#0F0F0F`)
4. Disables all scroll flags on the screen object
5. Registers a `LV_EVENT_DELETE` callback that nulls all static pointers when the screen is replaced (automatic cleanup since Home uses `auto_del = true`)
6. Creates top bar, bottom bar, and icon grid
7. Loads the screen with the requested animation

### `home_screen_handle_trackball(SigurdOSTrackballEvent event)`

Routes a trackball event to the icon grid navigation. See [Trackball Navigation](#trackball-navigation). Called from `ui::handle_trackball_event()` in `ui.cpp`.

### `home_screen_update_battery(int pct)`

Updates the battery percentage label in the bottom bar. Turns the text colour red when percentage is ≤ 20%.

### `home_screen_update_time(const char* time_str)`

Updates the 24h time display in the top bar. Expected format: `"HH:MM"`.

### `home_screen_update_signal(int rssi)`

Updates the RSSI signal dots widget in the bottom bar. Converts RSSI dBm to 1–5 active dots via `rssi_to_dots()`:

| RSSI dBm | Active Dots |
|----------|-------------|
| > -70    | 5 (full)    |
| > -85    | 4           |
| > -95    | 3           |
| > -105   | 2           |
| ≤ -105   | 1 (weak)    |

### `home_screen_update_channels()`

Rebuilds the channel hashtag string in the top bar. Called periodically to reflect channel join/leave changes from the mesh.

---

## UI Theme Integration

All visual styling follows the SigurdOS pixel theme (`src/ui/theme.h`):

| Role | Colour | Hex | Usage |
|------|--------|-----|-------|
| Screen background | `BG_PRIMARY` | `#0F0F0F` | Full-screen black (`apply_dark_bg()`) |
| Bar backgrounds | `BG_SECONDARY` | `#181818` | Top bar, bottom bar |
| Tile background | `BG_TERTIARY` | `#1E1E1E` | Icon tile cards |
| Tile default border | `BG_PRIMARY` | `#0F0F0F` | Unselected tile border |
| Selection highlight | `ACCENT` | `#00BFFF` | Selected tile border, symbol colour, battery %, signal bars |
| Channel hashtag | `CHANNEL_HASH` | `#00BFFF` | Channel names in top bar |
| Primary text | `TEXT_PRIMARY` | `#F2F3F5` | Tile labels, time display |
| Secondary text | `TEXT_SECONDARY` | `#949BA4` | Hamburger icon, device name |
| Divider lines | `DIVIDER` | `#2A2A2A` | Thin dividers between bars and grid |
| Tile pressed state | — | `#2A2A2A` | Background when pressed/clicked |
| Badge (alert) | `ACCENT_RED` | `#ED4245` | CHATS notification dot |
| Battery low warning | `ACCENT_RED` | `#ED4245` | Red text when ≤ 20% |

All tiles use zero border radius, `PIXEL_BORDER` (2px) minimum border width, no shadows — maintaining the blocky "pixel" aesthetic.

---

## Safety: Stale-Pointer Handling

The home screen uses **static cached pointers** to LVGL objects for efficient runtime updates of time, battery, signal, and channel text. Since the screen is recreated fresh each time (Home is not persisted in the nav stack), a `LV_EVENT_DELETE` callback is registered in `build_home_screen()`:

```cpp
lv_obj_add_event_cb(scr, [](lv_event_t*) {
    scr = top_bar = bottom_bar = grid = nullptr;
    time_label = batt_label = signal_label = hashtag_label = nullptr;
    for (int i = 0; i < ICON_COUNT; i++) icon_tiles[i] = nullptr;
}, LV_EVENT_DELETE, nullptr);
```

This ensures that when `ui::loop()` or any other code calls a home screen update function while another screen is active, the null checks (`if (!time_label) return;`) at the top of each update function prevent dereferencing freed objects.

Additionally, `build_home_screen()` explicitly resets all pointers to `nullptr` before creating new objects, and `screens_clear_back_btn()` is called to clear any stale back-button reference from the previous screen.

---

## Known Issues

- **No scroll**: The grid is fixed-size with no scroll. On very small displays or landscape orientations with more rows than fit, tiles may be clipped.
- **Theme symbol rendering**: LVGL symbol characters are used for tile icons rather than custom bitmap sprites. On builds with limited font compression, these may render as fallback boxes if the symbol font is not correctly linked.
