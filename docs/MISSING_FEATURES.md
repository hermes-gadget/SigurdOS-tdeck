# Missing Features

This document catalogs features present in the MeshCore protocol and ecosystem that are not yet implemented in SlopOS-TDeck firmware. It is intended as a roadmap reference — not a bug tracker. Bugs and workarounds belong in `KNOWN_ISSUES.md`.

SlopOS-TDeck is a standalone companion-radio firmware for the LilyGo T-Deck. It interoperates with any MeshCore node but does not implement every capability the protocol supports.

All MeshCore file paths below are relative to the root of `https://github.com/meshcore-dev/MeshCore` (main branch). The submodule in this repo is pinned to a specific commit — if a symbol can't be found, check `lib/meshcore/` directly.

---

## How to use this document

Each entry describes what the feature is, what MeshCore provides, and what would be needed to implement it. Entries are grouped by category and marked with a rough effort level:

- **S** — small: isolated change, few files, testable in native tests
- **M** — medium: touches mesh layer + UI, needs device testing
- **L** — large: architectural change, multiple screens or protocol work

---

## Protocol / Packet Types

### Multipart messages (PAYLOAD_TYPE_MULTIPART 0x0A) — L

MeshCore defines a multipart packet type for segmenting large payloads across multiple LoRa frames. SlopOS currently caps all outgoing messages at 150 bytes of text (enforced in `src/mesh/slop_mesh.h:357`). Messages longer than 150 characters are silently truncated before transmission.

The library only implements one multipart subtype today — multi-ACK. There is no general reassembly buffer for arbitrary payload types. That work would need to live in SlopOS on top of `onGroupDataRecv` or `onRawDataRecv`.

**What's needed:** Implement multipart send/receive in `SlopMesh`. Increase the message input limit in the chat screen send path. Add a reassembly buffer (PSRAM) per sender.

**MeshCore reference:**
- `src/Packet.h` — `#define PAYLOAD_TYPE_MULTIPART 0x0A`
- `src/Mesh.cpp` — `case PAYLOAD_TYPE_MULTIPART:` dispatch; parses `remaining` (high nibble) and embedded subtype (low nibble)
- `src/Mesh.h` — `Mesh::createMultiAck(uint32_t ack_crc, uint8_t remaining)` — the only multipart factory currently in the library

---

### Group data datagrams (PAYLOAD_TYPE_GRP_DATA 0x06) — M

MeshCore defines typed binary group datagrams with a 16-bit type namespace for application use. `onGroupDataRecv` in `src/mesh/slop_mesh.h:176` receives `GRP_DATA` packets but only processes `GRP_TXT` — binary datagrams are silently ignored. There is no API in `mesh_wrapper.h` for sending a binary datagram.

This opens up extensible group-channel applications: shared state sync, map tile requests, sensor broadcasts, etc.

**What's needed:** Add `sendGroupDatagram(channel, type_code, data, len)` to the wrapper API. Add a callback or dispatch mechanism for received datagrams. Design a registry of type codes for SlopOS app use.

**MeshCore reference:**
- `src/Packet.h` — `#define PAYLOAD_TYPE_GRP_DATA 0x06`
- `src/Mesh.h` — `Mesh::createGroupDatagram()` (send), `virtual void onGroupDataRecv(Packet*, uint8_t type, const GroupChannel&, uint8_t* data, size_t len)` (receive callback)
- `src/helpers/TxtDataHelpers.h` — `DATA_TYPE_RESERVED` (0x0000) and `DATA_TYPE_DEV` (0xFFFF) as reserved type code boundaries

---

### Anonymous requests (PAYLOAD_TYPE_ANON_REQ 0x07) — M

SlopOS handles incoming anonymous requests in `onAnonDataRecv` (displays as `anon_XX` sender) but has no way to send one. Anonymous requests allow a node to initiate contact with another node without a prior advert exchange — the sender's public key is embedded in the packet rather than looked up from a contact.

**What's needed:** Add `sendAnonMessage(pubkey_hex, text)` to the wrapper API. Wire a UI entry point (e.g. a "Message unknown node" option in Contacts or a Terminal command).

**MeshCore reference:**
- `src/Packet.h` — `#define PAYLOAD_TYPE_ANON_REQ 0x07`
- `src/Mesh.h` — `Mesh::createAnonDatagram()` (send), `virtual void onAnonDataRecv(Packet*, const uint8_t* secret, const Identity& sender, uint8_t* data, size_t len)` (receive callback)
- `examples/simple_room_server/MyMesh.cpp` — example usage: room server uses anonymous requests for status/telemetry queries

