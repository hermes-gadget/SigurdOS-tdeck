# Chat Screen

The Chat screen is SigurdOS's primary messaging interface — a Discord-inspired chat application running on the LilyGo T-Deck over the MeshCore LoRa mesh network. It supports channel-based group messaging, direct messages (DMs), emoji input, message history persistence, and full trackball navigation.

---

## Source Files

| File | Purpose |
|------|---------|
| `src/ui/chat_screen.h` | Public API — `chat_screen_show()`, `chat_screen_open_dm()`, `chat_screen_add_msg()`, `chat_screen_handle_trackball()`, message cap get/set, unified-store restore |
| `src/ui/chat_screen.cpp` | Full implementation — channel list, messaging view, message bubbles, input bar, emoji picker, send logic, trackball handler, unified-store restore |
| `src/mesh/message_store.*` | Single durable message log shared by the UI and companion offline sync |
| `src/ui/chat_history_store.*` | Read-only one-time migration codec for the retired `/msgs` snapshot |
| `src/ui/navigation.cpp` | Screen routing — `navigate_to(Screen::Chat)` dispatches to `chat_screen_show()` |

---

## Entry Points

The Chat screen is reached through three paths:

| Trigger | Function | What happens |
|---------|----------|--------------|
| Home "CHATS" tile | `navigate_to(Screen::Chat)` → `chat_screen_show()` | Opens the **channel list view** |
| Contacts screen (tap a contact) | `chat_screen_open_dm("name")` | Creates or opens a "DM: name" conversation directly |
| Incoming mesh message | `chat_screen_add_msg_at(channel, sender, text, timestamp, is_self)` | Appends with the protocol timestamp; renders immediately if the channel's messaging view is currently visible |

---

## Architecture: Two Views

The Chat screen has two distinct sub-views:

### 1. Channel List View

The entry point when tapping the CHATS tile on the home screen.

```
┌──────────────────────────────────┐
│ ←  #general  #random  #help  14:32│  ← top bar with back button, channel snapshot, time
├──────────────────────────────────┤
│ [#] general                      │  ← row: avatar, channel name, preview, time, unread
│    Last message here...    14:30 │
├──────────────────────────────────┤
│ [#] random                       │
│    No messages yet               │
├──────────────────────────────────┤
│ [#] help                         │
│    Anyone around?          13:15 │
├──────────────────────────────────┤
│        [+ Add # Channel]         │  ← opens add-channel dialog
├──────────────────────────────────┤
│ SigurdOS T-Deck   ▂▄▆█       72%  │  ← bottom bar: device name, signal bars, battery
└──────────────────────────────────┘
```

#### Channel Rows

Each row in the list shows:
- **Avatar block** — dark blue (`0x5865F2`) square with `#` glyph
- **Channel name** — from mesh, e.g. `#general`, `DM: Alice`
- **Last message preview** — truncated with `LV_LABEL_LONG_DOT`
- **Timestamp** — 24h format `HH:MM` or `--:--` if no time available
- **Unread badge** — accent-cyan square with count (capped at `9+`). Counts
  live in a stable conversation-keyed registry, so opening a channels-only or
  DMs-only list cannot clear unread state for conversations hidden by that filter.
- Alternating row backgrounds: even = `BG_TERTIARY` (`0x1e1e1e`), odd = `BG_INPUT` (`0x252525`)

#### Top Bar (List View)

- **← Back button** — returns to previous screen (dims to `TEXT_MUTED` if no history)
- **Channel hashtag snapshot** — space-separated list of all known channels prefixed with `#`, auto-truncated with `LV_LABEL_LONG_DOT`
- **24h time** — right-aligned, from `mesh::getCurrentTime()`

#### Bottom Bar

- **Device name** (left) — from `mesh::getOwnName()`
- **Signal dots** (center) — iOS-style 5-dot RSSI indicator from `create_signal_dots()` in `theme.h`
- **Battery %** (right) — turns red below 20%

#### Add Channel Dialog

