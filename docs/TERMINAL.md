# Terminal Screen

> **Source**: `src/ui/screens/screen_terminal.cpp`
> **Navigation**: Home screen → **TERMINAL** icon (`LV_SYMBOL_KEYBOARD`)

The Terminal is a built-in, keyboard-driven command-line interface on the T-Deck. It provides direct access to mesh radio diagnostics and utility functions without leaving the LVGL UI. Designed for developers, field testing, and power users.

---

## Table of Contents

- [Purpose](#purpose)
- [UI Layout](#ui-layout)
- [Color Coding](#color-coding)
- [64-Line Cap](#64-line-cap)
- [Commands Reference](#commands-reference)
  - [`help`](#help)
  - [Current command set](#current-command-set)
  - [`status`](#status)
  - [`advert`](#advert)
  - [`ping`](#ping)
  - [`emoji-list`](#emoji-list)
- [Unknown Command Handling](#unknown-command-handling)
- [Boot Header](#boot-header)
- [Implementation Details](#implementation-details)

---

## Purpose

The Terminal screen serves as a developer-facing diagnostic and control interface. Use it to:

- Inspect real-time mesh radio conditions (RSSI, SNR, noise floor)
- Send ad-hoc advertisement broadcasts into the mesh
- Sign diagnostic payloads with the node identity
- Back up / restore the node private key (`exportkey`, `importkey`)
- Import `meshcore://` contact/channel URIs
- Send anonymous messages, room-server fetch requests, and raw group-data test payloads
- Manage small custom variables in SPIFFS (`getvar`, `setvar`, `delvar`, `listvars`)
- Browse the full emoji character set available on the device
- Obtain a command reference without external documentation

It is **not** a general-purpose shell — it does not execute arbitrary code. Commands are single-line, submit-and-response; only the explicit custom-variable helpers write Terminal-managed data to SPIFFS.

---

## UI Layout

The Terminal screen is divided into three vertically stacked zones:

```
┌──────────────────────────────────┐
│          Top Bar (22px)          │  ⬅ `make_screen_full("Terminal")`
├──────────────────────────────────┤
│                                  │
│         Log Output Area          │  ⬅ 167px (on 320×240 display)
│        (scrollable, flex         │
│         column, black bg)        │
│                                  │
├──────────────────────────────────┤
│   Divider (1px, #2a2a2a)        │
├──────────────────────────────────┤
│  > enter command...              │  ⬅ 28px input field
└──────────────────────────────────┘
          Bottom Bar (20px)
```

### Log Output Area

- **Background**: Pure black (`0x000000`)
- **Layout**: `LV_FLEX_FLOW_COLUMN` — each output line is an LVGL label added as a child
- **Scrolling**: Vertical scroll only (`LV_DIR_VER`), scrollbar hidden (`LV_SCROLLBAR_MODE_OFF`)
- **Padding**: 4px on all sides
- **Height**: `CONTENT_H - TERM_INPUT_H - DIVIDER_H` (167px on a 320×240 display)
- **Auto-scroll**: New lines are automatically scrolled into view (`lv_obj_scroll_to_view`)

### Command Input

- **Background**: `BG_INPUT` (`0x252525`)
- **Text color**: `TEXT_PRIMARY` (`0xf2f3f5`)
- **Font**: Montserrat 10px (`lv_font_montserrat_10`)
- **Height**: 28px fixed
- **Mode**: Single-line (`lv_textarea_set_one_line`)
- **Placeholder**: `> enter command...`
- **Submission**: Press Enter / `LV_EVENT_READY` — the command is read, echoed to the log, executed, and the input field is cleared

### Divider

A 1px horizontal line (`DIVIDER = 0x2a2a2a`) separating the log area from the input field, matching the theme's structural divider style.

---

## Color Coding

Output lines are automatically colour-coded based on their text content. The classification function `term_classify_line()` scans each line for keywords and applies the first matching colour:

| Condition | Colour | Hex | Example Matches |
|---|---|---|---|
| Contains `ERROR`, `FAIL`, or `failed` | **Accent Red** | `#ed4245` | `Radio: ERROR - not configured` |
| Contains `WARN` or `WARNING` | **Accent Orange** | `#faa61a` | `WARN: battery low` |
| Contains `OK`, `ok`, `sent`, or `Pong` | **Accent Green** | `#3ba55d` | `Advert sent`, `Pong! Uptime: ...` |
| Contains `RSSI`, `SNR`, `Noise`, `dBm`, `MHz` | **Accent Cyan** | `#00bfff` | `RSSI:-67dBm SNR:12.3dB ...` |
| Starts with `>` | **Text Primary** | `#f2f3f5` | `> help` (command echo) |
| Default (no match) | **Green** | `#00ff00` | Boot header lines, emoji output |

The font used throughout the terminal log is `emoji_wrapped_montserrat_10` — Montserrat 10px with the Noto Emoji fallback font, enabling emoji glyphs (like those printed by `emoji-list`) to render correctly alongside Latin text.

> **Design note**: The final `#00ff00` fallback is intentionally bright green to distinguish unclassified output from classified lines. It is the only hardcoded colour in the function — all others reference theme constants.

---

## 64-Line Cap

The terminal enforces a **maximum of 64 visible lines** in the log area (`MAX_TERM_LINES = 64`). When a new line would cause the total to exceed 64:

1. The **oldest** child label in the log container is deleted
2. The new line is appended as a new label
3. The view scrolls to the newly added line

This prevents unbounded memory growth from accumulated output during long sessions. The cap applies to all output: boot headers, command echoes, command results, and emoji-list output.

> **Important**: The `emoji-list` command generates ~46 lines of emoji output (362 emoji ÷ 8 per row ≈ 46 rows, plus header and footer). Combined with the 4 boot header lines and any command history, this can cause older output — including earlier command results — to be scrolled out and evicted from the display. For persistent diagnostics, re-issue the command when needed.

---

## Commands Reference

### `help`

Lists all available commands in a single line.

**Syntax**:
```
help
```

**Example Output**:
```
> help
Commands: help status advert ping sign anon fetchmsgs groupdata emoji-list exportkey importkey import getvar setvar delvar listvars
```

**Notes**:
- The `help` output is a space-separated list, not a formatted table.
- For command details, refer to this document.

---

### Current command set

| Command | Syntax | Purpose |
|---------|--------|---------|
| `help` | `help` | Print the one-line command list. |
| `status` | `status` | Show last RSSI/SNR/noise plus contact/channel counts. |
| `advert` | `advert` | Broadcast this node's advert. |
| `ping` | `ping` | Local uptime check. |
| `sign` | `sign <data>` | Sign arbitrary text with the node identity and print signature hex. |
| `anon` | `anon <64hex_pubkey> <text>` | Send an anonymous message to a public key. |
| `fetchmsgs` | `fetchmsgs <contact> <channel>` | Request room-server messages for a channel. |
| `groupdata` | `groupdata <channel_idx> <type_hex> <hex_payload>` | Send raw group data to a channel for protocol testing. |
| `emoji-list` | `emoji-list` | Print the compiled emoji font inventory. |
| `exportkey` | `exportkey` | Print the private key hex; keep this secret. |
| `importkey` | `importkey <64hex_private_key>` | Import/restore identity; reboot afterwards for contacts to re-pair. |
| `import` | `import meshcore://...` | Import contact/channel URIs (`contact/add`, `channel/add`, and raw contact hex). |
| `getvar` | `getvar <key>` | Read a custom variable from SPIFFS. |
| `setvar` | `setvar <key> <value>` | Write/update a custom variable in SPIFFS. |
| `delvar` | `delvar <key>` | Delete a custom variable from SPIFFS. |
| `listvars` | `listvars` | List all custom variables. |

---

### `status`

Displays real-time mesh radio statistics obtained from the MeshCore layer.

**Syntax**:
```
status
```

**Example Output**:
```
> status
RSSI:-67dBm SNR:12.3dB Noise:-98dBm  Contacts:3 Channels:5
```

**Fields**:

| Field | Source | Description |
|---|---|---|
| `RSSI` | `sigurdos::mesh::getLastRSSI()` | Received Signal Strength Indicator of the last packet, in dBm. Typical range: -30 (very strong) to -120 (very weak). |
| `SNR` | `sigurdos::mesh::getLastSNR()` | Signal-to-Noise Ratio of the last packet, in dB. Higher values indicate a cleaner signal. |
| `Noise` | `sigurdos::mesh::getNoiseFloor()` | Current noise floor measurement, in dBm. Lower (more negative) values indicate a quieter RF environment. |
| `Contacts` | `sigurdos::mesh::getContactCount()` | Number of unique mesh nodes this device has discovered and stored. |
| `Channels` | `sigurdos::mesh::getChannelCount()` | Number of active mesh channels known to this node. |

**Notes**:
- All values reflect the **last received packet** (RSSI/SNR) or **current channel state** (noise floor, contacts, channels).
- Values are not polled continuously — this is a point-in-time snapshot.
- If no packets have been received since boot, RSSI/SNR values may be 0 or undefined.
- The line is classified as **Accent Cyan** (`#00bfff`) because it contains RSSI, SNR, Noise, and dBm keywords.

---

### `advert`

Sends an advertisement broadcast into the mesh network.

**Syntax**:
```
advert
```

**Example Output** (success):
```
> advert
Advert sent
```

**Example Output** (failure):
```
> advert
Send failed
```

**Implementation**:
- Calls `sigurdos::mesh::sendAdvert()` which returns a boolean
- A `bool` return value is converted to the appropriate message

**Notes**:
- An advert is a broadcast message announcing this node's presence to the mesh.
- Success depends on the radio being properly configured and the channel being available.
- The response line `Advert sent` contains `sent`, so it is classified as **Accent Green** (`#3ba55d`).
- The response line `Send failed` contains `failed`, so it is classified as **Accent Red** (`#ed4245`).

---

### `ping`

Tests device responsiveness and displays the current system uptime.

**Syntax**:
```
ping
```

**Example Output**:
```
> ping
Pong! Uptime: 734152ms
```

**Implementation**:
- Returns the fixed string `Pong!` followed by the value of `millis()` (Arduino/ESP32 system uptime counter)
- `millis()` returns the number of milliseconds since the microcontroller booted

**Notes**:
- Uptime is in **milliseconds**. To convert: 734152ms ≈ 12 minutes 14 seconds.
- The uptime counter wraps around after approximately 49.7 days (`2^32` ms).
- Contains `Pong` so it is classified as **Accent Green** (`#3ba55d`).
- This is a purely local operation — no mesh radio activity occurs.

---

### `emoji-list`

Prints all 362 emoji characters available in the device's font system, arranged in rows of 8.

**Syntax**:
```
emoji-list
```

**Example Output**:
```
> emoji-list
--- Emoji list (362 available) ---
‼ ⁉ ℹ ⌛ ⌛ ⌛ ⌛ ⌛
⏰ ⏳ ☀ ☁ ☂ ☃ ☔ ☕
♟ ♥ ♻ ⚙ ⚠ ⚡ ⚽ ⚾
... (46 rows total)
--- End emoji list ---
```

**Implementation Details**:

- Iterates the emoji index from 0 to `emoji_font_get_count()` (362) in steps of 8
- Each row is built as a space-separated string of up to 8 emoji characters
- The emoji set is compiled into the firmware at build time (`src/fonts/emoji_font_setup.cpp`)
- Emoji glyphs come from the **Noto Emoji font** (16px, 4bpp), registered as a fallback for the Montserrat font family

**Emoji Count Verification**:
```cpp
static constexpr int EMOJI_LIST_COUNT = sizeof(emoji_list) / sizeof(emoji_list[0]);
static_assert(EMOJI_LIST_COUNT == 362, "emoji_list size mismatch");
```

**Notes**:
- Produces approximately **46 output rows** (362 ÷ 8 ≈ 46) plus the header (1 line) and footer (1 line), totalling ~48 lines.
- This is the most **output-intensive** command — it approaches the 64-line cap on its own.
- Previous output (including earlier command results) may be **evicted** due to the 64-line cap.
- Useful for verifying that the emoji fallback font is correctly loaded and that all 362 codepoints are accessible.
- The emoji data originates from the glyph set in `fonts/emoji_font.c` (the compiled font binary) and is mirrored in the string index in `fonts/emoji_font_setup.cpp`.

**Emoji Font Registration** (for reference):

The emoji font is wired up as a fallback at boot time in `emoji_font_register_fallback()`. Each Montserrat size variant (10–28px) is copied into a writable RAM wrapper, and its `.fallback` pointer is set to `&emoji_font`. This ensures any codepoint not present in Montserrat — including all emoji — resolves through the emoji font.

---

## Unknown Command Handling

Any input that does not match one of the recognised commands listed in [Current command set](#current-command-set) is treated as an unknown command.

**Behaviour**:
1. The input is echoed to the log prefixed with `>`
2. A response line is printed: `Unknown: <command>  (type 'help')`

**Example**:
```
> foobar
Unknown: foobar  (type 'help')
```

**Notes**:
- The response contains neither `help` as a command match (it's text only) — the classification fallback colour (`#00ff00`) applies.
- Empty input (zero-length string or `nullptr`) is silently ignored — no echo, no error.
- There is no fuzzy matching, case-insensitivity, or partial match logic. `Help` (capital H) and `HELP` are both unknown.

---

## Boot Header

When the Terminal screen is first opened, four or five initial lines are printed before any user interaction:

```
SigurdOS T-Deck Terminal
MeshCore protocol active
Radio: SX1262 868.000 MHz configured     ← (if configured)
```

Or, if the radio has not been configured:

```
SigurdOS T-Deck Terminal
MeshCore protocol active
Radio: ERROR - not configured
```

**Lines**:

| # | Line | Colour | Condition |
|---|---|---|---|
| 1 | `SigurdOS T-Deck Terminal` | `#00ff00` (default fallback) | Always |
| 2 | `MeshCore protocol active` | `#00ff00` (default fallback) | Always |
| 3 | `Radio: SX1262 <freq> MHz configured` | `#00bfff` (Accent Cyan — contains MHz) | `NodePrefs.configured == true` |
| 3 | `Radio: ERROR - not configured` | `#ed4245` (Accent Red — contains ERROR) | `NodePrefs.configured == false` |

The radio configuration line reads from `sigurdos::prefs_get()` at the moment the Terminal screen is created.

---

## Implementation Details

### Key Constants

| Constant | Value | Description |
|---|---|---|
| `TERM_INPUT_H` | `28` | Height of the command input textarea (px) |
| `TERM_LOG_H` | `CONTENT_H - TERM_INPUT_H - DIVIDER_H` | Height of the log output area (167px on 320×240) |
| `MAX_TERM_LINES` | `64` | Maximum number of log lines before oldest is pruned |
| `DIVIDER_H` | `1` | Height of the divider between log and input (from `responsive.h`) |
| `CONTENT_Y` | `TOP_BAR_H + DIVIDER_H` | Y-offset where the log area begins |

### Flow of a Command

1. User types text in the input textarea and presses Enter
2. `LV_EVENT_READY` fires on the textarea
3. The textarea's event callback (in `screen_terminal.cpp`) executes:
   - Reads text via `lv_textarea_get_text(ta)`
   - Returns early if text is empty (`nullptr` or `""`)
   - Echoes `> <command>` to the log in `TEXT_PRIMARY`
   - Compares against the known command strings
   - Prints the result line (with appropriate colour classification)
   - Clears the input field via `lv_textarea_set_text(ta, "")`

### Focus and Navigation

- On screen creation, the input textarea is added to the default LVGL input group
- Focus is immediately set to the input textarea, so the physical keyboard is ready for typing
- The back button in the top bar navigates to the Home screen (`go_back()`)

### Test Mode Stubs

When compiled in test mode (no LVGL display), the Terminal functions are replaced with stubs in `src/test/test_controller.cpp`:

| Function | Stub Behaviour |
|---|---|
| `term_dump_log()` | Prints `[term] dump: no terminal screen (test mode)` |
| `term_clear_log()` | Prints `[term] cleared (test mode)` |
| `term_submit(text)` | Prints `[term] submit: <text> (test mode, ignored)` |
| `term_get_input()` | Returns `nullptr` |

These stubs are declared in `src/ui/screens.h` and are reachable from the test controller for integration testing.

---

## Theme Constants Reference

Colours referenced by the Terminal screen, defined in `src/ui/theme.h`:

| Constant | Hex | Usage |
|---|---|---|
| `BG_INPUT` | `0x252525` | Input field background |
| `TEXT_PRIMARY` | `0xf2f3f5` | Command echo (`>`) text colour |
| `ACCENT` | `0x00bfff` | Radio diagnostics (RSSI, SNR, dBm lines) |
| `ACCENT_GREEN` | `0x3ba55d` | Success responses (sent, Pong, OK) |
| `ACCENT_RED` | `0xed4245` | Error responses (ERROR, FAIL, failed) |
| `ACCENT_ORANGE` | `0xfaa61a` | Warnings (WARN, WARNING) |
| `DIVIDER` | `0x2a2a2a` | Divider line colour |

---

## Summary

| Command | Purpose | Typical Output | Colour |
|---|---|---|---|
| `help` | List available commands | `Commands: help status advert ping sign anon fetchmsgs groupdata emoji-list exportkey importkey import getvar setvar delvar listvars` | Default green |
| `status` | Show mesh radio diagnostics | `RSSI:-67dBm SNR:12.3dB Noise:-98dBm Contacts:3 Channels:5` | Accent Cyan |
| `advert` | Broadcast advert | `Advert sent` or `Send failed` | Green / Red |
| `ping` | Device responsiveness + uptime | `Pong! Uptime: 734152ms` | Accent Green |
| `sign` | Sign diagnostic payload | 64-byte signature as hex | Default green |
| `exportkey` / `importkey` | Back up / restore identity | Private-key hex or import confirmation | Default green / warning-sensitive |
| `import` | Import `meshcore://` URI | `Contact imported via URI` / `Channel added via URI` | Green / Red |
| `getvar` / `setvar` / `delvar` / `listvars` | Manage Terminal custom variables | Key/value output | Default green |
| `emoji-list` | Print all 362 emoji | ~48 lines of emoji rows | Default green |
| _(unknown)_ | Unrecognised command | `Unknown: foobar (type 'help')` | Default green |
