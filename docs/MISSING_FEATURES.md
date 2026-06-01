# Missing Features

This document catalogs features present in the MeshCore protocol and ecosystem that are **not yet implemented** in SlopOS-TDeck firmware. It is a roadmap reference — not a bug tracker. Bugs and workarounds belong in `KNOWN_ISSUES.md`.

SlopOS-TDeck is a standalone **companion-radio firmware** for the LilyGo T-Deck. It interoperates with any MeshCore node and is designed for the end-user handheld experience — not for infrastructure roles (dedicated repeaters, room servers, sensors). Features are tagged to distinguish companion-relevant from infrastructure-only items. For build order, dependencies, and step-by-step implementation guidance, see [`ROADMAP.md`](ROADMAP.md).

## Where to find things in upstream MeshCore

Every reference below links directly into **`https://github.com/meshcore-dev/MeshCore`** (main branch) so other agents can jump straight to the source. The repo submodule (`lib/meshcore/`) is pinned to companion firmware **v1.15.0 / `FIRMWARE_VER_CODE 11`** ([`examples/companion_radio/MyMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.h)). Line numbers drift between versions — references cite **symbol names**, so grep the linked file if a line has moved. If a symbol can't be found upstream, check `lib/meshcore/` directly (the pinned commit is authoritative for what SlopOS actually builds against).

The single most useful reference is the companion radio command dispatcher:
[`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `handleCmdFrame()` defines all **64 `CMD_*` request codes**, **28 `RESP_CODE_*` reply codes**, and **17 `PUSH_CODE_*` async push codes**. Almost every protocol feature below has a worked example in this one file.

### ⚠️ Architectural note — read before estimating effort

The companion radio (`MyMesh`) extends **`BaseChatMesh`** ([`src/helpers/BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h)), which provides ready-made high-level helpers: `sendLogin()`, `sendCommandData()`, `sendRequest()`, `resetPathTo()`, `processAck()` with an `expected_ack_table`, an offline message queue, and contact-by-pubkey lookup.

**SlopOS's `SlopMesh` extends `::mesh::Mesh` *directly*** (`src/mesh/slop_mesh.h`), one layer lower. It therefore does **not** inherit any of those helpers — they are reimplemented (partially) or absent. For each missing feature below, the implementer chooses one of:
- **Port the `BaseChatMesh` method** into `SlopMesh` (usually the fastest path — the logic already exists), or
- **Reimplement on raw `Mesh`** using `createDatagram()` / `createAnonDatagram()` / `sendRequest`-style primitives.

This is why several "the library already does this" features still require real work in SlopOS.

---

## How to use this document

Each entry describes the feature, what MeshCore provides, and what implementing it in SlopOS would take. Effort levels:

- **S** — small: isolated change, few files, testable in native tests
- **M** — medium: touches mesh layer + UI, needs device testing
- **L** — large: architectural change, multiple screens or protocol work

---

## Infrastructure Interaction (companion-relevant)

> These are **companion** features even though they involve infrastructure nodes. A handheld companion routinely logs into repeaters/room servers, pulls their status/telemetry, and discovers paths. SlopOS currently does none of this. All of these have a complete worked implementation in `BaseChatMesh` + the companion radio — the gap is that `SlopMesh` extends `Mesh` directly (see architectural note above).

### Repeater / room-server login + remote administration — L

A companion logs into a repeater or room server with a password, receiving a permission level (guest / admin), then issues CLI admin commands over the mesh (`set freq`, `reboot`, `set name`, etc.) as encrypted COMMAND-type datagrams. This is how the official app administers remote infrastructure. SlopOS has no login, no session/keep-alive, and no remote-command path.

**What's needed:** Port `sendLogin()` and `sendCommandData()` into `SlopMesh`. Track login sessions (keep-alive seconds, permission byte). Add a "Login / Admin" action on repeater/room contacts in Contacts, with a password field and a command console. Parse the login response for the permission level.

**MeshCore reference:**
- [`src/helpers/BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h) — `sendLogin(recipient, password, est_timeout)`, `sendCommandData(recipient, timestamp, attempt, text, est_timeout)`, `startConnection()` / `stopConnection()` (keep-alive session)
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_LOGIN` (26) and `CMD_LOGOUT` (29) handlers; `onContactResponse()` parses `RESP_SERVER_LOGIN_OK`, extracts keep-alive + permission/ACL bytes, pushes `PUSH_CODE_LOGIN_SUCCESS` / `PUSH_CODE_LOGIN_FAIL`; `onCommandDataRecv()` / `onSignedMessageRecv()` show how admin command replies arrive
- [`examples/simple_repeater/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_repeater/MyMesh.cpp) — the server side: how a repeater authenticates a login and executes admin commands

---

### Status request (repeater / room server health) — M

`CMD_SEND_STATUS_REQ` asks an infrastructure node for a status blob (uptime, battery, airtime, TX/RX queue depth, free heap). The companion displays this so a user can check a remote repeater without physical access. SlopOS cannot query node status.

**What's needed:** Port the REQ send path for a status request, match the response by pubkey/tag, parse the status struct, and show it on a "Node status" detail panel.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_STATUS_REQ` (27) handler; `onContactResponse()` builds `PUSH_CODE_STATUS_RESPONSE`
- [`examples/simple_repeater/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_repeater/MyMesh.cpp) — `REQ_TYPE_GET_STATUS` handler builds the status payload (the canonical field layout)
- [`src/helpers/StatsFormatHelper.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/StatsFormatHelper.h) — status/stats field formatting

---

### Telemetry queries (remote + self, CayenneLPP) — M

MeshCore carries sensor telemetry in CayenneLPP format. `CMD_SEND_TELEMETRY_REQ` queries a remote node's telemetry (battery voltage, environment sensors, GPS); a length-4 self-request returns the device's own telemetry. SlopOS exposes none of this — neither requesting a remote node's battery/sensors nor reporting its own over the mesh.

**What's needed:** Port `sendRequest(recipient, REQ_TYPE_GET_TELEMETRY_DATA, …)`. Decode the CayenneLPP response and render channels (voltage, temp, humidity, lat/lon). Optionally answer inbound telemetry requests with the T-Deck's own battery via `onContactRequest()`.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_TELEMETRY_REQ` (39): remote variant calls `sendRequest(*recipient, REQ_TYPE_GET_TELEMETRY_DATA, …)`; `len==4` self variant fills a `CayenneLPP telemetry` with `addVoltage(TELEM_CHANNEL_SELF, …)` + `sensors.querySensors()` and pushes `PUSH_CODE_TELEMETRY_RESPONSE`; `onContactRequest()` answers `REQ_TYPE_GET_TELEMETRY_DATA`
- `REQ_TYPE_GET_TELEMETRY_DATA` (0x03) is defined at the top of [`examples/companion_radio/MyMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.h)
- [`src/helpers/SensorManager.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/SensorManager.h) and [`src/helpers/sensors/`](https://github.com/meshcore-dev/MeshCore/tree/main/src/helpers/sensors) — the sensor/telemetry framework and CayenneLPP usage

---

### Path discovery request — M

Distinct from Trace (which probes an *already-known* path). `CMD_SEND_PATH_DISCOVERY_REQ` actively asks the mesh to discover a route to a contact whose path is unknown, returning the path in a `PAYLOAD_TYPE_RESPONSE`. SlopOS only floods blindly when `out_path_len == OUT_PATH_UNKNOWN` and has no explicit discovery action.

**What's needed:** Add a "Discover path" action on contacts. Send the discovery REQ, match the response by tag, store the learned path into `SlopContact::out_path`.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_PATH_DISCOVERY_REQ` (52) handler; `onContactPathRecv()` matches `pending_discovery` against `PAYLOAD_TYPE_RESPONSE` extra data and pushes `PUSH_CODE_PATH_DISCOVERY_RESPONSE`

---

### Reset path to a contact — S

When a learned path goes stale (a repeater moves or dies), messages keep failing on the dead route until the path is cleared. `CMD_RESET_PATH` wipes the stored out-path so the next message floods and re-learns. SlopOS stores `out_path` in `SlopContact` but offers no way to clear it from the UI.

**What's needed:** A "Reset path" action on the contact detail screen that sets `out_path_len = OUT_PATH_UNKNOWN` and persists. Port `resetPathTo()` semantics (it also notifies the mesh).

**MeshCore reference:**
- [`src/helpers/BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h) — `resetPathTo(ContactInfo& recipient)`
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_RESET_PATH` (13) handler

---

### Generic binary request framework (REQ/RESPONSE) — M

`CMD_SEND_BINARY_REQ` is the generalised request primitive that status/telemetry are now built on: send arbitrary `req_data` to a contact, match the `RESPONSE` by a 4-byte tag. Implementing this once gives status, telemetry, room-fetch, and future app requests for free.

**What's needed:** Port `sendRequest(recipient, req_data, data_len, tag, est_timeout)` and a tag→handler dispatch table. This is the foundation for the four entries above and "Room server message fetch".

**MeshCore reference:**
- [`src/helpers/BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h) — both `sendRequest()` overloads
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_BINARY_REQ` (50) handler; `onContactResponse()` matches `pending_req` by tag, pushes `PUSH_CODE_BINARY_RESPONSE`

---

## Protocol / Packet Types

### Message delivery status (ACK display) — M

`onAckRecv()` in `src/mesh/slop_mesh.h` (currently a no-op comment, the `onAckRecv` override) does nothing with received ACKs. The chat screen shows no sent/pending/delivered/failed state — every sent message looks identical regardless of acknowledgement.

**What's needed:** Track outgoing DM state (pending / acked / failed) in the message store. Display a status tick in chat bubbles. Hook `onAckRecv` to match the 4-byte ACK hash to the pending message. Because `SlopMesh` extends `Mesh` directly, port the `BaseChatMesh` ACK table rather than expecting it to exist.

**MeshCore reference:**
- [`src/Mesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Mesh.h) — `virtual void onAckRecv(Packet*, uint32_t ack_crc)`. **The value is NOT a CRC-32** — it is the first 4 bytes of SHA-256 over a message-type-dependent buffer including the recipient's public key. Implementing CRC-32 will never match.
- [`src/helpers/BaseChatMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.cpp) — `BaseChatMesh::onAckRecv()` → `processAck()`: the reference table-matching implementation; clears `txt_send_timeout` and calls `packet->markDoNotRetransmit()`
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `processAck()` with circular `expected_ack_table[8]`, pushes `PUSH_CODE_SEND_CONFIRMED` with round-trip time

---

### Multipart messages (PAYLOAD_TYPE_MULTIPART 0x0A) — ❌ NOT DOING

- **Reason:** User declined — not implementing. 150-byte send cap remains.
- **Reference (for posterity):** MeshCore defines a multipart packet type for segmenting large payloads across LoRa frames. [`src/Packet.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — `#define PAYLOAD_TYPE_MULTIPART 0x0A`

---

### Group data datagrams (PAYLOAD_TYPE_GRP_DATA 0x06) — M

`onGroupDataRecv` in `slop_mesh.h` accepts both `GRP_TXT` and `GRP_DATA` but renders everything as text — there is no type-code dispatch for binary group datagrams and no API in `mesh_wrapper.h` to *send* one. The companion radio exposes this via `CMD_SEND_CHANNEL_DATA` with a 16-bit type namespace, enabling shared-state sync, sensor broadcasts, etc.

**What's needed:** Add `sendGroupDatagram(channel, type_code, data, len)` to the wrapper. Add a dispatch table for received datagram type codes.

**MeshCore reference:**
- [`src/Packet.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — `#define PAYLOAD_TYPE_GRP_DATA 0x06`
- [`src/helpers/BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h) — `sendGroupData(channel, path, path_len, data_type, data, data_len)`
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_CHANNEL_DATA` (62); `onChannelDataRecv()` shows the `data_type` dispatch and `RESP_CODE_CHANNEL_DATA_RECV`
- [`src/helpers/TxtDataHelpers.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/TxtDataHelpers.h) — `DATA_TYPE_RESERVED` (0x0000) / `DATA_TYPE_DEV` (0xFFFF) reserved type-code boundaries

---

### Anonymous requests (PAYLOAD_TYPE_ANON_REQ 0x07) — M

SlopOS *receives* anonymous requests in `onAnonDataRecv` (shown as `anon_XX`) but cannot send one. Anonymous requests let a node contact another without a prior advert exchange — the sender's pubkey is embedded in the packet. Used for first-contact login to room servers.

**What's needed:** Add `sendAnonMessage(pubkey_hex, text)` to the wrapper. Wire a "Message unknown node" entry in Contacts or a Terminal command.

**MeshCore reference:**
- [`src/Packet.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — `#define PAYLOAD_TYPE_ANON_REQ 0x07`
- [`src/Mesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Mesh.h) — `Mesh::createAnonDatagram()` (send), `onAnonDataRecv()` (receive)
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_ANON_REQ` (57) handler
- [`examples/simple_room_server/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_room_server/MyMesh.cpp) — room server uses anon requests for first-contact login

---

### Direct request/response (PAYLOAD_TYPE_REQ 0x00 / RESPONSE 0x01) — M

`onPeerDataRecv` in `slop_mesh.h` now *accepts* inbound REQ/RESPONSE payloads but treats them as plain text messages — there is no type dispatch and no *send* path. Implementing the generic binary-request framework (see Infrastructure Interaction above) is the clean way to add this.

**What's needed:** See "Generic binary request framework" — that entry covers the send path and tag-matched dispatch. Room-server fetch and path discovery build on it.

**Core Protocol Spec reference:**
- `§2.9` — Direct-encrypted REQ (0x00) / RESPONSE (0x01) share the TEXT/PATH wire format (dest hash + encrypted data)
- [`examples/simple_room_server/MyMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_room_server/MyMesh.h) — `REQ_TYPE_GET_STATUS`, `REQ_TYPE_GET_TELEMETRY_DATA`, `REQ_TYPE_KEEP_ALIVE`

---

### Control packets (PAYLOAD_TYPE_CONTROL 0x0B) — L

`onControlDataRecv` in `slop_mesh.h` implements a SlopOS-specific PING/PONG over zero-hop control packets (the Finder "Ping Nearby" feature). No other control sub-types (capability query, neighbour hello, sub-mesh management) are handled. The library only dispatches control packets for direct-routed, previously-unseen, zero-hop packets where `payload[0] & 0x80` is set; subtypes are application-defined.

**What's needed:** Design which control sub-types SlopOS should answer. Note the companion radio exposes this generically via `CMD_SEND_CONTROL_DATA` / `PUSH_CODE_CONTROL_DATA`.

**MeshCore reference:**
- [`src/Packet.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — `#define PAYLOAD_TYPE_CONTROL 0x0B`
- [`src/Mesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Mesh.h) — `Mesh::createControlData()`, `onControlDataRecv()`
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_CONTROL_DATA` (55) handler

---

### Raw custom payloads (PAYLOAD_TYPE_RAW_CUSTOM 0x0F) — ❌ NOT DOING

- **Reason:** User declined — not implementing.
- **Reference (for posterity):** `onRawDataRecv` in `slop_mesh.h` is a stub. (Note: SlopOS uses `createRawData()` internally for PING/PONG, but there is no general app dispatch.) [`src/Packet.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — `#define PAYLOAD_TYPE_RAW_CUSTOM 0x0F`

---

## Radio Configuration

### RX gain boost toggle — S

The SX1262 supports a boosted RX sensitivity mode, unexposed in SlopOS. (`applyRadioParams()` in `mesh_wrapper.h` already takes an `rx_gain` argument — the plumbing exists; what's missing is the persisted pref + Settings toggle.)

**What's needed:** Add `rx_boosted_gain` to `NodePrefs`. Add a toggle in Radio Setup. Apply via `setRxBoostedGainMode()` at radio init.

**MeshCore reference:**
- [`src/helpers/radiolib/RadioLibWrappers.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/radiolib/RadioLibWrappers.h) — `RadioLibWrapper::setRxBoostedGainMode(bool)`
- [`src/helpers/radiolib/CustomSX1262Wrapper.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/radiolib/CustomSX1262Wrapper.h) — concrete override
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `NodePrefs::rx_boosted_gain`

---

### Temporary radio config (no reboot) — M

MeshCore's CLI supports `tempradio freq,bw,sf,cr,timeout_mins` — a trial config that auto-reverts on reboot without writing NVS. SlopOS has `applyRadioParams()` / `revertRadioParams()` in the wrapper (live apply without NVS), but no auto-revert timer and no Radio Setup "Try" UI exposing it.

**What's needed:** A "Try (no save)" button in Radio Setup wired to `applyRadioParams()`, plus a timeout that calls `revertRadioParams()`.

**MeshCore reference:**
- [`src/helpers/CommonCLI.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/CommonCLI.cpp) — `handleCommand()` `tempradio` branch; `temp_radio_timeout` in NodePrefs
- [`src/helpers/radiolib/RadioLibWrappers.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/radiolib/RadioLibWrappers.h) — `setParams(freq, bw, sf, cr)`

---

### Duty cycle enforcement — M

EU ISM 868 MHz limits airtime to 1%/hour. `SlopMesh` already overrides `getAirtimeBudgetFactor()` and exposes `setDutyCycle()` / `getRemainingTxBudget()` in the wrapper — but there is **no Settings UI** to configure the limit and **no display** of remaining budget.

**What's needed:** Add a duty-cycle limit control in Settings; display remaining hourly airtime budget on the Signal screen via `getRemainingTxBudget()`.

**MeshCore reference:**
- [`src/Dispatcher.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Dispatcher.h) — `tx_budget_ms`, `duty_cycle_window_ms`, `getAirtimeBudgetFactor()`, `getRemainingTxBudget()`
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `airtime_factor`

---

### Auto-add contact configuration — M

`onAdvertRecv` in `slop_mesh.h` already gates auto-add by a `flood_max_hops` pref, but auto-adds **all** contact types unconditionally. The companion protocol (`CMD_SET_AUTOADD_CONFIG` / `GET`) configures *which types* to auto-add and an overwrite-oldest policy.

Per-type bitmask: `AUTO_ADD_CHAT` (0x02), `AUTO_ADD_REPEATER` (0x04), `AUTO_ADD_ROOM_SERVER` (0x08), `AUTO_ADD_SENSOR` (0x10), `AUTO_ADD_OVERWRITE_OLDEST` (0x01).

**What's needed:** Add `autoadd_config` + `autoadd_max_hops` to `NodePrefs`. Gate auto-add by type in `onAdvertRecv`. Add a Settings checklist.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.h) — `isAutoAddEnabled()`, `shouldAutoAddContactType()`, `shouldOverwriteWhenFull()`, `getAutoAddMaxHops()`, `AUTO_ADD_*`
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SET_AUTOADD_CONFIG` (58) / `CMD_GET_AUTOADD_CONFIG` (59); `onDiscoveredContact()` filtering; `onContactOverwrite()`
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `autoadd_config`, `autoadd_max_hops`, `manual_add_contacts`

---

### Custom variables (key-value store) — S

`CMD_GET_CUSTOM_VARS` / `CMD_SET_CUSTOM_VAR` provide a named `name:value` key-value store for vendor-specific config (GPS tuning, sensor calibration). SlopOS has no equivalent — every config needs a new `NodePrefs` field + firmware change.

**What's needed:** A small NVS-backed key-value store, exposed via a Terminal command.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_GET_CUSTOM_VARS` (40) formats comma-separated `name:value`; `CMD_SET_CUSTOM_VAR` (41) parses `name:value` → `sensors.setSettingValue()`; pushes `RESP_CODE_CUSTOM_VARS`
- `Core Protocol Spec Part 2 §2.5.9` — wire format

---

## GPS and Location

### Advert location-share policy (privacy) — S

`SlopMesh::broadcastAdvert()` has a lat/lon overload and SlopOS reports `getLastAdvertUsedGps()` — so location *can* go into adverts. What's missing is the **policy toggle**: the companion `NodePrefs::advert_loc_policy` (`ADVERT_LOC_NONE` = 0, `ADVERT_LOC_SHARE` = 1) lets the user decide whether their position is broadcast. SlopOS has no privacy control over this.

**What's needed:** Add `advert_loc_policy` to `NodePrefs`. A Settings toggle "Share my location in adverts". Gate the lat/lon advert overload on it.

**MeshCore reference:**
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `ADVERT_LOC_NONE` / `ADVERT_LOC_SHARE`, `NodePrefs::advert_loc_policy`
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SET_ADVERT_LATLON` (14) handler

---

### GPS enable / read-interval control — S

The companion stores `gps_enabled` and `gps_interval` (seconds) and applies them through the sensor/custom-var system (`applyGpsPrefs()`). SlopOS parses NMEA (`hal/gps.cpp`) but offers no UI to enable/disable the GPS or set its polling interval — affecting battery life.

**What's needed:** Add `gps_enabled` + `gps_interval` to `NodePrefs`. Settings controls. Gate `gps.cpp` polling on them.

**MeshCore reference:**
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `gps_enabled`, `gps_interval`
- [`examples/companion_radio/MyMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.h) — `applyGpsPrefs()` (sets `gps` / `gps_interval` setting values)

---

### Periodic auto-advert — S

Companion nodes can broadcast periodic adverts so they stay discoverable. SlopOS only adverts on manual taps of the Advertise screen — a node left on for hours is invisible to new nodes. Optional, user-toggled.

**What's needed:** Add `advert_interval` / `flood_advert_interval` to `NodePrefs`. A loop timer that re-adverts on the interval. A Settings toggle.

**MeshCore reference:**
- [`src/helpers/CommonCLI.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/CommonCLI.h) — `NodePrefs::advert_interval` (minutes/2), `flood_advert_interval` (hours); `CommonCLICallbacks::updateAdvertTimer()` / `updateFloodAdvertTimer()`

---

### Contact locations on Map screen — M

The Map screen shows offline tiles and own GPS position but not other nodes. Contacts already carry `has_location` / `latitude` / `longitude` in `SlopContact` (advert parsing is done) — the data is available, only the rendering is missing.

**What's needed:** Render labelled contact markers on the map canvas; update on new adverts; tap a marker → contact detail.

*(No additional MeshCore reference — `SlopContact` already has the coordinates.)*

---

## Contacts and Discovery

### Contact removal — S

SlopOS can add contacts (auto + via advert) but offers **no way to delete one**. The list is a fixed 64-entry array with LRU eviction only when full; the user cannot manually remove a stale or unwanted contact.

**What's needed:** A "Remove contact" action on the contact detail screen that compacts the `_contacts[]` array and persists. (Channel removal is a separate entry under Messaging.)

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_REMOVE_CONTACT` (15) handler; `PUSH_CODE_CONTACT_DELETED` (0x8F)

---

### Identity backup — export / import private key — M

The node's identity *is* its private key — losing it means losing your address on the mesh, and there is no recovery. The companion exposes `CMD_EXPORT_PRIVATE_KEY` / `CMD_IMPORT_PRIVATE_KEY` so a user can back up and restore identity. SlopOS has no export, import, or backup of its identity.

**What's needed:** A "Back up identity" action (export hex/QR) and an "Import identity" path (re-key the node). Handle the re-key carefully — contacts' shared secrets must be recomputed.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_EXPORT_PRIVATE_KEY` (23) → `RESP_CODE_PRIVATE_KEY`; `CMD_IMPORT_PRIVATE_KEY` (24)
- [`src/helpers/IdentityStore.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/IdentityStore.h) — identity load/save to filesystem

---

### QR code generation for contact/channel sharing — L

MeshCore uses a deep-link URI scheme for sharing (`meshcore://contact/add?name=…&public_key=<64hex>&type=<1-4>` and `meshcore://channel/add?name=…&secret=<hex32>`). The 320×240 display can show a QR; a tiny MIT-licensed QR encoder (~2 KB) fits.

**What's needed:** Add a QR library. "Share" buttons on Contact Detail and Channels. Render to an LVGL canvas.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SHARE_CONTACT` (16) / `CMD_EXPORT_CONTACT` (17) handlers produce the binary the URI encodes (name + 32-byte pubkey + type byte)

---

### QR code / URI import for contacts and channels — M

The inverse: accept a `meshcore://…` URI by keyboard (no camera) in an "Add by URI" dialog or Terminal command.

**What's needed:** A URI parser; a Terminal `add contact meshcore://…` command; optionally a dedicated Add Contact screen.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_IMPORT_CONTACT` (18) handler shows the binary contact format

---

## Messaging

### Room server message fetch — L

A companion out of range of a room server later wants to sync missed messages. Built on the binary-request framework (above): detect `ADV_TYPE_ROOM` contacts (already parsed into `SlopContact::type`), log in, fetch stored posts.

**What's needed:** Depends on "Repeater/room login" + "Generic binary request framework". Add a "Fetch from room" action; parse the response and merge into the message store.

**MeshCore reference:**
- [`examples/simple_room_server/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_room_server/MyMesh.cpp) — `PostInfo`, cyclic post queue (`MAX_UNSYNCED_POSTS` = 32), `pushPostToClient()` (how stored messages are encoded back to a client)

---

### Channel removal — S

Once added there is no way to remove a channel; the 8-slot list fills with no eviction.

**What's needed:** A long-press/swipe remove on channel list items; a `removeChannel(idx)` that shifts `SlopMesh::_channels` and persists.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SET_CHANNEL` (32) with an empty/zero channel clears a slot (the protocol-level removal idiom)

---

### Message timestamps in chat bubbles — S

Bubbles show sender + text but no time. `MeshMessage::timestamp` already holds the packet timestamp — UI-only work.

**What's needed:** Render a small `HH:MM` under each bubble via `getCurrentLocalDateTime()`; show the date for messages >24 h old.

*(No MeshCore reference — data already present in `MeshMessage`.)*

---

### Message search — M

No way to search history; finding a message means scrolling.

**What's needed:** A search icon in the chat top bar; substring filter; trackball navigation between matches.

*(No MeshCore reference — local UI work.)*

---

### Per-contact RSSI/SNR history graph — L

The Signal screen shows a snapshot bar chart, no trend. `SlopContact` keeps only `last_rssi` / `last_snr`.

**What's needed:** A per-contact circular buffer of recent samples; an `lv_chart` sparkline on Signal or Contact Detail.

*(No MeshCore reference — local UI work.)*

---

## Settings and Configuration

### Factory reset — S

`CMD_FACTORY_RESET` wipes prefs, contacts, channels, and identity to a clean state. SlopOS has no reset path — recovering from a corrupt config means reflashing.

**What's needed:** A "Factory reset" action in Settings (double-confirm). Clear NVS prefs, contacts, channels, and regenerate identity. Delay for flash writes, then reboot.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_FACTORY_RESET` (51) handler (guarded by a literal `"reset"` payload)

---

### Message signing — S *(niche)*

`CMD_SIGN_START` / `CMD_SIGN_DATA` / `CMD_SIGN_FINISH` sign arbitrary data with the node's private key (for signed announcements / authenticated messages). SlopOS does not expose signing.

**What's needed:** Port the streaming-sign API; expose via Terminal command. Low priority for the handheld use case.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SIGN_START` (33) / `CMD_SIGN_DATA` (34) / `CMD_SIGN_FINISH` (35); `RESP_CODE_SIGN_START` / `RESP_CODE_SIGNATURE`; `onSignedMessageRecv()` (receive side)

---

### Keyboard backlight control — S

`NodePrefs` stores `kb_backlight` (0–255) and the keyboard MCU accepts I2C brightness commands, but there is no Settings control.

**What's needed:** A brightness slider in Settings wired to `keyboard_set_backlight()` + `prefs_set()`.

*(No MeshCore reference — T-Deck HAL.)*

---

### Message history cap control — S

`NodePrefs::msg_cap` controls per-channel retention but has no UI.

**What's needed:** A numeric input in Settings; validate against PSRAM.

*(No MeshCore reference — local.)*

---

### Node type selection — M

Companions advertise as `ADV_TYPE_CHAT`. A fixed T-Deck might advertise as repeater/room. Note: non-CHAT types change forwarding behaviour (companions don't relay by default) — a significant protocol change.

**What's needed:** An "Advanced" Settings node-type selector; for repeater mode, enable the relay path and raise advert frequency.

**MeshCore reference:**
- [`src/helpers/AdvertDataHelpers.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/AdvertDataHelpers.h) — `ADV_TYPE_CHAT` (1), `ADV_TYPE_REPEATER` (2), `ADV_TYPE_ROOM` (3), `ADV_TYPE_SENSOR` (4); `AdvertDataBuilder` takes type as first arg
- [`src/helpers/CommonCLI.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/CommonCLI.h) — `NodePrefs::advert_type`

---

## Diagnostics and Statistics

### Node stats query (CMD_GET_STATS) — S

SlopOS tracks `getNumSentFlood/Direct`, `getNumRecvFlood/Direct`, and airtime totals in the wrapper, but does not expose the full companion stats set (per-type counters, dropped packets, airtime budget). The companion `CMD_GET_STATS` returns a typed stats blob.

**What's needed:** Surface the existing counters plus dropped/airtime stats on a diagnostics panel; optionally match the companion stats-type layout.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_GET_STATS` (56) handler (second byte = stats type); `RESP_CODE_STATS`
- [`src/helpers/StatsFormatHelper.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/StatsFormatHelper.h)

---

### Terminal command documentation (help system) — S

Also in `KNOWN_ISSUES.md`. The Terminal has no `help` command — users read source to find commands.

**What's needed:** A `help` command listing commands with one-line descriptions.

*(No MeshCore reference — local UI.)*

---

## User Interface

### Zero-hop ping in Finder screen — M

Also in `KNOWN_ISSUES.md`. The mesh-layer PING/PONG and `sendPingNearby()` already exist (`slop_mesh.h` / `mesh_wrapper.h`). What's missing is the Finder UI: a "Ping Nearby" button, a 3 s collection window display, a results list sorted by RSSI, and the 30 s cooldown indicator (all the backend timers/accessors already exist: `pingIsActive`, `activePingRemaining`, `getPingResult*`).

**What's needed:** Wire the existing ping API into a Finder button + results list. Pure UI work.

*(Backend complete in `SlopMesh::sendPingNearby` / `onControlDataRecv`.)*

---

### Universal trackball back-swipe — M

Also in `KNOWN_ISSUES.md`. Left-swipe → `go_back()` works only on the Chat screen.

**What's needed:** Extract the swipe handler from `chat_screen.cpp` into `navigation.cpp`; apply to all screens; resolve conflicts with screens using left-swipe themselves.

*(No MeshCore reference — local UI.)*

---

### Graceful shutdown from UI — S

No UI shutdown — the user holds the power button, risking lost NVS writes (see `KNOWN_ISSUES.md`). SlopOS already has `saveState()` / `saveChannels()` / `shutdown()` in the wrapper.

**What's needed:** A "Shut down" Settings option (or long-press home): `saveState()`, `saveChannels()`, ~100 ms delay, then `esp_deep_sleep_start()`.

*(No MeshCore reference — T-Deck HAL.)*

---

## Security

### ACL / contact permissions — L

MeshCore defines permission levels (guest / read-only / read-write / admin). SlopOS treats all contacts identically.

**What's needed:** Add a `perm` field to `SlopContact`; promote contacts in Contact Detail; gate sensitive actions behind permission checks.

**MeshCore reference:**
- [`src/helpers/ClientACL.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/ClientACL.h) / [`ClientACL.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/ClientACL.cpp) — the ACL store; permission levels `PERM_ACL_GUEST` (0), `PERM_ACL_READ_ONLY` (1), `PERM_ACL_READ_WRITE` (2), `PERM_ACL_ADMIN` (3), `PERM_ACL_ROLE_MASK`, and `isAdmin()`

---

### Device admin password / PIN — M

No password protects Settings or Terminal — anyone with physical access can change radio params, read messages, or alter identity. The companion stores a device PIN (`CMD_SET_DEVICE_PIN`) and an admin password.

**What's needed:** An optional hashed PIN in NVS; prompt on Settings/Terminal entry, with a grace period after recent activity.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SET_DEVICE_PIN` (37) handler
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `NodePrefs::ble_pin` (the persisted device PIN)

---

## System

### OTA firmware update — L

MeshCore supports `start ota` over BLE/serial. SlopOS requires a USB cable + flashing tool.

**What's needed:** An OTA partition layout in `platformio.ini`; a download mechanism (WiFi or BLE — both present on ESP32-S3, neither initialised); a UI progress indicator. Transfer uses ESP-IDF `esp_ota_ops.h` (outside MeshCore).

**MeshCore reference:**
- [`src/helpers/CommonCLI.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/CommonCLI.cpp) — `handleCommand()` `start ota` branch (when OTA mode is triggered + pre-OTA cleanup)

---

## Infrastructure-only (documented, not planned)

> These turn the T-Deck into something other than a handheld companion. Listed for completeness; excluded from the implementation plan.

### BLE companion protocol (expose T-Deck as a radio modem) — L

MeshCore's BLE UART companion protocol (Nordic UART service, UUID `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`) lets a phone app use the radio as a modem. The T-Deck *is* the companion already, so this is a different product.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — the authoritative `CMD_*` / `RESP_CODE_*` / `PUSH_CODE_*` frame protocol
- [`src/helpers/BaseSerialInterface.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseSerialInterface.h) — frame transport (`MAX_FRAME_SIZE = 172`); [`ArduinoSerialInterface.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/ArduinoSerialInterface.cpp) — concrete impl

---

### Region management / flood scope keys — L

MeshCore v1.10+ supports sub-region routing and per-region transport keys (`CMD_SET_FLOOD_SCOPE_KEY`, `CMD_SET/GET_DEFAULT_FLOOD_SCOPE`, `CMD_SET_PATH_HASH_MODE`). Primarily a repeater feature.

**MeshCore reference:**
- [`src/helpers/RegionMap.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/RegionMap.h) / [`RegionMap.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/RegionMap.cpp) — `RegionMap`, `RegionEntry`, `REGION_DENY_FLOOD`/`REGION_DENY_DIRECT`, `MAX_REGION_ENTRIES`
- [`src/helpers/TransportKeyStore.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/TransportKeyStore.h) — per-region transport key lookup
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SET_FLOOD_SCOPE_KEY` (54), `CMD_SET_PATH_HASH_MODE` (61), `CMD_SET/GET_DEFAULT_FLOOD_SCOPE` (63/64)
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `path_hash_mode`, `default_scope_name`, `default_scope_key`

---

### Launcher compatibility — M

A niche build target for running under `bmorcelli/Launcher`. Not relevant to the standalone companion experience.

*(No MeshCore reference — local build/HAL.)*

---

## Implementation order

The phased implementation plan that used to live here has moved to **[`ROADMAP.md`](ROADMAP.md)** — it carries the build order, dependencies, step-by-step guidance, pitfalls, and per-task test plans (including the foundational `BaseChatMesh` migration). This document is now purely the *catalog* of what's missing and where to find it upstream; `ROADMAP.md` is *how and in what order* to build it.

---

*Last reviewed: 2026-05-28 against companion firmware v1.15.0 (`FIRMWARE_VER_CODE 11`, [`examples/companion_radio/`](https://github.com/meshcore-dev/MeshCore/tree/main/examples/companion_radio)). Added: Infrastructure Interaction category (login/remote-admin, status, telemetry, path discovery, reset-path, binary-request framework), GPS/Location section (location-share policy, GPS interval), contact removal, identity backup, factory reset, message signing, node-stats query. Fixed stale ACK line reference and clarified that `SlopMesh` extends `::mesh::Mesh` directly (not `BaseChatMesh`). Implementation plan extracted to [`ROADMAP.md`](ROADMAP.md). Cross-reference `KNOWN_ISSUES.md` for bugs in implemented features.*
