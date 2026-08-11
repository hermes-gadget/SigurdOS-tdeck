# Settings Screen

The Settings screen is a **category hub**: a short tappable list that routes to dedicated sub-screens for each configuration area. The old single flat settings list was split into per-category screens when `src/ui/screens.cpp` was decomposed into `src/ui/screens/screen_*.cpp` modules.

```
┌──────────────────────────────────┐
│          Settings                │  ← top bar + back button
├──────────────────────────────────┤
│ 📶 WiFi                       >  │  ← Screen::WiFiNetworks
│ 📶 Network                    >  │  ← Screen::Transports
│ 📶 Bluetooth                  >  │  ← Screen::Bluetooth
│ 📶 Radio / Mesh               >  │  ← Screen::SettingsRadio
│ 🛰  GPS / Location             >  │  ← Screen::SettingsGPS
│ 🖼  Display / UI               >  │  ← Screen::SettingsDisplay
│ ⚙  System                     >  │  ← Screen::SettingsSystem
│ ⚙  Node Stats                 >  │  ← Screen::NodeStats
├──────────────────────────────────┤
│     (bottom bar — device info)   │
└──────────────────────────────────┘
```

If a device PIN is configured (`NodePrefs::device_pin != 0`) and the PIN grace period is not active, opening Settings first shows the PIN entry screen (`pin_entry_show(Screen::Settings)`).

---

## Source Files

| File | Purpose |
|------|---------|
| `src/ui/screens/screen_settings.cpp` | The category hub — `settings_screen_show()`, PIN gate, category rows |
| `src/ui/screens/screen_settings_radio.cpp` | Radio / Mesh sub-screen — RF summary row plus mesh behavior settings |
| `src/ui/screens/screen_settings_gps.cpp` | GPS / Location sub-screen — fix status, GPS enable, poll interval, location sharing, track log recording |
| `src/ui/screens/screen_file_browser.cpp` | SD Card File Browser — nested navigation, file preview, copy, delete |
| `src/ui/screens/screen_settings_display.cpp` | Display / UI sub-screen — keyboard/display brightness, auto-off, chat history cap, theme |
| `src/ui/screens/screen_settings_system.cpp` | System sub-screen — name, SD, date/time, wizard, PIN, WiFi credentials, OTA, power controls, version |
| `src/ui/screens/screen_radio_setup.cpp` | Radio Setup screen — frequency presets, SF/BW/CR/TX power, multi-ACK toggle, Custom RF |
| `src/ui/screens/screen_wifi_networks.cpp` | WiFi networks screen — scan, connect, AP management |
| `src/ui/screens/screen_transports.cpp` / `.h` | Network screen — TCP/WebSocket companion toggles and live status |
| `src/ui/screens/screen_bluetooth.cpp` | Bluetooth screen — BLE companion enable/disable, PIN display, connection status |
| `src/ui/screens/screen_node_stats.cpp` | Node statistics screen |
| `src/ui/screens/screen_regions.cpp` | Regions (flood scope) screen — reached from Radio / Mesh |
| `src/ui/screens_common.cpp` / `.h` | Shared helpers — `make_screen_full()`, PIN entry, back button handling |
| `src/ui/navigation.cpp` | Screen routing — `navigate_to(Screen::Settings)` and all sub-screen IDs |
| `src/ui/responsive.h` | `dialog_size()` — caps dialog dimensions to display bounds with margin |
| `src/ui/theme.h` | Pixel theme colours — `BG_TERTIARY`, `BG_INPUT`, `TEXT_PRIMARY` for alternating rows |
| `src/hal/prefs.h` | `NodePrefs` struct — all persisted configuration fields |
| `src/hal/tdeck_pins.h` | `SIGURDOS_VERSION` macro |

---

## The Hub (`settings_screen_show()`)

- Built with `make_screen_full("Settings")` (standard top/bottom bars).
- A single `lv_list` of category rows with alternating backgrounds (`BG_TERTIARY` / `BG_INPUT`), an accent icon on the left, and a muted `>` chevron on the right.
- Each row's `LV_EVENT_CLICKED` handler calls `navigate_to()` with the target `Screen` ID passed through `user_data`.