Opened via the `[+ Add # Channel]` button at the bottom. A modal dialog with:
- Text input for hashtag name (max `MAX_NAME_LEN` = 31 chars, placeholder `e.g. #general`)
- Submit via "Add" button or `LV_EVENT_READY` (Enter key)
- Validation feedback text (bottom-center, red)
- Calls `mesh::addHashtagChannel(name)` on submit
- On success, deletes dialog and refreshes the channel list via `refresh_chat_list_view()`
- On failure, shows "Invalid or full"

---

### 2. Messaging View

Opened by tapping a channel row or calling `chat_screen_open_dm()`.

```
┌──────────────────────────────────┐
│ ← [#general] [#random] [#help] 14:32│  ← top bar: back, scrollable channel pills, time
├──────────────────────────────────┤
│ Alice                    14:30   │  ← incoming message bubble (blue-gray)
│ ┌────────────────────────────┐   │
│ │ Hey everyone!              │   │
│ └────────────────────────────┘   │
│                                  │
│ You                     14:31   │  ← self message bubble (cyan, right-aligned)
│              ┌──────────────────┐│
│              │ Hi Alice!        ││
│              └──────────────────┘│
│                                  │
│ Bob                      14:32   │
│ ┌────────────────────────────┐   │
│ │ What's up?                 │   │
│ └────────────────────────────┘   │
├──────────────────────────────────┤
│ ┌───────────────┐ 😀 [Send]      │  ← input bar: textarea, emoji button, send button
│ │ Message       │               │
│ └───────────────┘               │
├──────────────────────────────────┤
│ SigurdOS T-Deck   ▂▄▆█       72%  │  ← bottom bar
└──────────────────────────────────┘
```

#### Top Bar (Messaging View)

- **← Back button** — returns to the channel list view via `show_channel_list(LV_SCR_LOAD_ANIM_MOVE_RIGHT)`
- **Channel ribbon** — horizontally scrollable row of channel pills. Each pill:
  - Width computed dynamically: `20 + name_length * 7`, clamped to [48, 112] px
  - Selected pill: `ACCENT` (`#00BFFF`) background with white text
  - Unselected pills: `BG_TERTIARY` (`#1E1E1E`) background with `CHANNEL_HASH` cyan text
  - Clicking a pill switches `active_channel` and re-renders messages
- **DM signal bars** — for DM conversations, shows the contact's per-node RSSI signal bars in the top bar (to the left of the time)
- **24h time** — right-aligned

#### Message List

A vertically scrollable LVGL object (`msg_list`) containing a bounded window
of at most 24 message bubbles. The backing per-channel history remains up to
the configured 200-message cap; “Load older messages” and “Return toward
newest” controls page through it by rebinding a fixed 24-bubble LVGL pool
instead of constructing objects for every stored message. Search reuses the
same pool for the 24-match page containing the active result.

**Bubble structure** (Discord-style):
- Each bubble is a flex container with the bubble body inside
- **Self messages** (`is_self = true`):
  - Background: `ACCENT` (`#00BFFF`)
  - Flex right-aligned within the container
  - Sender name: white
  - Text: white
  - Timestamp: light blue (`#b0d4ff`)
- **Incoming messages** (`is_self = false`):
  - Background: `MSG_INCOMING` (`#3A4560`)
  - Flex left-aligned
  - Sender name: `ACCENT` cyan
  - Text: `TEXT_PRIMARY` (`#f2f3f5`)
  - Timestamp: `TEXT_MUTED` (`#6b7078`)
- Text wraps with `LV_LABEL_LONG_WRAP` at 78% of message list width
- Self-DM delivery state appears beside the timestamp: no suffix while the ACK
  is pending, `✅` when confirmed, and `NO ACK` when the bounded ACK window
  expires or the 16-entry pending table has to evict it. `NO ACK` is persisted;
  pending DMs restored after reboot are also marked lost because their
  RAM-only ACK hashes can no longer be matched.
- The newest window auto-scrolls to the last bubble; incoming messages preserve
  the current scroll position when the operator is reading above the bottom

#### Input Bar

Located between the message list and bottom bar.

