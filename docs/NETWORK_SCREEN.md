# Network Screen (Finder)

The Network screen (internally called **Finder**) is SigurdOS's node discovery and network neighbourhood view. It displays nearby nodes discovered via MeshCore adverts, sorted by most recently seen, and includes a **Ping Nearby** feature to actively probe for reachable nodes on the mesh.

---

## Source Files

| File | Purpose |
|------|---------|
| `src/ui/screens/screen_finder.cpp` | Implementation — `finder_screen_show()` |
| `src/ui/screens.h` | Public API — `finder_screen_show()` declaration |
| `src/mesh/mesh_wrapper.h` / `.cpp` | Ping API — `sendPingNearby()`, `pingIsActive()`, `pingOnCooldown()`, `pingCooldownRemaining()`, `getPingResultCount()`, `getPingResult()` |
| `src/mesh/sigurd_mesh_v2.h` | BaseChatMesh subclass — `sendPingNearby()`, `onControlDataRecv()` for PING/PONG and Node Discovery handling |

---

## Layout Structure

```
┌──────────────────────────────────┐
│ ←  #general  #random         14:32│  ← top bar
├──────────────────────────────────┤
│ [Ping Nearby]                     │  ← button (or status line)
├──────────────────────────────────┤
│ ◎ NodeAlpha  3s ago  -72dBm      │  ← sorted node list
│ ◎ NodeBeta   12s ago -88dBm      │     (recent contacts or
│ ◎ NodeGamma  45s ago -91dBm      │      ping results)
│ ◎ NodeDelta  78s ago -105dBm     │
│ ...                              │
├──────────────────────────────────┤
│ SigurdOS T-Deck   ▂▄▆█       72%  │  ← bottom bar
└──────────────────────────────────┘
```

---

## Ping Nearby System

The Ping Nearby feature actively discovers which nodes are within immediate LoRa range using a lightweight zero-hop request/response protocol.

This UI feature is separate from the binary MeshCore node-discovery responder
described below.

### Protocol

```
Initiator                    Responder(s)
    │                            │
    │  ── PING:<tag> ──────────> │  (sendZeroHop, control-disco bit set)
    │                            │
    │  <── PONG:<tag>:<name>:<rssi> ──  │  (sendZeroHop, control-disco bit set)
    │  <── PONG:<tag>:<name>:<rssi> ──  │  (each reachable node responds)
    │  <── ...                        │
    │                            │
    │  ── collection window ──── │  3 seconds (PING_WINDOW_MS)
```

**Tag matching:** Each ping generates a predictable correlation tag
(`now ^ (intptr_t)this`). Matching rejects unrelated or late replies, but it is
not authentication and does not prevent a nearby sender from spoofing a PONG.
The tag is formatted as an 8-digit hex string and embedded in both payloads.

### Key Constants (from `sigurd_mesh_v2.h`)

| Constant | Value | Description |
|----------|-------|-------------|
| `PING_WINDOW_MS` | `3000` | Collection window duration in milliseconds after sending a PING |
| `PING_COOLDOWN_MS` | `30000` | Cooldown period in milliseconds (30 seconds) between successive pings |
| `PING_RESULTS_MAX` | `32` | Maximum number of unique PONG responses stored per ping |

### Wire Format

| Message | Format | Sent via |
|---------|--------|----------|
| **PING** | `"PING:%08lx"` (e.g. `PING:A3F72C81`) | `sendZeroHop()` with control-disco bit set |
| **PONG** | `"PONG:<tag>:<name>:<rssi>"` (e.g. `PONG:A3F72C81:NodeAlpha:-72`) | `sendZeroHop()` with control-disco bit set |

Both messages use `createRawData()` and set `payload[0] |= 0x80` to mark them as control-disco packets. This ensures they are handled by `onControlDataRecv()` and **not** forwarded beyond the immediate one-hop neighbourhood.

### MeshCore Node-Discovery Responder

`SigurdMeshV2::onControlDataRecv()` also implements MeshCore's binary discovery
protocol. It accepts only exact 6-byte requests or 10-byte requests with a
`since` timestamp. The request supplies a node-type filter, a correlation tag,
and a flag selecting an 8-byte public-key prefix or the full 32-byte key.

