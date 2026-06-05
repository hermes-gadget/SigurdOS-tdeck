# Settings Screen

The Settings screen provides a tappable list of device status indicators and configuration options. It's the central dashboard for viewing the device's current state and making runtime adjustments — radio parameters, keyboard backlight, chat history limits, system time, and more.

```
┌──────────────────────────────────┐
│          Settings                │  ← top bar + back button
├──────────────────────────────────┤
│ ⚙  Name: SigurdOS T-Deck          │  ← Node name (read-only)
│ 📶 Radio: 869.618 MHz / 62.5 kHz│  ← tappable → Radio Setup screen
│      / SF8 / 22 dBm              │
│ 💾 SD Card: Mounted              │  ← status read-only
│ 🛰  GPS: Fix acquired            │  ← status read-only
│ ⌨  Keyboard BL: 127 (49%)       │  ← tappable → backlight slider dialog
│ ☰  Chat history: 200 messages   │  ← tappable → +/- cap dialog
│ ⚙  Date: 2025-05-26             │  ← tappable → set dialog (YYYY-MM-DD)
│ ⚙  Time: 14:32                  │  ← tappable → set dialog (HH:MM 24h)
│ ⚙  Run Setup Wizard             │  ← tappable → navigates to Onboarding
│ ⌂  SigurdOS beta-0.1.32           │  ← version info (read-only)
├──────────────────────────────────┤
│     (bottom bar — device info)   │
└──────────────────────────────────┘
```

---

## Source Files

| File | Purpose |
|------|---------|
| `src/ui/screens.cpp` | `settings_screen_show()` at line 1159, all dialog functions, `update_row_label()` at line 812 |
| `src/ui/screens.h` | Public API declaration — `settings_screen_show()` |
| `src/ui/navigation.cpp` | Screen routing — `navigate_to(Screen::Settings)` dispatches here |
| `src/ui/responsive.h` | `dialog_size()` — caps dialog dimensions to display bounds with margin |
| `src/ui/theme.h` | Pixel theme colours — `BG_TERTIARY`, `BG_INPUT`, `TEXT_PRIMARY` for alternating rows |
| `src/hal/prefs.h` | `NodePrefs` struct — all persisted configuration fields |
| `src/hal/tdeck_pins.h` | `SIGURDOS_VERSION` macro at line 130 |
| `src/hal/sdcard.h` | `sigurdos_sdcard_mounted()` — SD card status check |
| `src/hal/gps.h` | `sigurdos_gps_has_fix()` — GPS fix status check |
| `src/hal/keyboard.h` | `sigurdos_keyboard_set_brightness()`, `sigurdos_keyboard_set_default_brightness()` |
| `src/hal/wifi_ota.h` / `.cpp` | AP-mode OTA upload server (`SigurdOS-OTA`, `192.168.4.1`) |
| `src/hal/github_ota.h` / `.cpp` | STA-mode GitHub OTA downloader and progress state machine |
| `src/ui/chat_screen.cpp` | `chat_screen_get_message_cap()`, `chat_screen_set_message_cap()` |

---

## Recently Added System Rows

The live Settings screen has grown beyond the original status/configuration rows above. Current user-facing rows added by recent PRs include:

| Row | Purpose | Persistence / backend |
|-----|---------|-----------------------|
| `Client repeat: ON/OFF` | Enables opportunistic companion relay mode | `NodePrefs::client_repeat`, `SigurdMeshV2` forwarding gate |
| `Multi ACKs: ON/OFF` | Sends extra redundant ACK transmissions on lossy links | `NodePrefs::multi_acks`, `getExtraAckTransmitCount()` |
| `Device PIN: Set/Change` | Protects Settings and Terminal entry | `NodePrefs::device_pin`, PIN prompt helpers in `screens.cpp` |
| `WiFi: <ssid>/Not set` | Stores home WiFi credentials for GitHub OTA | `NodePrefs::wifi_ssid`, `wifi_password` |
| `OTA Update` | Starts AP-mode upload OTA | `sigurdos::ota::start()` (`SigurdOS-OTA`, `192.168.4.1`) |
| `OTA from GitHub` | Downloads latest release `firmware.bin` and flashes it | `sigurdos::github_ota::startGitHubUpdate()` |
| Storage readouts | Shows internal/SD storage state where available | Storage helpers in `screens.cpp` / SD HAL |
| `Shut down` / `Reboot` / `Factory reset` | Power and reset controls with confirmation | board/reset helpers plus mesh persistence cleanup |

---

## Layout Structure

The Settings screen follows the standard screen structure from `make_screen_full("Settings")`:

- **Top bar**: 22px `BG_SECONDARY` bar with a ← back button and "Settings" title
- **Content area**: A single `lv_list` object sized to `LV_PCT(100) × CONTENT_H`, positioned at `CONTENT_Y` from the top. Background transparent, no border. Each row is a tappable list button.
- **Bottom bar**: 20px `BG_SECONDARY` with device name, signal bars, battery percentage (standard for all screens)

### Row Style

Each row is created via the `add_row` lambda (line 1173):

```cpp
auto add_row = [&](const char* icon, const char* text) -> lv_obj_t* {
    lv_obj_t* btn = lv_list_add_btn(list, icon, text);
    lv_obj_set_style_bg_color(btn,
        lv_color_hex(row % 2 == 0 ? BG_TERTIARY : BG_INPUT), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(TEXT_PRIMARY), 0);
    row++;
    return btn;
};
```

Key characteristics:
- **Alternating row backgrounds**: Even rows use `BG_TERTIARY` (`#1E1E1E`), odd rows use `BG_INPUT` (`#252525`)
- **Full opacity** on background (`LV_OPA_COVER`)
- **Text colour**: `TEXT_PRIMARY` (`#F2F3F5`) with an LVGL symbol icon prefix
- **Tappable rows**: Rows that open dialogs or navigate to other screens register `LV_EVENT_CLICKED` callbacks
- **Leading two-space indent** before the label content for visual breathing room

---

## Settings Rows (Top to Bottom)

### 1. Node Name

| Property | Value |
|----------|-------|
| **Label** | `"  Name: <node_name>"` (line 1184) |
| **Data source** | `sigurdos::prefs_get().node_name` — a `char[32]` field in `NodePrefs` |
| **Tap action** | None (read-only status display) |
| **Symbol** | `LV_SYMBOL_SETTINGS` (⚙) |

Displays the mesh node's name as configured during onboarding or via NVS preferences. Truncated to 31 characters + null terminator in the `NodePrefs` struct.

---

### 2. Radio Configuration