| Row | Target | Sub-screen contents |
|-----|--------|---------------------|
| WiFi | `Screen::WiFiNetworks` | Network scan, connect/disconnect, saved credentials |
| Network | `Screen::Transports` | TCP and WebSocket companion servers; BLE remains on Bluetooth |
| Bluetooth | `Screen::Bluetooth` | BLE companion toggle, pairing PIN, connection status |
| Radio / Mesh | `Screen::SettingsRadio` | See below |
| GPS / Location | `Screen::SettingsGPS` | See below |
| Display / UI | `Screen::SettingsDisplay` | See below |
| System | `Screen::SettingsSystem` | See below |
| Node Stats | `Screen::NodeStats` | Uptime, memory, packet counters |

---

## Radio / Mesh (`screen_settings_radio.cpp`)

| Row | Action / persistence |
|-----|----------------------|
| `Radio: <freq> MHz / <bw> kHz / SF<sf> / <dBm>` (or `NOT CONFIGURED`) | Tap → Radio Setup screen |
| `Flood max hops` | Cycles preset values, persisted in `NodePrefs` |
| `Auto-add` | Contact auto-add policy |
| `Add max hops` | Hop limit for auto-added contacts |
| `RX delay base` / `TX delay factor` / `Direct TX delay` | Radio timing tuning |
| `Auto-advert` | Periodic advert interval |
| `Node type` | Companion / repeater-style node type |
| `Duty cycle` | TX duty-cycle limit |
| `Client repeat: ON/OFF` | Opportunistic relay (`NodePrefs::client_repeat`) |
| `Regions: <active scope>` | Tap → `Screen::Regions` (flood-scope region management) |

### Radio Setup screen (`screen_radio_setup.cpp`)

Reached from the RF summary row. Provides:

- **Warning banner** — check local regulations before transmitting.
- **Frequency presets** plus a **Custom RF** entry for arbitrary values.
- **SF / BW / CR / TX power** adjusters.
- **Multi-ACK toggle** (`NodePrefs::multi_acks`) — extra redundant ACK transmissions for lossy links.
- **Save** persists to NVS via `sigurdos::prefs_set()`.

When `NodePrefs::configured == false`, the RF summary row shows `Radio: NOT CONFIGURED` on a dark-red background — the device will not transmit until the radio is explicitly configured (safety against illegal frequency use).

---

## GPS / Location (`screen_settings_gps.cpp`)

| Row | Action / persistence |
|-----|----------------------|
| `GPS: Fix acquired / No fix` | Read-only status from `sigurdos_gps_has_fix()` |
| `GPS: ON/OFF` | Enables/disables the GPS module (`NodePrefs::gps_enabled`) |
| `GPS interval` | Published position-update interval; UART acquisition remains continuous while initialized |
### Track Log Recording

When a GPS fix is available, the device can record a breadcrumb track log
rendered as a path overlay on the Map screen (see [MAP_SCREEN.md](MAP_SCREEN.md)):

| Row | Action / persistence |
|-----|----------------------|
| `Track log: ON/OFF` | Enables/disables GPS breadcrumb recording (`NodePrefs::track_log_enabled`) |
| `Track interval: <N>s` | Recording interval in seconds (1–3600, default 15) |
| `Clear on start: ON/OFF` | Whether to clear the previous track when recording starts |
| `Waypoints: <N>` | Read-only count of recorded points (up to 2048) |
| `Distance: <N>m` | Cumulative haversine distance of the recorded track |
| `Duration: <N>s` | Elapsed time between first and last waypoint |

When track logging is enabled, GPS demand uses the minimum of the track
interval and the configured GPS position interval — whichever is shorter —
to ensure the track log receives timely fixes.
| `Share location: ON/OFF` | Include coordinates in adverts (`NodePrefs::advert_loc_policy`) |

---

## Display / UI (`screen_settings_display.cpp`)

| Row | Action / persistence |
|-----|----------------------|
| `Keyboard BL: <v> (<pct>%)` | Opens the backlight dialog (below) |
| `Display: <v> (<pct>%)` | Display brightness dialog (same +/- pattern) |
| `Auto-off: Off / <N>s` | Display auto-off timeout (`NodePrefs::auto_off_timeout`) |
| `Chat history: <N> messages` | Opens the chat message cap dialog (below) |
| `Theme: <name>` | UI theme selection |