Responses echo the tag, include the device's configured advert type, and encode
the measured inbound SNR as a signed quarter-dB byte. Requests that do not match
the local type or modification time are ignored. To avoid response storms, the
device permits at most four discovery responses per 120-second window and uses
a randomized, widened zero-hop transmit delay.

### PingResult Struct

```cpp
struct PingResult {
    char name[32];   // Node's self-reported name (max 31 chars + NUL)
    int  rssi;       // RSSI of the PONG response (dBm)
};
```

Returned by `getPingResult(i)` for `i` in `[0, getPingResultCount())`.

---

## Screen Behaviour

### Ping Button (Ready State)

When `pingIsActive()` is false and `pingOnCooldown()` is false, the top area shows a styled **"Ping Nearby"** button (`ACCENT` cyan, 100×22px, zero-radius). Tapping it:

1. Calls `sigurdos::mesh::sendPingNearby()` — sends the PING with a correlation tag
2. Recreates the screen via `finder_screen_show()` — transitions to listening state

### Listening / Active State

When `pingIsActive()` returns true (a ping was sent within the last 3 seconds), the button area shows:

> 🔊 **Listening... (1/3)**

The elapsed seconds update when the screen is recreated (the screen does not poll — the countdown is a snapshot). The three-second collection window accepts PONG replies from any node whose tag matches.

### Cooldown State

After the collection window expires, the device enters a **30-second cooldown**. The status line reads:

> 📶 **Ping ready in 27s**

The countdown decrements in seconds. The Ping Nearby button is hidden until the cooldown completes and `pingOnCooldown()` returns false.

### Node List Display

The content area below the ping row is populated from one of two sources:

#### 1. Ping Results Available (`getPingResultCount() > 0`)

If the most recent ping produced results, the list shows nodes **in arrival order** (first respondent at top):

```
◎ NodeAlpha  -72dBm
◎ NodeBeta   -88dBm
◎ NodeGamma  -91dBm
```

Each row:
- Prefixed with `LV_SYMBOL_WIFI` (Wi-Fi icon)
- Shows name and RSSI (e.g. `NodeAlpha  -72dBm`)
- Alternating row colours (`BG_TERTIARY` / `BG_INPUT`)
- A footer at the bottom reads: `✓ 3 nodes responded`

#### 2. No Ping Results — Fallback to Recent Contacts

If no ping has been run yet or no results were received, the screen falls back to displaying recently seen nodes from the contact database (`exportContactsFull`):

| Filter | Value |
|--------|-------|
| Time window | Contacts seen within the last **120 seconds** |
| Sort order | Most recently seen first (descending `last_seen`) |
| Row format | `◎ NodeName  45s ago  -91dBm` |
| Exclusion | Nodes older than 120 seconds are hidden |

#### 3. Empty State

If no ping results exist, no recent contacts are available, and the device is not on cooldown, a single row reads:

> 🔊 **Listening...**

This signals that the radio is active and awaiting incoming adverts or a manual ping activation.

---

## Implementation Details

### `finder_screen_show()` (`src/ui/screens/screen_finder.cpp`)

1. Calls `make_screen_full("Finder")` to construct standard chrome.
2. Checks `getPingResultCount()` to decide between ping mode and fallback mode.
3. Builds a **ping row** (24px tall, flex row) at `CONTENT_Y + 2`:
   - **Active**: Shows `LV_SYMBOL_AUDIO` + "Listening... (elapsed/total)" in `ACCENT`
   - **Cooldown**: Shows `LV_SYMBOL_WIFI` + "Ping ready in Ns" in `TEXT_SECONDARY`
   - **Ready**: Shows an `ACCENT`-coloured button labelled "Ping Nearby"
4. Creates an `lv_list` for the node results below the ping row (`CONTENT_H - 44` tall).
5. Populates the list:
   - Ping results: iterates `getPingResult(i)` for each responder
   - Fallback contacts: bubble-sorts by `last_seen` descending, filters to ≤120s old
   - Empty: adds a single "Listening..." placeholder