| Property | Value |
|----------|-------|
| **Label** | `"  Radio: <freq> MHz / <bw> kHz / SF<sf> / <power> dBm"` (line 1189) |
| **Data source** | `sigurdos::prefs_get()` — `freq`, `bw`, `sf`, `tx_power_dbm` |
| **Tap action** | Opens `radio_setup_screen_show()` — see [Radio Setup Dialog](#radio-setup-dialog) |
| **Symbol** | `LV_SYMBOL_WIFI` (📶) |

#### Not Configured State

When `p.configured = false`, the row shows a red warning (`#4A2020` background) and the unconfigured text (line 1192):

```
Radio: NOT CONFIGURED — tap to configure
```

This forces the user to run the radio setup before the device can transmit — a safety measure to prevent illegal frequency use. The button background is changed to `0x4A2020` (dark red) as an additional visual cue.

#### Radio Setup Dialog

The `radio_setup_screen_show()` function at line 1899 creates a full-screen "Radio Setup" screen with:

- **Warning banner**: Amber text at the top: *"Check local regulations. Incorrect settings may be illegal."*
- **Frequency presets** (left column, 148px wide): Five tappable frequency buttons:
  - `868.000 (EU)`, `869.525 (UK)`, `869.618 (UK)`
  - `915.000 (US)`, `433.500 (EU)`
  - Selected frequency is highlighted in green (`#2A5A2A`)
- **Custom RF... button** (right column): Opens `custom_rf_screen_show()` for arbitrary frequency entry
- **SF (Spreading Factor)**: `-`/`+` buttons adjusting Spreading Factor (SF6–SF12)
- **BW (Bandwidth)**: `-`/`+` buttons adjusting bandwidth in kHz
- **TX Power**: `-`/`+` buttons adjusting transmit power in dBm
- **Save button**: Persists all values to NVS via `sigurdos::prefs_set()`

---

### 3. SD Card Status

| Property | Value |
|----------|-------|
| **Label** | `"  SD Card: Mounted"` or `"  SD Card: Not mounted"` (line 1204) |
| **Data source** | `sigurdos_sdcard_mounted()` |
| **Tap action** | None (read-only status) |
| **Symbol** | `LV_SYMBOL_SD_CARD` (💾) |

Shows whether the microSD card is detected and mounted at boot. The SD card uses the shared SPI bus (CS=39, SCK=40, MOSI=41, MISO=38) with VFS mount point at `/sdcard`.

---

### 4. GPS Status

| Property | Value |
|----------|-------|
| **Label** | `"  GPS: Fix acquired"` or `"  GPS: No fix"` (line 1209) |
| **Data source** | `sigurdos_gps_has_fix()` |
| **Tap action** | None (read-only status) |
| **Symbol** | `LV_SYMBOL_GPS` (🛰) |

Indicates whether the GPS module has acquired a fix. The firmware reads the module on Serial1 with `PIN_GPS_RX=44`, `PIN_GPS_TX=43`, primary 9600 baud, and fallback 38400 baud. "Fix acquired" means at least a basic GPS 2D/3D fix is available.

---

### 5. Keyboard Backlight

| Property | Value |
|----------|-------|
| **Label** | `"  Keyboard BL: <value> (<pct>%)"` (line 1214) |
| **Data source** | `sigurdos::prefs_get().kbd_backlight` (0–255) |
| **Tap action** | Opens `backlight_dialog()` — see [Backlight Dialog](#backlight-dialog) |
| **Symbol** | `LV_SYMBOL_KEYBOARD` (⌨) |
| **Global pointer** | `g_backlight_row` — updated on save so the row text reflects the new value |

Stores the cached row reference in `g_backlight_row` (line 1217) so the dialog can call `update_row_label()` to refresh the row text in-place after a save.

#### Backlight Dialog

The `backlight_dialog()` function at line 1059 creates a centered modal dialog (220×120px via `dialog_size()`):

```
┌──────────────────────┐
│   Keyboard Backlight  │
│                      │
│  [-]  127 (49%)  [+] │
│                      │
│        [ Set ]       │
└──────────────────────┘
```

**Structure:**
- **Title**: "Keyboard Backlight" label at top center, `montserrat_12`, `TEXT_PRIMARY`
- **Value label**: Center-aligned, shows `"<brightness> (<pct>%)"` — e.g. `"127 (49%)"`
- **Minus button** (`-`): Left-aligned, 40×28px, `ACCENT_RED` background. Decrements brightness by 25 (floor at 0 when < 25). Calls `sigurdos_keyboard_set_brightness()` for **live preview** — the keyboard lights change immediately as you tap.
- **Plus button** (`+`): Right-aligned, 40×28px, `ACCENT` background. Increments brightness by 25 (clamped to 255). Also provides live preview.
- **Set button**: Bottom center, 72×24px, `ACCENT_GREEN`. Saves the final brightness to NVS via `sigurdos::prefs_set()` with `kbd_backlight` set to the current value, then calls `sigurdos_keyboard_set_default_brightness()` (persists the default used by the Alt+B toggle). Updates the settings row label and closes the dialog.

**Context struct** (`BacklightCtx`, line 956):
```cpp
struct BacklightCtx {
    lv_obj_t* value_label;   // dialog's value display label
    lv_obj_t* row_label;     // settings row — updated on save
    int       brightness;    // current brightness value (0–255)
};
```

**Cleanup**: Registered on `LV_EVENT_DELETE` — deletes the `BacklightCtx` when the dialog is dismissed.

---

### 6. Chat History Cap

| Property | Value |
|----------|-------|
| **Label** | `"  Chat history: <N> messages"` (line 1223) |
| **Data source** | `chat_screen_get_message_cap()` |
| **Tap action** | Opens `chat_message_cap_dialog()` — see [Chat Cap Dialog](#chat-cap-dialog) |
| **Symbol** | `LV_SYMBOL_LIST` (☰) |
| **Global pointer** | `g_chat_history_row` |

Controls the per-channel in-memory message history cap. Default = 200, minimum = 8, maximum = 200.

#### Chat Cap Dialog

The `chat_message_cap_dialog()` function at line 968 creates a centered modal dialog (220×120px via `dialog_size()`):

```
┌──────────────────────┐
│   Chat Message Cap    │
│                      │
│  [-]   200 msgs  [+] │
│                      │
│        [ Set ]       │
└──────────────────────┘
```

**Structure:**
- **Title**: "Chat Message Cap" label at top center, `montserrat_12`, `TEXT_PRIMARY`
- **Value label**: Center-aligned, shows `"<N> msgs"` — e.g. `"200 msgs"`
- **Minus button** (`-`): Left-aligned, 40×28px, `ACCENT_RED`. Decrements cap by 16, floored at 8. Calls `chat_screen_set_message_cap()` immediately so the chat screen can trim history in real-time as you tap.
- **Plus button** (`+`): Right-aligned, 40×28px, `ACCENT` cyan. Increments cap by 16 (capped at `CHAT_MSGS_MAX` = 200). Also applies live.
- **Set button**: Bottom center, 72×24px, `ACCENT_GREEN`. Reads the current cap value from the context, updates the settings row label via `update_row_label(g_chat_history_row, ...)`, and closes the dialog.

**Context struct** (`ChatHistoryCapCtx`, line 962):
```cpp
struct ChatHistoryCapCtx {
    lv_obj_t* value_label;   // dialog's value display label
    lv_obj_t* row_label;     // settings row — updated on save
    int       cap;           // current cap value
};
```

**Cap clamping** (from `chat_screen_set_message_cap()`, line 1597):
- `cap == 0` → resets to `CHAT_MSGS_DEFAULT_CAP` (200)
- `cap < CHAT_MSGS_MIN_CAP` (8) → clamped to 8
- `cap > CHAT_MSGS_MAX` (200) → clamped to 200

On save, all channel buffers are trimmed via `trim_channel_history()` to respect the new cap.

---

### 7. Date

| Property | Value |
|----------|-------|
| **Label** | `"  Date: <YYYY>-<MM>-<DD>"` (line 1235) |
| **Data source** | `sigurdos::mesh::getCurrentLocalDateTime()` |
| **Tap action** | Opens `datetime_set_dialog(parent, true)` — see [Date/Time Dialog](#datetime-dialog) |
| **Symbol** | `LV_SYMBOL_SETTINGS` (⚙) |
| **Global pointer** | `g_date_row` — updated on set |

### 8. Time

| Property | Value |
|----------|-------|
| **Label** | `"  Time: <HH>:<MM>"` (line 1243) |
| **Data source** | `sigurdos::mesh::getCurrentLocalDateTime()` |
| **Tap action** | Opens `datetime_set_dialog(parent, false)` — see [Date/Time Dialog](#datetime-dialog) |
| **Symbol** | `LV_SYMBOL_SETTINGS` (⚙) |
| **Global pointer** | `g_time_row` — updated on set |

#### Date/Time Dialog

The `datetime_set_dialog()` function at line 832 creates a centered modal dialog (260×120px via `dialog_size()`):

```
┌──────────────────────────┐
│ Set Date (YYYY-MM-DD)    │  ← or "Set Time (HH:MM 24h)"
│ ┌────────────────────┐   │
│ │ 2025-05-26         │   │  ← pre-filled textarea, auto-focused
│ └────────────────────┘   │
│                          │
│ [Cancel]     [Set]       │
└──────────────────────────┘
```

**Structure:**
- **Title**: `"Set Date (YYYY-MM-DD)"` for date mode, `"Set Time (HH:MM 24h)"` for time mode. `montserrat_12`, `TEXT_PRIMARY`
- **Text input**: Pre-filled with the current value (e.g. `"2025-05-26"` or `"14:32"`). Single-line, auto-focused so the physical keyboard works immediately. Styled with `BG_INPUT` background, `TEXT_PRIMARY` text, no border.
- **Feedback label** (bottom center): Displays error messages in `ACCENT_RED` — e.g. `"Invalid date (YYYY-MM-DD)"` or `"Invalid time (HH:MM)"`
- **Cancel button**: Bottom-left, 72×24px, `BG_TERTIARY`. Closes the dialog via `lv_obj_del_async()`.
- **Set button**: Bottom-right, 72×24px, `ACCENT_GREEN`. Validates and persists.

**Validation logic** (line 915):
- **Date mode**: Parses `YYYY-MM-DD` via `sscanf()`. Validates: year > 2020, month 1–12, day 1–31. Combines parsed date with current time from `getCurrentLocalDateTime()` and builds epoch via `sigurdos::mesh::makeEpoch()`.
- **Time mode**: Parses `HH:MM` via `sscanf()`. Validates: hours 0–23, minutes 0–59. Combines parsed time with current date from `getCurrentLocalDateTime()`.

On success, calls `sigurdos::mesh::setSystemTime(epoch)`, then:
1. Reads back the new time via `getCurrentLocalDateTime()`
2. Updates both Date and Time row labels in-place via `update_row_label()`
3. Calls `home_screen_update_time()` to refresh the top bar clock
4. Deletes the dialog

**Context struct** (`DateTimeDialogCtx`, line 826):
```cpp
struct DateTimeDialogCtx {
    lv_obj_t* input;     // textarea for user entry
    lv_obj_t* feedback;  // error message label
    bool      is_date;   // true for date mode, false for time mode
};
```

---

### 9. Run Setup Wizard

| Property | Value |
|----------|-------|
| **Label** | `"  Run Setup Wizard"` (line 1251) |
| **Tap action** | `navigate_to(Screen::Onboarding)` — navigates to the first-boot onboarding wizard |
| **Symbol** | `LV_SYMBOL_SETTINGS` (⚙) |

Allows the user to re-run the setup wizard at any time. This navigates to `Screen::Onboarding` (the same screen shown on first boot), which walks through node name, radio configuration, and channel setup.

---

### 10. Version Info

| Property | Value |
|----------|-------|
| **Label** | `"  SigurdOS beta-0.1.32"` (line 1257) |
| **Data source** | `SIGURDOS_VERSION` macro from `src/hal/tdeck_pins.h` |
| **Tap action** | None (read-only) |
| **Symbol** | `LV_SYMBOL_HOME` (⌂) |

Displays the firmware version string. The `SIGURDOS_VERSION` macro is defined at line 130 of `tdeck_pins.h`:

```cpp
#define SIGURDOS_VERSION  "beta-0.1.32"
```

This is updated manually during release workflows.

---

## Stale-Pointer Safety

The settings screen uses five **static cached pointers** to row button objects so that dialogs can update rows in-place after saving:

```cpp
static lv_obj_t* g_date_row = nullptr;        // line 48
static lv_obj_t* g_time_row = nullptr;         // line 49
static lv_obj_t* g_backlight_row = nullptr;    // line 953
static lv_obj_t* g_chat_history_row = nullptr; // line 954
```

A `LV_EVENT_DELETE` callback on the screen (line 1261) nulls all four pointers when the screen is destroyed, preventing use-after-free on stale pointers:

```cpp
lv_obj_add_event_cb(scr, [](lv_event_t*) {
    g_date_row = nullptr;
    g_time_row = nullptr;
    g_backlight_row = nullptr;
    g_chat_history_row = nullptr;
}, LV_EVENT_DELETE, nullptr);
```

The `update_row_label()` helper (line 812) also null-checks the row pointer before accessing it.

---

## Helper: `update_row_label()`

Defined at line 812. Updates the text inside a settings row button by iterating its children and finding the first `lv_label`:

```cpp
static void update_row_label(lv_obj_t* row, const char* new_text)
{
    if (!row) return;
    uint32_t n = lv_obj_get_child_cnt(row);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* ch = lv_obj_get_child(row, i);
        if (lv_obj_check_type(ch, &lv_label_class)) {
            lv_label_set_text(ch, new_text);
            return;
        }
    }
}
```

---

## UI Theme Integration

| Role | Colour | Hex | Usage |
|------|--------|-----|-------|
| Even row background | `BG_TERTIARY` | `#1E1E1E` | Row 0, 2, 4, 6, 8… |
| Odd row background | `BG_INPUT` | `#252525` | Row 1, 3, 5, 7, 9… |
| Unconfigured radio row | — | `#4A2020` | Dark red warning when radio not configured |
| Row text | `TEXT_PRIMARY` | `#F2F3F5` | All row labels |
| Dialog background | `BG_SECONDARY` | `#181818` | All modal dialog containers |
| Dialog title | `TEXT_PRIMARY` | `#F2F3F5` | Dialog title labels |
| Feedback/error text | `ACCENT_RED` | `#ED4245` | Date/time validation errors |
| Cancel button | `BG_TERTIARY` | `#1E1E1E` | Dialog cancel buttons |
| Set/Save button | `ACCENT_GREEN` | `#3BA55D` | Dialog confirm action |
| Minus button | `ACCENT_RED` | `#ED4245` | Decrement action in +/- dialogs |
| Plus button | `ACCENT` | `#00BFFF` | Increment action in +/- dialogs |
| Input field | `BG_INPUT` | `#252525` | Date/time text input |
| Screen background | `BG_PRIMARY` | `#0F0F0F` | Full screen black via `apply_dark_bg()` |
| Divider | `DIVIDER` | `#2A2A2A` | Divider lines between bars and content |

All dialog elements use zero radius, zero border width, 8px padding — consistent with the pixel theme.

---

## Navigation

- **Entry point**: Set by `navigate_to(Screen::Settings)` in `src/ui/navigation.cpp` (line 83)
- **On back**: Navigates to the previous screen (nav stack, 8-entry circular buffer)
- **Row tap → Radio**: Calls `radio_setup_screen_show()` (a full screen replacement, not a dialog)
- **Row tap → Setup Wizard**: Calls `navigate_to(Screen::Onboarding)`
- **All other tappable rows**: Open centered modal dialogs overlaid on the existing settings screen

---

## Further Reading

- `docs/HOME_SCREEN.md` — Home screen with the SETTINGS tile launcher
- `src/hal/prefs.h` — `NodePrefs` struct definition and all persisted fields
- `src/ui/responsive.h` — `dialog_size()` helper and layout constants
- `src/ui/theme.h` — Full pixel theme colour palette