---

### Control packets (PAYLOAD_TYPE_CONTROL 0x0B) — L

`onControlDataRecv` in `src/mesh/slop_mesh.h:291` is a stub with a `// Future` comment. MeshCore uses control packets for zero-hop neighbor discovery, capability queries, and sub-mesh management. Currently no control packets are sent or acted on.

The library dispatches control packets only for direct-routed, previously-unseen packets where the first payload byte has bit 7 set (`payload[0] & 0x80`) and the hop count is zero. No named subtype constants are defined — the application layer defines its own.

**What's needed:** Design which control sub-types SlopOS should respond to (neighbor hello, capability query). Implement handlers. The zero-hop ping (see Finder section below) depends on this.

**MeshCore reference:**
- `src/Packet.h` — `#define PAYLOAD_TYPE_CONTROL 0x0B`
- `src/Mesh.h` — `Mesh::createControlData(const uint8_t* data, size_t len)` (send), `virtual void onControlDataRecv(Packet*)` (receive callback)
- `src/Mesh.cpp` — dispatch logic: `case PAYLOAD_TYPE_CONTROL:` (only fires for zero-hop direct packets with `payload[0] & 0x80`)

---

### Raw custom payloads (PAYLOAD_TYPE_RAW_CUSTOM 0x0F) — L

`onRawDataRecv` in `src/mesh/slop_mesh.h:296` is a stub. This payload type allows unstructured encrypted byte strings over the mesh — the building block for custom apps on top of MeshCore without needing to fit the group or peer message formats.

**What's needed:** Define an application dispatch interface. Expose a registration API for future SlopOS app extensions.

**MeshCore reference:**
- `src/Packet.h` — `#define PAYLOAD_TYPE_RAW_CUSTOM 0x0F`
- `src/Mesh.h` — `Mesh::createRawData(const uint8_t* data, size_t len)` (send), `virtual void onRawDataRecv(Packet*)` (receive callback)
- `src/Mesh.cpp` — `case PAYLOAD_TYPE_RAW_CUSTOM:` fires only for direct-routed, previously-unseen packets

---

## Radio Configuration

### RX gain boost toggle — S

The SX1262 hardware supports a boosted RX sensitivity mode. This is not exposed anywhere in SlopOS — not in Settings, Radio Setup, or the Terminal.

**What's needed:** Add an `rxgain` boolean to `NodePrefs`. Expose a toggle in the Radio Setup screen. Apply the setting via the radio wrapper's `setRxBoostedGainMode()` during radio init in `src/mesh/mesh_wrapper.cpp`.

**MeshCore reference:**
- `src/helpers/radiolib/RadioLibWrappers.h` — `RadioLibWrapper::setRxBoostedGainMode(bool en)` (default no-op virtual)
- `src/helpers/radiolib/CustomSX1262Wrapper.h` — concrete override forwarding to RadioLib's `SX1262::setRxBoostedGainMode()`; same pattern in `CustomSX1268Wrapper.h`
- `src/helpers/CommonCLI.h` — `NodePrefs::rx_boosted_gain` (uint8_t) — the persisted setting; `CommonCLICallbacks::setRxBoostedGain(bool)` virtual method wired to the CLI

---

### Temporary radio config (no reboot required) — M

MeshCore companion CLI supports `tempradio freq,bw,sf,cr,timeout_mins` — a trial radio config that reverts automatically on reboot without writing to NVS. SlopOS requires a full `Save & Reboot` cycle to try any new radio parameter.

This is useful for field-testing alternative configs without losing the previous working setup.

**What's needed:** A "Try" button in Radio Setup that applies parameters live via RadioLib without writing to NVS. A timer-based or manual "Revert" that restores the last saved config.

**MeshCore reference:**
- `src/helpers/CommonCLI.h` — `handleCommand()` handles the `tempradio` command; `NodePrefs` stores a `temp_radio_timeout` field alongside the temp freq/sf/bw/cr values
- `src/helpers/radiolib/RadioLibWrappers.h` — `RadioLibWrapper::setParams(freq, bw, sf, cr)` — the live-apply method to call without persisting

---

### Duty cycle enforcement — M

EU ISM 868 MHz regulations limit airtime to 1% per hour on most sub-bands. MeshCore's `Dispatcher` tracks airtime and enforces a configurable duty cycle budget. SlopOS does not expose or display this setting, and does not show how much airtime budget remains.