6. Calls `show_screen(scr)` with slide-in animation.

### Message Handling (`onControlDataRecv` in `sigurd_mesh_v2.h`)

| Event | Action |
|-------|--------|
| **PING received** | Extracts tag from payload. Builds `PONG:<tag>:<own_name>:<rssi>`. Sends zero-hop reply. |
| **PONG received** | Verifies `_ping_sent_at != 0` and `_ping_tag != 0`. Checks collection window hasn't expired (`now_ms < _ping_sent_at + PING_WINDOW_MS`). Extracts and compares tag. Parses name and RSSI. Stores in `_ping_results[]` up to `PING_RESULTS_MAX`. |

### Re-entrancy

The ping button's event handler calls `finder_screen_show()` again to rebuild the UI in the active state. This means the screen is **re-entrant** — it fully tears down and rebuilds on each state transition.

---

## Edge Cases & Pitfalls

| Scenario | Behaviour |
|----------|-----------|
| **Multiple pings back-to-back** | Blocked by 30s cooldown. `sendPingNearby()` returns `false` if `now - _ping_last_at < 30000`. |
| **Stale PONG after window** | Rejected — if `now_ms > _ping_sent_at + PING_WINDOW_MS`, the PONG is silently dropped. |
| **PONG with wrong tag** | Rejected — tag comparison fails. A matching tag correlates a response only; PONGs are unauthenticated and spoofable. |
| **32+ responders** | Only the first 32 are recorded (`PING_RESULTS_MAX`). Later PONGs are silently dropped. |
| **Duplicate PONGs** | Not explicitly deduplicated — the same node sending multiple replies creates duplicate entries. |
| **Node renames before ponging** | The `onControlDataRecv` handler reads the name directly from the PONG payload (`remaining` → `rssi_start`), so the name is self-reported at response time. |
| **Self-ping prevention** | Handled by the mesh stack — `sendZeroHop` does not echo back to the sender. |

All PONG fields shown by Finder, including name and responder-reported RSSI,
are **untrusted self-reported data**. Do not use them as identity or proximity
proof; a contact's cryptographic identity is established through the mesh
identity/contact flow, not Ping Nearby.

---

## WiFi Radio Ownership Arbitration (PR #1414)

SigurdOS now uses a `wifi_coordinator` module (`src/hal/wifi_coordinator.h`) to
manage concurrent WiFi radio access. The WiFi radio can only serve one owner at
a time; the coordinator prevents one screen from disconnecting WiFi while
another operation (e.g. an OTA update) is in progress.

### Owner Types

| Owner | Description |
|-------|-------------|
| `None` | WiFi radio is idle |
| `Sta` | Station mode — connected to an access point (persistent lease) |
| `Scan` | WiFi Networks screen performing a site survey |
| `ApOta` | AP-mode OTA update (`SigurdOS-OTA` AP) |
| `GitHubOta` | GitHub OTA download |

### Arbitration Rules

- A `Sta` lease may be temporarily suspended by a `Scan` or OTA lease; all
  other pairings conflict.
- The coordinator uses a 2-frame depth stack. When a scan/OTA operation
  releases, the original STA lease is restored.
- The WiFi Networks screen must call `wifi::acquire(Owner::Scan, ...)` before
  starting a site survey, and `wifi::release(Owner::Scan)` when done — this
  ensures an OTA in progress is not interrupted by a UI scan.

### Async Cancellable Site Survey (PR #1435)

The WiFi Networks screen no longer performs a blocking scan. Instead:

- The scan is launched asynchronously via the coordinator
- The screen polls scan completion with a cancellable timer
- If the user navigates away, the pending scan is cancelled and WiFi ownership
  is released
- This eliminates the multi-second UI freeze that occurred on the old blocking
  scan path

## Related Screens