| Element | Details |
|---------|---------|
| **Textarea** | `apply_pixel_input()` styling, one-line mode, max length = `MAX_MSG_BYTES` (149), placeholder "Message #channel" |
| **Emoji button** | 😀 label, opens emoji picker dialog on click |
| **Send button** | "Send" label, accent background, dispatches `do_send()` on click |

**Send flow** (`do_send()`):
1. Read raw text from textarea
2. Byte-level truncation via `utf8_truncate_bytes()` to `MAX_MSG_BYTES` (149 bytes)
3. Determine destination:
   - If `active_channel` is `"DM: name"` → strip prefix, call `mesh::sendMessage(dest, text)`
   - Otherwise → call `mesh::sendChannelMessage(dest, text)`
4. Append sent message to per-channel history with `is_self = true`
5. Re-render the message list
6. Clear the textarea
7. Auto-scroll to the last message

Also triggers on `LV_EVENT_READY` (Enter key on the textarea).

#### Emoji Picker

A modal dialog with 46 hand-picked emoji in 4 categories:

| Category | Examples | Count |
|----------|----------|-------|
| Faces | 😀😂🤣😍🤔😢😭😡🥰 | 16 |
| Hands | 👍👎👌✌👏🙌🙏💪🤝👋 | 10 |
| Hearts | ❤🧡💛💚💙💜🖤💕💞💓 | 10 |
| Objects/Symbols | 🔥🎉🎊✅❌💯⭐🚀🎈💡🔔🎯🔋⚙📡🌍 | 10 |

Dialog features:
- Close button (top-right, red "X")
- "Emoji" title centered
- Scrollable grid with `LV_FLEX_FLOW_ROW_WRAP`, 28×26px buttons
- Tapping an emoji inserts it at the cursor in the input textarea and closes the picker
- Uses `emoji_font` (Noto Emoji 16px, grayscale anti-aliasing)

---

## Channel Management

### Dynamic Channels

Channels are pulled from the MeshCore mesh layer via `mesh::exportChannels()`.

- **Mesh channel capacity**: 16 exported group channels
- **Conversation registry**: 32 canonical entries — 16 mesh channels plus 16 synthetic DMs
- **Filtered views**: CHATS and DMs store non-owning indices into the registry, so switching views never removes hidden histories or unread counts
- **Sorting**: MRU (most recently used) — `active_channel` tracks the current selection
- **Auto-join**: On first load, if no channels exist, `mesh::joinPublicChannel()` is called
- **Fallback**: If the mesh still returns no channels, `#general` is created as a synthetic fallback

### Channel Pills

In the messaging view's top bar, channels are rendered as clickable pills in a horizontal scrollable ribbon. Selecting a pill:
1. Sets `active_channel` to the selected index
2. Clears the unread counter for that channel (`ch_meta[idx].unread = 0`)
3. Rebuilds the ribbon to reflect the new selection
4. Re-renders the message list via `render_active_messages()`

### Direct Messages (DMs)

DMs are synthetic channels prefixed with `"DM: "` followed by the contact name.

- **Creation**: `chat_screen_open_dm(contact_name)` resolves or reserves a DM
  slot before changing navigation. If all 16 conversation slots are occupied,
  the current screen remains active and a bounded error toast is shown.
- **Routing**: When sending, the prefix is stripped and `mesh::sendMessage(dest, text)` is called instead of `mesh::sendChannelMessage()`
- **Incoming DM routing**: When a message arrives with an empty channel field, the sender's name is wrapped as `"DM: sender"` to map it to the correct conversation
- **Signal indicator**: DM conversations show the contact's RSSI-based signal bars inline in the top bar

---

## Message Data Model

### ChannelMessage Struct

```cpp
struct ChannelMessage {
    char     sender[32];      // Node name, null-terminated
    char     text[160];       // UTF-8 message text, null-terminated
    uint32_t timestamp;       // Unix epoch seconds (from mesh RTC)
    uint32_t store_id;        // unified-store record ID for detail lookup
    bool     is_self;         // true if sent by the local node
    bool     acked;           // refreshed from the live ACK table
};
```

### Per-Channel Storage

Each conversation (up to 32: 16 mesh channels and 16 DMs) gets a circular buffer allocated on first use:

- **PSRAM path**: `heap_caps_malloc(CHAT_MSGS_MAX * sizeof(ChannelMessage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` → capacity = 200
- **DRAM fallback**: If PSRAM is exhausted, falls back to internal DRAM at reduced capacity (`CHAT_MSGS_MIN_CAP` = 8)

### Message Cap Configuration

Controlled via `NodePrefs.chat_msg_cap` (persisted in NVS):

| Constant | Value | Description |
|----------|-------|-------------|
| `CHAT_MSGS_MAX` | 200 | Absolute maximum per-channel |
| `CHAT_MSGS_DEFAULT_CAP` | 200 | Default value on fresh boot |
| `CHAT_MSGS_MIN_CAP` | 8 | Absolute minimum enforced by clamping |

- **Get**: `chat_screen_get_message_cap()` → reads from `prefs_get().chat_msg_cap`
- **Set**: `chat_screen_set_message_cap(cap)` → clamps to [8, 200], saves to NVS, and trims all channel histories
- **Trim**: `trim_channel_history(idx, cap)` uses `memmove` to drop oldest messages when count exceeds cap

### Message Append Flow

`append_channel_message(idx, sender, text, timestamp, is_self)`:
1. `ensure_channel_buffer(idx)` — allocates PSRAM/DRAM buffer if not yet allocated
2. `chat_msg_cap()` — reads the configured cap from NVS
3. `trim_channel_history(idx, cap)` — drops oldest messages if needed
4. If buffer is full, shifts all messages left by one (dropping oldest), places new message at end
5. If buffer has room, increments count and stores at the next slot
6. Updates `ch_meta[idx].preview` and `ch_meta[idx].timestamp`

### Message detail sheet

Long-press any message bubble to open its detail sheet. The bubble's
`store_id` loads the authoritative `StoredMessage` record without duplicating
route and radio metadata in every RAM cache entry. The sheet shows:

- direct, flood (with hop count), or unknown route
- RSSI/SNR when supplied by the received packet
- acknowledged, pending/unconfirmed, broadcast, or received delivery state
- send attempt when the originating send path exposed it
- plain, CLI, or signed text type
- the sender public-key prefix, with an action that copies it to chat input

The ACK row also checks the live ACK table before rendering, so an open chat
does not need to wait for another persistence reload.

---

## Persistence (SPIFFS)

`src/mesh/message_store.*` is the single durable source of truth for both the
LVGL chat UI and companion offline sync. The bounded log remains at the
historical `/companion_msgs` path to preserve upgrades. Incoming RF messages,
local sends, and app-initiated sends append immediately; the UI no longer
periodically rewrites a second snapshot.

Each record carries the conversation, sender and public-key prefix, text,
timestamp, RSSI/SNR, route provenance, send attempt (when known), companion
text type, ACK state, and companion delivery state. Store format v6 migrates
v5 records atomically, marking metadata that older callbacks did not expose as
unknown. The log holds at most 512 records. Crossing that limit retains
the newest 448 records in one atomic streaming compaction, leaving headroom so
normal appends do not cause a full rewrite every time.

### Boot restore and legacy migration (`chat_load_messages()`)

Called from `ui::init()` after mesh storage has initialized:

1. Refresh the current channel list (with Public/fallback setup).
2. If the retired `/msgs` v1 snapshot exists, validate and stream its records
   into `message_store`; deduplication makes an interrupted migration retry-safe.
3. Delete `/msgs` only after every record append succeeds. Invalid files remain
   available for diagnosis instead of being silently discarded.
4. Load the unified log's chronological RAM window, creating saved DM/channel
   conversations on demand and restoring ACK state.

The companion offline queue is a non-destructive view of unsent incoming rows.
Draining it marks rows delivered but does not remove chat history.

---

## Input Routing

### Trackball Navigation

Full navigation via the 5-direction trackball and center click.

#### Messaging View (msg_list active)

| Event | Action |
|-------|--------|
| **Up** | Scroll message list up by 44px |
| **Down** | Scroll message list down by 44px |
| **Left** | Return to channel list view (with `MOVE_RIGHT` animation) |
| **Right** | Focus the input textarea |
| **Click** | Focus the input textarea |