### Backlight dialog

A centred modal (sized via `dialog_size()`):

```
┌──────────────────────┐
│   Keyboard Backlight  │
│                      │
│  [-]  127 (49%)  [+] │
│                      │
│        [ Set ]       │
└──────────────────────┘
```

- `-` / `+` step the value by 25 (clamped 0–255) and call `sigurdos_keyboard_set_brightness()` for **live preview**.
- **Set** persists `NodePrefs::kbd_backlight`, calls `sigurdos_keyboard_set_default_brightness()` (the default used by the Alt+B toggle), updates the row label in-place, and closes the dialog.
- A heap-allocated context struct is freed by an `LV_EVENT_DELETE` callback when the dialog closes.

### Chat message cap dialog

Same +/- pattern. Steps by 16, clamped to `[CHAT_MSGS_MIN_CAP, CHAT_MSGS_MAX]` = [8, 200]; changes apply live via `chat_screen_set_message_cap()` (which persists to NVS and trims channel buffers immediately).

---

## System (`screen_settings_system.cpp`)

| Row | Action / persistence |
|-----|----------------------|
| `Name: <node_name>` | Read-only (set via onboarding) |
| `SD Card: Mounted / Not mounted` | Read-only status from `sigurdos_sdcard_mounted()`; tap to open **File Browser** |
| `Date: YYYY-MM-DD` / `Time: HH:MM` | Open the date/time dialog (below) |
| `Time source: <source>, <age>` | Shows Manual, Companion, GPS, or Unknown; tap to refresh the age |
| `Run Setup Wizard` | `navigate_to(Screen::Onboarding)` |
| `Device PIN: Set/Change` | PIN protecting Settings entry (`NodePrefs::device_pin`) |
| `WiFi: <ssid> / Not set` | Shows live STA/AP status and stores credentials for GitHub OTA (`NodePrefs::wifi_ssid/wifi_password`) |
| `OTA Update` | Starts AP-mode upload OTA (`SigurdOS-OTA` AP, upload page at `192.168.4.1`); companion TCP/WebSocket servers are parked for the AP session |
| `OTA Branch` / `Pre-releases` | GitHub OTA release-selection options |
| `OTA from GitHub` | Downloads the latest release `firmware.bin` and flashes it |
| `Shut down` / `Reboot` / `Factory reset` | Power controls with confirmation; state is saved before restart |
| `SigurdOS <version>` | Read-only — `SIGURDOS_VERSION` from `src/hal/tdeck_pins.h` |

Self-OTA rows refuse to start when the firmware detects it is running under bmorcelli/Launcher (see `docs/LAUNCHER.md`) — updating must then go through Launcher instead.

### WiFi Networks

The WiFi screen keeps the existing bounded asynchronous scan and password
dialog, and adds a live status card. It reports STA SSID, local IP, RSSI, and
the current connection error; failed authentication is cleared when a new
connection attempt starts. The Reconnect action retries the saved credentials,
while an OTA access point is shown explicitly and temporarily disables that
action. Scan status reports busy, progress, completion, and failure without
blocking the LVGL loop.

### Network / Transports

The Network category exposes independent TCP and WebSocket companion-server
toggles. Each row is refreshed from `transport_status()` and shows enabled or
parked state, connection state, client count, and the implementation detail
string. Toggle changes call `transport_set_enabled()`; persistence remains
owned by the transport layer. BLE is intentionally separate and remains under
Settings → Bluetooth. An OTA AP parks companion listeners at runtime without
rewriting their persisted enabled settings; the transport implementation must
register the `ota::setCompanionTransportParkHook()` callback and check
`ota::companionTransportsAllowed()` before enabling and while servicing.

The transport registry registers that hook during initialization. Before the
OTA AP radio is started, the park callback synchronously closes every TCP and
WebSocket client and listener. Reconciliation, receive polling, status,
explicit enable, connection lookup, and send paths all fail closed while the
OTA gate is active. When the AP lease is released, unpark only schedules normal
loop reconciliation; listeners whose persisted toggles remain enabled are
recreated on the restored WiFi network without exposing a transition window.