**What's needed:** Surface the MeshCore duty cycle tracking in the Signal or Settings screen. Add a configurable limit to `NodePrefs`. Display remaining hourly airtime budget.

**MeshCore reference:**
- `src/Dispatcher.h` — `class mesh::Dispatcher`; key fields: `tx_budget_ms`, `duty_cycle_window_ms` (default 3 600 000 ms), `total_air_time`, `rx_air_time`
- `src/Dispatcher.h` — `virtual float getAirtimeBudgetFactor() const` — override to set TX fraction (e.g. `0.01` = 1%); `getRemainingTxBudget()` public accessor
- `src/helpers/CommonCLI.h` — `NodePrefs::duty_cycle` — persisted user setting fed into `getAirtimeBudgetFactor()`

---

### Flood max hops — S

MeshCore allows configuring the maximum hop count for flood packets. SlopOS uses the default. Dense networks benefit from a lower limit; sparse networks need a higher one.

**What's needed:** Add `flood_max_hops` to `NodePrefs`. Wire it to the MeshCore flood limit setting. Expose a dropdown in Radio Setup.

**MeshCore reference:**
- `src/helpers/CommonCLI.h` — `NodePrefs::flood_max` (uint8_t) — feeds `Mesh::setFloodMax()`
- `src/Mesh.h` — `Mesh::setFloodMax(uint8_t max)` — the setter; `Mesh::getFloodMax()` — the getter

---

### TX/RX delay tuning — S

MeshCore exposes `txdelay`, `direct.txdelay`, and `rxdelay` parameters that stagger transmissions to reduce collision probability on busy channels. These are not configurable or visible in SlopOS.

**What's needed:** Add to `NodePrefs`, expose in an advanced radio settings section.

**MeshCore reference:**
- `src/helpers/CommonCLI.h` — `NodePrefs::tx_delay_msecs`, `NodePrefs::direct_tx_delay_msecs`, `NodePrefs::rx_delay_msecs`
- `src/Dispatcher.h` — `Dispatcher::setTxDelay()`, `setDirectTxDelay()`, `setRxDelay()` — the setters consumed by the dispatcher's send scheduling

---

## GPS and Location

### Contact locations not parsed from adverts — M

When a remote node broadcasts an advert that includes GPS coordinates, `onAdvertRecv` in `src/mesh/slop_mesh.h:108` calls `parser.getName()` but never calls `parser.getLat()` or `parser.getLon()`. The location data is silently discarded.

As a result, the offline map can only show your own position — other nodes' locations are unknown to the firmware even when they are broadcasting them.

**What's needed:** Extend `SlopContact` with `float lat`, `float lon`, `bool has_location`. Read location from `AdvertDataParser` in `onAdvertRecv`. Show contact markers on the Map screen. Optionally display location in the contact detail view.

**MeshCore reference:**
- `src/helpers/AdvertDataHelpers.h` — `AdvertDataParser::hasLatLon()` (checks `flags & 0x10`), `getLat()` (returns `int32_lat / 1e6`), `getLon()` (returns `int32_lon / 1e6`)
- `src/helpers/ContactInfo.h` — `ContactInfo` struct (used in MeshCore reference implementations): shows the pattern of storing lat/lon alongside contact identity

---

### Contact node type not parsed from adverts — S

`AdvertDataParser` exposes the advertising node's type (chat node, repeater, room server, sensor). `onAdvertRecv` never reads `parser.getType()`. `SlopContact` has no `node_type` field.

This means the Contacts screen cannot distinguish repeaters from chat nodes, or show which nodes are room servers.

**What's needed:** Add `uint8_t node_type` to `SlopContact`. Read `ADV_TYPE_*` from the parser. Show a node type indicator in the Contacts list (e.g. a relay icon for repeaters, a server icon for room servers).

**MeshCore reference:**
- `src/helpers/AdvertDataHelpers.h` — `ADV_TYPE_NONE` (0), `ADV_TYPE_CHAT` (1), `ADV_TYPE_REPEATER` (2), `ADV_TYPE_ROOM` (3), `ADV_TYPE_SENSOR` (4); `AdvertDataParser::getType()` returns `flags & 0x0F`
- `src/helpers/ContactInfo.h` — `ContactInfo::type` (uint8_t) — the pattern for storing node type alongside identity in MeshCore reference implementations

---

### GPS location sharing policy — S