| Screen | Relationship |
|--------|-------------|
| **Signal** ([SIGNAL_SCREEN](SIGNAL_SCREEN.md)) | Radio stats (RSSI, SNR, noise) — same radio hardware |
| **Contacts** (`contacts_screen_show`) | Full contact list (alphabetical, all ages) — tapped to DM |
| **Packets** (`heard_screen_show`) | Raw packet log with per-packet RSSI/SNR — useful alongside ping diagnostics |
| **Advertise** (`advertise_screen_show`) | Manual advert broadcast to announce presence on the mesh |
| **WiFi Networks** (`screen_wifi_networks.cpp`) | WiFi scan and connect — uses the coordinator for radio arbitration |

---

## Mesh Health Dashboard (PR #1438)

The **Mesh Health Dashboard** (`Screen::MeshDashboard`) provides a real-time
overview of mesh network health with per-second metric refresh.

### Source Files

| File | Purpose |
|------|---------|
| `src/ui/screens/screen_mesh_dashboard.cpp` | Dashboard screen — LVGL cards, bars, 1s timer |
| `src/ui/mesh_dashboard_metrics.h` | Pure metric-aggregation functions — testable without display or radio |

### Layout

```
┌──────────────────────────────────┐
│ ←  Mesh Health              14:32│  top bar
├──────────────────────────────────┤
│ ┌────────────┐ ┌────────────┐   │
│ │  Nodes: 12  │ │ Throughput:│   │  metric cards (2×2 grid)
│ │   Active    │ │  3.2 pkt/s │   │
│ └────────────┘ └────────────┘   │
│ ┌────────────┐ ┌────────────┐   │
│ │RSSI:-72dBm │ │SNR: +8.2dB │   │
│ │   Good     │ │   Good     │   │
│ └────────────┘ └────────────┘   │
├──────────────────────────────────┤
│ TX Duty ──── [████░░░░] 23%     │  progress bars
│ CPU Util ─── [███░░░░░] 31%     │
│ Nearby Bat ─ [████████] 87%     │
├──────────────────────────────────┤
│ SigurdOS T-Deck   ▂▄▆█       72%  │  bottom bar
└──────────────────────────────────┘
```

### Metrics Displayed

| Metric | Source | Description |
|--------|--------|-------------|
| **Nodes** | Contact list count | Number of known contacts with `rssi != 0` |
| **Throughput** | Recent packet rate | Packets/sec averaged over the observation window |
| **Avg RSSI** | Contact list `rssi` values | Mean RSSI of all heard contacts (dBm) |
| **Avg SNR** | Contact list `snr` values | Mean SNR of all heard contacts (dB) |
| **TX Duty** | MeshCore duty-cycle budget | Percentage of the configured duty cycle consumed |
| **CPU Util** | Message store / queue | Utilization proxy based on activity indicators |
| **Nearby Battery** | Contact telemetry | Average battery % of nearby nodes (when available) |

### Health Status Colours

Each metric is colour-coded by health status defined in `DashboardHealth`:

| Status | Colour | Criteria (RSSI example) |
|--------|--------|------------------------|
| `Good` | `ACCENT_GREEN` | RSSI > -85 dBm |
| `Fair` | `ACCENT_ORANGE` | -105 dBm ≤ RSSI ≤ -85 dBm |
| `Poor` | `ACCENT_RED` | RSSI < -105 dBm |

### Refresh

The dashboard refreshes every **1 second** (`DASHBOARD_REFRESH_MS = 1000`) via
an LVGL timer. The timer is started when the screen is shown and cancelled when
navigating away. All aggregation functions in `mesh_dashboard_metrics.h` are
pure C++ (no LVGL, no Arduino, no mesh wrapper dependencies) — they can be
unit-tested on the host without hardware.

### Entry Point

The dashboard is reached from the home screen via a dedicated tile, or from the
Network/Finder screen via a status button.

---

## Audio / Visual Indicators

| State | Visual |
|-------|--------|
| Ping ready | Accent button `[Ping Nearby]` |
| Listening | `🔊 Listening... (1/3)` in accent |
| Cooldown | `📶 Ping ready in 27s` in secondary text |
| Results footer | `✓ 3 nodes responded` in secondary text, bottom-aligned |
| No activity | `🔊 Listening...` placeholder in the list |