Hardware regression procedure: enable TCP and WebSocket on a trusted STA,
connect one client to each endpoint, then open the local OTA AP. Both existing
connections must close before the AP becomes reachable, ports 5000 and 8765
must refuse connections on the AP, and the Network rows must report `parked
for OTA`. Stop or expire the OTA session, allow WiFi ownership to return, and
confirm both listeners reopen from their unchanged enabled settings. Repeat the
sequence once to cover stop/restart cleanup.

On an unconfigured device, boot enters Onboarding as a forced navigation root:
history is cleared, no top-bar Back button is shown, and other routes are
rejected until setup persists a valid radio profile and restarts. Opening the
wizard later from System or the Home SETUP tile remains ordinary, reversible
navigation.

### Date/Time dialog

```
┌──────────────────────────┐
│ Set Date (YYYY-MM-DD)    │  ← or "Set Time (HH:MM 24h)"
│ ┌────────────────────┐   │
│ │ 2026-06-11         │   │  ← pre-filled textarea, auto-focused
│ └────────────────────┘   │
│                          │
│ [Cancel]     [Set]       │
└──────────────────────────┘
```

- **Date mode** validates `YYYY-MM-DD` (year > 2020, month 1–12, day 1–31); **time mode** validates `HH:MM` (0–23 / 0–59). Invalid input shows a red feedback label.
- On success the dialog combines the new value with the current date/time, builds an epoch via `sigurdos::mesh::makeEpoch()`, applies it with `sigurdos::mesh::setSystemTime()`, records Manual as the source, refreshes the date/time/source rows and the home-screen clock, and closes.

---

## Row Style (shared pattern)

All sub-screens use the same list-row pattern as the hub:

- **Alternating row backgrounds**: even rows `BG_TERTIARY` (`#1E1E1E`), odd rows `BG_INPUT` (`#252525`), full opacity
- **Text colour**: `TEXT_PRIMARY` (`#F2F3F5`) with an LVGL symbol icon prefix
- **Leading two-space indent** in the label text
- Rows that change state update their own label in-place via a row-label helper rather than rebuilding the screen
- Cached row pointers are nulled by `LV_EVENT_DELETE` callbacks when a screen is destroyed (stale-pointer safety)

All dialogs use zero radius, zero border width, and `dialog_size()` bounds — consistent with the pixel theme.

---

## UI Theme Integration

| Role | Colour | Hex | Usage |
|------|--------|-----|-------|
| Even row background | `BG_TERTIARY` | `#1E1E1E` | Row 0, 2, 4… |
| Odd row background | `BG_INPUT` | `#252525` | Row 1, 3, 5… |
| Unconfigured radio row | — | dark red | Warning when radio not configured |
| Row text | `TEXT_PRIMARY` | `#F2F3F5` | All row labels |
| Chevron / muted accents | `TEXT_MUTED` | `#6B7078` | `>` on hub rows |
| Dialog background | `BG_SECONDARY` | `#181818` | All modal dialog containers |
| Feedback/error text | `ACCENT_RED` | `#ED4245` | Validation errors |
| Set/Save button | `ACCENT_GREEN` | `#3BA55D` | Dialog confirm action |
| Minus button | `ACCENT_RED` | `#ED4245` | Decrement in +/- dialogs |
| Plus button | `ACCENT` | `#00BFFF` | Increment in +/- dialogs |
| Screen background | `BG_PRIMARY` | `#0F0F0F` | Full screen black via `apply_dark_bg()` |

---

## Navigation

- **Entry point**: `navigate_to(Screen::Settings)` in `src/ui/navigation.cpp` (Home → SETTINGS tile)
- **PIN gate**: if a device PIN is set and the grace window has lapsed, the PIN entry screen is shown first
- **On back**: returns to the previous screen via the navigation stack (linear 8-entry stack; drops the oldest entry when full)
- **Sub-screens**: each is a full screen reached with `navigate_to()`, so the back button walks back through the hub

---

## Further Reading

- `docs/HOME_SCREEN.md` — Home screen with the SETTINGS tile launcher
- `docs/LAUNCHER.md` — why self-OTA is gated under Launcher
- `src/hal/prefs.h` — `NodePrefs` struct definition and all persisted fields
- `src/ui/responsive.h` — `dialog_size()` helper and layout constants
- `src/ui/theme.h` — Full pixel theme colour palette