SlopOS currently includes GPS coordinates in the Advertise screen when a fix is available, with no user-configurable policy. Users in sensitive contexts may not want to broadcast location data at all.

**What's needed:** Add a `gps_advert_policy` enum to `NodePrefs` (none / on-fix / always-include). Expose a toggle in Settings. Gate the `broadcastAdvert(name, lat, lon)` variant behind this policy.

**MeshCore reference:**
- `src/helpers/CommonCLI.h` — `NodePrefs::gps_advert` (uint8_t); CLI values are `"none"`, `"share"`, `"prefs"` parsed in `handleCommand()` under `"gps"` subcommand

---

### Periodic auto-advert — S

Standalone MeshCore nodes broadcast periodic adverts so they are discoverable without user action. SlopOS only sends an advert when the user manually taps the Advertise screen. Nodes powered on for hours without manually advertising are invisible to new nodes.

**What's needed:** Add an `advert_interval_minutes` setting to `NodePrefs`. From the main loop (or a FreeRTOS timer), call `slopos::mesh::sendAdvert()` when the interval has elapsed. Enforce the existing cooldown to prevent accidental flooding.

**MeshCore reference:**
- `src/helpers/CommonCLI.h` — `NodePrefs::advert_interval` (uint8_t, in minutes/2; value 10 = every 20 min), `NodePrefs::flood_advert_interval` (uint8_t, in hours)
- `src/helpers/CommonCLI.h` — `CommonCLICallbacks::updateAdvertTimer()` and `updateFloodAdvertTimer()` — virtual methods called when these settings change; the concrete firmware implements the actual timer scheduling

---

### GPS clock sync — S

MeshCore firmware supports `gps sync` to set the RTC from a GPS fix. SlopOS receives GPS NMEA sentences including UTC time, and `setSystemTime()` exists in `src/mesh/mesh_wrapper.h`, but the GPS parser (`src/hal/gps.cpp`) does not automatically sync the RTC when a valid fix arrives.

**What's needed:** In the GPS update loop, when a valid fix is received and the RTC has not been synced since boot, call `slopos::mesh::setSystemTime()` with the GPS UTC epoch. Add a `gps_time_synced` status flag visible in the GPS section of Settings.

**MeshCore reference:**
- `src/helpers/CommonCLI.h` — `handleCommand()` under the `"gps"` / `"sync"` subcommand: the pattern for reading GPS time and writing it to `RTCClock::setCurrentTime()`
- `src/helpers/ArduinoHelpers.h` — `ArduinoRTCClock::setCurrentTime(uint32_t epoch)` — the setter used in reference implementations

---

## Contacts and Discovery

### Contact details screen — M

There is no per-contact detail view. Tapping a contact in the Contacts screen navigates directly to a DM. Users cannot see a contact's: public key hash, node type, last seen time, RSSI, known path (direct vs. flood), GPS location, or whether a direct path exists.

**What's needed:** A Contact Detail screen (reachable via long-press or info icon) showing all `SlopContact` fields. Include a "Send Trace" button wired to the existing `sendTrace()` API.

*(No MeshCore source reference — purely local UI work.)*

---

### QR code generation for contact sharing — L

MeshCore defines a URI scheme for sharing contacts (`meshcore://contact/add?name=<name>&public_key=<64hex>&type=<1-4>`) and channels (`meshcore://channel/add?name=<name>&secret=<hex32>`). SlopOS has no QR code generation or display.

The T-Deck's 320×240 display is large enough to show a QR code. A small software QR encoder (e.g. QRCode by Richard Moore, MIT license, ~2 KB) could be added without significant flash cost.

**What's needed:** Add a QR library. Add a "Share" button to the Contact Detail screen and Channels screen. Render the QR code in a full-screen LVGL canvas.

**MeshCore reference:**
- The URI format is documented in the MeshCore README (`meshcore://` deep-link scheme). No dedicated source file defines it — it is produced by the companion radio's `EXPORT_CONTACT` response handler in `examples/companion_radio/MyMesh.cpp`.

---

### QR code / URI import for contacts and channels — M

The inverse of the above: a user on another device shows a `meshcore://` URI. SlopOS has no camera but could accept the URI via keyboard input in a "Add by URI" dialog or Terminal command.

**What's needed:** A `meshcore://contact/add?...` parser. A Terminal command: `add contact meshcore://...`. Optionally a dedicated "Add Contact" screen with a URI text field.

**MeshCore reference:**
- `examples/companion_radio/MyMesh.cpp` — `IMPORT_CONTACT` command handler: shows the binary format the URI encodes (name + 32-byte pub key + type byte) and how it is written into the contact store.