#### Channel List View (ch_list active)

Three focus zones: 0 = channel list, 1 = back button, 2 = add channel button.

| Event | Action |
|-------|--------|
| **Up** | Move selection up (wraps around). If on a button focus, returns to list |
| **Down** | Move selection down (wraps around). If on a button focus, returns to list |
| **Left** | Cycle focus: list → back button → list. If on add button, returns to list |
| **Right** | Cycle focus: list → add button → list. If on back button, returns to list |
| **Click** | If list row selected → open channel messaging. If back button focused → go_back(). If add button focused → trigger click |

Selection is rendered as a 2px accent border on the row (or border highlight on buttons).

### Keyboard Input

- Via the I2C keyboard (ESP32-C3 MCU at address 0x55)
- Key events routed through `display.cpp` which feeds them to LVGL input groups
- The input textarea is focused on entry to messaging view via `lv_group_focus_obj(input_field)`
- LVGL's `LV_EVENT_READY` event on the textarea triggers `do_send()` (Enter key)

### Touch Input

- The GT911 capacitive touch controller supports tapping:
  - Channel pills in the ribbon to switch channels
  - Send and Emoji buttons in the input bar
  - Channel list rows to open messaging
  - Back button to return
  - Add channel button to open the dialog
- Touch coordinates are transformed for display rotation(1): `SWAP_XY=true`, `MIRROR_X=false`, `MIRROR_Y=true`

---

## Emoji Support

### Font Architecture

The chat screen uses **emoji-wrapped Montserrat fonts** throughout:
- `emoji_wrapped_montserrat_10` — used for timestamps, sender names, badges, channel pills
- `emoji_wrapped_montserrat_12` — used for message body text, channel list names, UI labels

These fonts include the Montserrat glyph set with the `emoji_font` (`emoji_font.h`) registered as a fallback. The emoji font is a subset of Noto Emoji (16px, 4bpp grayscale anti-aliasing, SIL OFL license), generated by `scripts/generate_emoji_font.sh`.

### Inline Emoji in Messages

Because the emoji font is registered as a fallback at the LVGL level via `emoji_font_register_fallback()`, any emoji codepoint in message text or channel names is rendered automatically — no special handling needed.

### Emoji Picker