---

### PSK channel import via UI — S

`slopos::mesh::addChannel(name, psk_base64)` exists in `src/mesh/mesh_wrapper.h:50` and is fully implemented in `src/mesh/slop_mesh.h`. However, the Channels screen only exposes hashtag channel creation — there is no UI path to add a PSK channel. The code comment in the Channels screen explicitly marks this as `// TODO`.

**What's needed:** A second option in the Channels screen: "Add PSK Channel" that prompts for name and base64 PSK. Self-contained S-effort change.

*(No MeshCore source reference — the mesh layer already supports it; this is UI-only work.)*

---

## Messaging

### Message delivery status (ACK display) — M

`onAckRecv` in `src/mesh/slop_mesh.h:285` is a stub. When a DM is sent, MeshCore may receive an ACK back from the destination. The chat screen has no sent/pending/delivered/failed state — all sent messages look the same regardless of acknowledgement.

**What's needed:** Track outgoing DM state (pending / acked / failed) in the message store. Display a status indicator in chat bubbles (single tick for sent, double tick for acked). Hook `onAckRecv` to update state by matching the ACK CRC to the pending message.

**MeshCore reference:**
- `src/Mesh.h` — `virtual void onAckRecv(Packet*, uint32_t ack_crc)` — the callback to override
- `src/Mesh.cpp` — `case PAYLOAD_TYPE_ACK:` dispatch
- `src/helpers/BaseChatMesh.h` / `src/helpers/BaseChatMesh.cpp` — `BaseChatMesh::onAckRecv()` → `processAck()`: the reference implementation that matches `ack_crc` to a pending-message table, clears `txt_send_timeout`, and calls `packet->markDoNotRetransmit()`

---

### Room server message fetch — L

MeshCore defines Room Server nodes that store up to 32 messages per user for offline retrieval. SlopOS has no concept of room servers and no fetch mechanism.

**What's needed:** Detect room server contacts by their `ADV_TYPE_ROOM` advert type (needs "Contact node type" feature above). Add a "Fetch from room server" action to the Contacts screen. Implement the REQ/RESPONSE packet exchange for message retrieval.

**MeshCore reference:**
- `examples/simple_room_server/MyMesh.h` + `MyMesh.cpp` — full room server implementation: `PostInfo` struct, cyclic post queue (`MAX_UNSYNCED_POSTS` = 32), `pushPostToClient()` — how stored messages are encoded and sent back to a client via encrypted datagrams
- Request type constants live in the same file: `REQ_TYPE_GET_STATUS`, `REQ_TYPE_GET_TELEMETRY_DATA`, `REQ_TYPE_KEEP_ALIVE`

---

### Unread message badges on home screen — S

The home screen 4×3 grid shows the CHAT icon with no indication of unread messages. A user returning to the home screen has no way to know there are new messages without navigating to Chat.

**What's needed:** A badge counter overlaid on the CHAT icon (and optionally on channel items in the chat tab bar) that increments on new messages and clears when the channel is viewed.

*(No MeshCore source reference — purely local UI work.)*

---

### Message timestamps in chat bubbles — S

Chat message bubbles display sender name and text but no timestamp. The `MeshMessage` struct already stores `timestamp` from the packet header. Users have no way to tell when a message was sent.

**What's needed:** Render a small timestamp below each message bubble (e.g. `14:32`), formatted using `getCurrentLocalDateTime()`. For messages older than 24 hours, show the date.

*(No MeshCore source reference — the timestamp is already in `MeshMessage::timestamp`; this is UI-only work.)*

---

### Channel removal — S

Once a channel is added, there is no way to remove it. The channel list fills up (max 8) with no way to evict old channels.

**What's needed:** A swipe-to-delete or long-press-to-remove gesture on channel list items. A `removeChannel(idx)` function in the mesh wrapper that shifts the channel array and persists the change.

*(No MeshCore source reference — purely local work; `SlopMesh::_channels` array management.)*

---

## Settings and Configuration

### Keyboard backlight control — S

`NodePrefs` stores a `kb_backlight` byte (0–255). The keyboard MCU accepts I2C brightness commands. However, there is no UI control to change it.

**What's needed:** A brightness slider in Settings. Wire it to `keyboard_set_backlight()` and `prefs_set()`.

*(No MeshCore source reference — T-Deck-specific HAL feature.)*

---

### Message history cap control — S

`NodePrefs` stores a `msg_cap` field controlling how many messages to retain per channel. There is no UI to change this.

**What's needed:** A numeric input in Settings for "Message history per channel." Validate against available PSRAM.

*(No MeshCore source reference — purely local.)*

---

### Display brightness control — S

Backlight brightness (0–255 via LovyanGFX PWM) is not exposed as a user setting. The display runs at full brightness regardless of conditions or user preference.

**What's needed:** Add a `display_brightness` byte to `NodePrefs`. Add a slider in Settings → Display. Apply on boot and on change via `display_set_brightness()`.

*(No MeshCore source reference — T-Deck HAL feature.)*

---

### Auto-backlight timeout control — S

The 30-second auto-off timeout is hardcoded in `src/hal/display.cpp`. Users in bright sunlight or during active use may want longer; users concerned about battery may want shorter.

**What's needed:** Add a `backlight_timeout_s` value to `NodePrefs` (0 = always on). Expose a dropdown in Settings → Display (off / 15s / 30s / 60s / 5min / always on).

*(No MeshCore source reference — T-Deck HAL feature.)*

---

### Node type selection — M

MeshCore adverts carry an `ADV_TYPE_*` flag. SlopOS always broadcasts `ADV_TYPE_CHAT`. Users running a dedicated T-Deck as a fixed relay might want to advertise as a repeater.

Note: enabling repeat mode changes packet forwarding behavior — companion nodes do not relay packets by default. This is a significant protocol change.

**What's needed:** An "Advanced" section in Settings with a node type selector. For repeater mode: enable the MeshCore relay path and increase advert frequency.

**MeshCore reference:**
- `src/helpers/AdvertDataHelpers.h` — `ADV_TYPE_CHAT` (1), `ADV_TYPE_REPEATER` (2), `ADV_TYPE_ROOM` (3), `ADV_TYPE_SENSOR` (4); `AdvertDataBuilder` constructor takes the type as first argument
- `src/helpers/CommonCLI.h` — `NodePrefs::advert_type` (uint8_t) — persisted node type; `CommonCLICallbacks` virtual methods for enabling/disabling repeat mode

---

## Diagnostics and Statistics

### TX/RX airtime display — S

MeshCore's dispatcher tracks total TX and RX airtime in milliseconds. This data is not surfaced anywhere in SlopOS. It is important for monitoring regulatory compliance (EU duty cycle) and for understanding channel utilization.

**What's needed:** Add `getTxAirtimeMs()` and `getRxAirtimeMs()` accessors to `src/mesh/mesh_wrapper.h`. Display on the Signal screen or a new Stats screen.

**MeshCore reference:**
- `src/Dispatcher.h` — `Dispatcher::getTotalAirTime()` → `total_air_time` (cumulative TX ms), `Dispatcher::getReceiveAirTime()` → `rx_air_time` (cumulative RX ms), `Dispatcher::getRemainingTxBudget()` → remaining duty-cycle budget

---

### Packet statistics (flood vs. direct counts) — S

MeshCore tracks how many packets were sent and received via flood vs. direct routing, and counts CRC errors. This is useful for diagnosing mesh health but is not visible in SlopOS.

**What's needed:** Add accessors to `src/mesh/mesh_wrapper.h`. Display on a Stats or Signal screen.

**MeshCore reference:**
- `src/Dispatcher.h` — `Dispatcher::getNumSentFlood()`, `getNumSentDirect()`, `getNumRecvFlood()`, `getNumRecvDirect()` — public counters; `resetStats()` — zeroes all counters and `_err_flags`
- `src/Radio.h` — `Radio::getEstAirtimeFor(int len_bytes)` — pure virtual; per-radio airtime estimate used by the dispatcher's budget accounting

---

### Contact SNR display — S

The `PacketLogEntry` struct stores SNR per packet. `SlopContact` stores `last_rssi` but not `last_snr`. The Contacts and Finder screens show RSSI but not SNR.

**What's needed:** Add `float last_snr` to `SlopContact`. Update in `onAdvertRecv` and `logRx`. Display alongside RSSI in Contacts and Heard screens.

*(No MeshCore source reference — local SlopOS struct extension; SNR is already available via `Packet::getSNR()`.)*

---

### Terminal command documentation (help system) — S

Also tracked in `KNOWN_ISSUES.md`. The Terminal screen has no `help` command. Users must read the source to discover commands.

**What's needed:** A `help` command that lists all available commands with one-line descriptions. See `KNOWN_ISSUES.md` for the full command list.