A modal dialog with 46 emoji in a scrollable grid. See the [Emoji Picker](#emoji-picker) section above for details.

---

## UI Theme Integration

All visual styling follows the SigurdOS pixel theme (`src/ui/theme.h`):

| Role | Color | Hex |
|------|-------|-----|
| Background | `BG_PRIMARY` | `#0F0F0F` |
| Bar backgrounds | `BG_SECONDARY` | `#181818` |
| Card/channel row bg | `BG_TERTIARY` | `#1E1E1E` |
| Input field bg | `BG_INPUT` | `#252525` |
| Accent (self bubble, active pill, signals) | `ACCENT` | `#00BFFF` |
| Incoming message bubble | `MSG_INCOMING` | `#3A4560` |
| Primary text | `TEXT_PRIMARY` | `#F2F3F5` |
| Secondary text | `TEXT_SECONDARY` | `#949BA4` |
| Muted text | `TEXT_MUTED` | `#6B7078` |
| Divider lines | `DIVIDER` | `#2A2A2A` |
| Channel hashtag color | `CHANNEL_HASH` | `#00BFFF` |
| Active channel text | `CHANNEL_ACTIVE` | `#FFFFFF` |

All styling uses zero-radius, `PIXEL_BORDER` (2px) minimum, no shadows — achieving the blocky "pixel" aesthetic.

---

## Layout Constants

Values are adaptive via `src/ui/responsive.h`:

| Constant | Value (320×240 portrait) | Description |
|----------|--------------------------|-------------|
| `TOP_BAR_H` | 22px | Top bar height (`DISPLAY_H / 11`, clamped 12–28) |
| `BOT_BAR_H` | 20px | Bottom bar height (`TOP_BAR_H - 2`) |
| `DIVIDER_H` | 1px | Divider line thickness |
| `INPUT_H` | 35px | Input bar height (fixed) |
| `MSG_LIST_Y` | `TOP_H + DIVIDER_H` | Message list top edge |
| `MSG_LIST_H` | `DISPLAY_H - TOP_H - DIVIDER_H - INPUT_H - DIVIDER_H - BOT_BAR_H` | Message list height |
| `BUBBLE_PAD` | 6px | Padding around message bubbles |
| `LIST_ROW_H` | 44px | Channel list row height |
| `LIST_BAR_H` | 22px | Channel list top bar |
| `MAX_MSG_BYTES` | 149 | Max text bytes for mesh payload |
| `MAX_NAME_LEN` | 31 | Max chars for channel/contact name |

---

## Public API Reference

### `chat_screen_show()`

Opens the Chat screen from the channel list view. Clears the back button reference (`screens_clear_back_btn()`) and shows the channel list with a `MOVE_LEFT` animation.

Called from: `navigate_to(Screen::Chat)` in `navigation.cpp`.

### `chat_screen_open_dm(const char* contact_name)`

Opens or creates a direct message conversation with a contact.

1. Pushes `Screen::Chat` onto the navigation stack via `navigate_to()`
2. Refreshes channels from mesh
3. Constructs the channel name `"DM: <contact_name>"`
4. Searches for an existing DM channel; creates one if not found and slots are available
5. Opens the messaging view for the DM channel

### `chat_screen_add_msg(const char* channel, const char* sender, const char* text, bool is_self)`

Adds a message to the per-channel history. The primary entry point for incoming mesh messages (called from `ui::loop()` → `mesh::pollMessages()`).

- Empty `channel` is auto-mapped to `"DM: <sender>"` (DM routing)
- Unknown conversations are auto-created while the 32-entry registry has capacity
- If the channel's messaging view is currently visible, the bubble is rendered immediately
- Unread count is incremented for background channels
- Automatically trims excess LVGL label widgets if display cap is exceeded

This compatibility entry point supplies timestamp zero, which falls back once
to `mesh::getCurrentTime()`.

### `chat_screen_add_msg_at(..., uint32_t timestamp, bool is_self)`

Timestamp-aware ingress used by `ui::loop()`. The authoritative
`MeshMessage.timestamp` is stored in channel history, channel metadata, the
rendered bubble, and the unified log. A zero protocol timestamp uses the
same local-clock fallback as the compatibility entry point.

### `chat_screen_add_stored_msg(..., uint32_t store_id)`

Ingress used by `ui::loop()` after the durable-first mesh fan-out. It preserves
the unified-store record ID in the RAM bubble cache so long-press detail lookup
can seek the exact record, including duplicate text/timestamps.

### `chat_screen_handle_trackball(SigurdOSTrackballEvent event) -> bool`

Routes trackball events to the active sub-view. Returns `true` if the event was consumed.

Called from: `ui::handle_trackball_event()` in `ui.cpp`.

### `chat_screen_get_input_field() -> lv_obj_t*`

Returns the input textarea object if the messaging view is active, `nullptr` otherwise. Used by the keyboard input routing in `display.cpp`.

### `chat_screen_get_message_cap() -> uint16_t`

Returns the configured per-channel message history cap from NVS.

### `chat_screen_set_message_cap(uint16_t cap)`

Sets the message cap (clamped to [8, 200]), persists to NVS, and immediately trims all channel histories.

### `chat_load_messages()`

Migrates a valid legacy `/msgs` snapshot once, then restores the UI RAM window
from the unified message log.

---

## Known Issues

See `docs/KNOWN_ISSUES.md` for the current tracker.

Previously listed here, since fixed:

- **Dual message stores**: `/msgs` is now migration-only; all new writes and boot restores use `message_store`
- **Navigation history stack**: now a linear 8-entry stack (`src/ui/navigation.cpp`) that drops the oldest entry when full — no circular wrap
- **LVGL tick starvation**: the mesh loop now services `lv_timer_handler()` periodically during long radio operations (`src/mesh/mesh_wrapper.cpp`)