*(No MeshCore source reference — purely local UI work.)*

---

## User Interface

### Contact locations on Map screen — M

The Map screen shows offline tile cartography and the device's own GPS position, but does not show other nodes' positions. When contacts broadcast GPS coordinates in their adverts, those locations could be displayed as labeled markers on the map.

Depends on first implementing "Contact locations not parsed from adverts" above.

**What's needed:** Render labeled contact markers on the map canvas after contact location parsing is implemented. Markers should update on new adverts. Tapping a marker opens the contact detail screen.

*(No additional MeshCore reference beyond the advert parsing entry above.)*

---

### Signal bars widget — S

Also tracked in `KNOWN_ISSUES.md`. The Contacts and Heard screens show raw RSSI dBm numbers. A 1–5 bar widget would give users an immediate sense of link quality without reading numbers.

**What's needed:** A `lv_draw_signal_bars(obj, rssi_dbm)` helper in `src/ui/theme.h`. Apply to Contacts list items, the chat screen header, and Finder results.

*(No MeshCore source reference — purely local UI work.)*

---

### Zero-hop ping in Finder screen — M

Also tracked in `KNOWN_ISSUES.md`. The Finder screen lists contacts by RSSI but there is no active probe. Nodes that have not recently advertised are invisible even if they are in range.

**What's needed:** A "Ping Nearby" button that sends a control packet with TTL=1. A 2–3 second collection window. Display responses sorted by RSSI. 30-second cooldown. Depends on control packet implementation (see Protocol section above).

**MeshCore reference:**
- `src/Mesh.h` — `Mesh::createControlData()` — the send path for a zero-hop control ping
- `src/Mesh.cpp` — `case PAYLOAD_TYPE_CONTROL:` — how a zero-hop control packet is received and dispatched to `onControlDataRecv()`

---

### Universal trackball back-swipe — M

Also tracked in `KNOWN_ISSUES.md`. Trackball left-swipe triggers `go_back()` only on the Chat screen. All other screens require the top-left back button.

**What's needed:** Extract the swipe handler from `src/ui/chat_screen.cpp` into `src/ui/navigation.cpp`. Apply to every screen. Handle conflict with screens that use left-swipe for their own navigation (two-swipe commit pattern recommended).

*(No MeshCore source reference — purely local UI work.)*

---

### Per-contact RSSI/SNR history graph — L

The Signal screen shows the most recent RSSI and SNR as a bar chart snapshot. There is no trend view — users cannot tell if signal quality is improving or degrading over time.

**What's needed:** A circular buffer of recent RSSI/SNR samples per contact (last N packets). A sparkline or step graph on the Signal or Contact Detail screen drawn with LVGL's `lv_chart`.

*(No MeshCore source reference — purely local UI work.)*

---

### Message search — M

There is no way to search message history. As channel history grows, finding a specific message requires manual scrolling.

**What's needed:** A search icon in the chat screen top bar. A text input that filters visible messages by substring match. Navigation between matches via trackball.

*(No MeshCore source reference — purely local UI work.)*

---

## System

### OTA firmware update — L

MeshCore companion firmware supports `start ota` to initiate an over-the-air update via BLE or serial. SlopOS has no OTA mechanism — firmware updates require a USB cable and flashing tool.

**What's needed:** An OTA partition layout in `platformio.ini`. An OTA download mechanism (WiFi or BLE — the ESP32-S3 has both, neither is currently initialized). A progress indicator in the UI.

**MeshCore reference:**
- `src/helpers/CommonCLI.h` — `handleCommand()` under `"ota"` subcommand: shows when OTA mode is triggered and what pre-OTA state cleanup looks like in reference implementations
- The actual OTA transfer uses ESP-IDF's `esp_ota_ops.h` — outside the MeshCore library

---

### Graceful shutdown from UI — S

There is no way to shut down the device from the UI. The user must hold the power button. On shutdown, pending NVS writes may not complete (separate known issue in `KNOWN_ISSUES.md`).

**What's needed:** A "Shut down" option in Settings (or long-press home button). Call `saveState()`, `saveChannels()`, delay 100ms for flash writes, then enter deep sleep with no wakeup configured.

*(No MeshCore source reference — T-Deck HAL feature using ESP-IDF `esp_deep_sleep_start()`.)*

---

### Launcher compatibility — M

Also tracked in `KNOWN_ISSUES.md`. SlopOS does not run correctly under `bmorcelli/Launcher` because it expects full hardware control at boot and re-initialises peripherals from a known-zero state.

**What's needed:** A `launcher-compatible` build target (`pio run -e SlopOS_TDeck_launcher`) that skips hardware init steps already performed by the launcher and handles shared peripheral state gracefully.

*(No MeshCore source reference — purely local build/HAL work.)*

---

## Security

### ACL / contact permissions — L

MeshCore defines permission levels for contacts: Guest, Read-only, Read-write, Admin. SlopOS treats all contacts identically — any node can send a message, and all messages are displayed.

**What's needed:** Add a `uint8_t perm` field to `SlopContact`. Default to Guest. Allow the user to promote specific contacts in the Contact Detail screen. Gate certain actions (channel management, terminal commands) behind permission checks.

**MeshCore reference:**
- `src/helpers/CommonCLI.h` — `setperm` command handler in `handleCommand()`: parses permission levels 0–3 and associates them with a contact public key; `NodePrefs` stores ACL entries
- `src/helpers/CommonCLI.h` — `NodePrefs::allow_read_only` (uint8_t) — controls whether unauthenticated reads are permitted

---

### Device admin password — M

There is no password protecting the Settings or Terminal screens. Anyone with physical access to the device can change radio parameters, read all messages, and modify node identity.

**What's needed:** An optional PIN or passphrase stored in NVS (hashed). Prompt on Settings/Terminal entry. Allow a grace period after recent input activity.

**MeshCore reference:**
- `src/helpers/CommonCLI.h` — `NodePrefs::admin_password` (char array) — the pattern for storing an admin password in NodePrefs; `handleCommand()` checks it for privileged commands in reference firmware

---

## Interoperability

### BLE companion protocol (expose T-Deck as a companion radio) — L

MeshCore defines a BLE UART companion protocol (Nordic UART service, UUID `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`) that allows a smartphone app to use the T-Deck as a mesh radio modem. SlopOS does not expose this protocol — it is a standalone UI device, not a radio modem.

Implementing this would allow the Flutter `SlopOS-client` app to optionally use the T-Deck as a BLE radio rather than its standalone UI, making it dual-mode.

**What's needed:** Enable the ESP32-S3 BLE stack. Implement the companion protocol command set. Add a "BLE modem mode" toggle in Settings that suspends the standalone UI and enters companion mode.

**MeshCore reference:**
- `examples/companion_radio/MyMesh.h` + `MyMesh.cpp` — authoritative companion radio implementation; defines all `CMD_*` request bytes (e.g. `APP_START`, `SEND_TXT_MSG`, `SEND_CHANNEL_TXT_MSG`, `GET_CONTACTS`, `SET_CHANNEL`, `SEND_PATH_DISCOVERY_REQ`, `SEND_TRACE_PATH`) and all `RESP_CODE_*` / `PUSH_CODE_*` response bytes
- `src/helpers/BaseSerialInterface.h` — abstract frame transport; `MAX_FRAME_SIZE = 172` bytes; `src/helpers/ArduinoSerialInterface.h` / `.cpp` — concrete Arduino implementation used by the companion radio

---

### Region management — L

MeshCore v1.10+ allows configuring which mesh sub-regions a node participates in. This is primarily a repeater feature but affects routing in multi-region deployments.

SlopOS has no region concept. In a multi-region deployment, a T-Deck may not receive messages from all regions it could potentially reach.

**What's needed:** Design a simplified region configuration UI. Add region filter settings to `NodePrefs`. Implement region header parsing in the MeshCore packet path.

**MeshCore reference:**
- `src/helpers/RegionMap.h` — `class RegionMap`; `struct RegionEntry` (fields: `uint16_t id`, `uint16_t parent`, `uint8_t flags`, `char name[31]`); flag constants `REGION_DENY_FLOOD` (0x01), `REGION_DENY_DIRECT` (0x02); `MAX_REGION_ENTRIES` = 32
- `src/helpers/RegionMap.cpp` — `putRegion()`, `findMatch()`, `getHomeRegion()`, `setHomeRegion()`, `load()`/`save()` to filesystem
- `src/helpers/TransportKeyStore.h` — per-region transport key lookup, referenced by `RegionMap`
- `src/helpers/CommonCLI.h` — `CommonCLI::handleRegionCmd()` — CLI entry point for all `region` subcommands

---

*Last updated: 2026-05-26. Cross-reference with `KNOWN_ISSUES.md` for bugs and workarounds in implemented features.*
