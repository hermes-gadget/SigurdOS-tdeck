# Feature Status (vs MeshCore)

This document catalogs the features MeshCore's protocol and ecosystem provide and tracks, for **each**, whether SigurdOS-TDeck firmware currently implements it. It is a **current-state snapshot of feature parity** — not a bug tracker and not a build plan. Every status below was checked against the actual code in this repo, which may differ from what changelogs or PR descriptions claimed. Bugs and workarounds in *implemented* features belong in [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md).

SigurdOS-TDeck is a standalone **companion-radio firmware** for the LilyGo T-Deck. It interoperates with any MeshCore node and is designed for the end-user handheld experience — not for infrastructure roles (dedicated repeaters, room servers, sensors). Features are tagged to distinguish companion-relevant from infrastructure-only items.

> **Status legend:**
> - **✅ Implemented** — present in the current firmware (verified against the code; the citing symbol/file is named). Kept here only for its upstream reference.
> - **⚠️ Partially implemented** — part of the feature works; the gap is described inline.
> - *(unmarked)* — still missing. The **What's needed** notes describe the work.
> - **❌ NOT DOING / NOT NEEDED** — explicitly declined.

## Where to find things in upstream MeshCore

Every reference below links directly into **`https://github.com/meshcore-dev/MeshCore`** (main branch) so other agents can jump straight to the source. The repo submodule (`lib/meshcore/`) is pinned to companion firmware **v1.15.0 / `FIRMWARE_VER_CODE 12`** ([`examples/companion_radio/MyMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.h)). Line numbers drift between versions — references cite **symbol names**, so grep the linked file if a line has moved. If a symbol can't be found upstream, check `lib/meshcore/` directly (the pinned commit is authoritative for what SigurdOS actually builds against).

The single most useful reference is the companion radio command dispatcher:
[`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `handleCmdFrame()` defines all **58 `CMD_*` request codes** (numbered up to 65, with gaps), **28 `RESP_CODE_*` reply codes**, and **17 `PUSH_CODE_*` async push codes**. Almost every protocol feature below has a worked example in this one file.

### ⚠️ Architectural note — read before estimating effort

The companion radio (`MyMesh`) extends **`BaseChatMesh`** ([`src/helpers/BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h)), which provides ready-made high-level helpers: `sendLogin()`, `sendCommandData()`, `sendRequest()`, `resetPathTo()`, `processAck()` with an `expected_ack_table`, an offline message queue, and contact-by-pubkey lookup.

**SigurdOS's `SigurdMeshV2` now also extends `BaseChatMesh`** (`src/mesh/sigurd_mesh_v2.h`). The migration off the old raw `::mesh::Mesh` subclass is **complete**. It therefore *inherits* those helpers, which is why most of the Infrastructure / Protocol features below are now implemented as thin **wrapper + UI** work rather than bespoke protocol code. Contacts use `BaseChatMesh`'s `::ContactInfo` (with `out_path` / `out_path_len`); the UI is insulated behind `sigurdos::mesh::ContactInfo` in `mesh_wrapper.h`.

> **Historical note:** earlier revisions of this document described a `SlopMesh` class extending `::mesh::Mesh` *directly* (`src/mesh/slop_mesh.h`) and warned that the `BaseChatMesh` helpers were "absent". That predates the migration and no longer applies — the class is now `SigurdMeshV2` in `src/mesh/sigurd_mesh_v2.h`.

---

## How to read effort levels

Effort levels on the still-missing items below estimate the work remaining:

- **S** — small: isolated change, few files, testable in native tests
- **M** — medium: touches mesh layer + UI, needs device testing
- **L** — large: architectural change, multiple screens or protocol work

---

## Infrastructure Interaction (companion-relevant)

> These are **companion** features even though they involve infrastructure nodes. A handheld companion routinely logs into repeaters/room servers, pulls their status/telemetry, and discovers paths. Now that `SigurdMeshV2` extends `BaseChatMesh` (see architectural note above), these were straightforward to wire up — most are ✅ below.

### Repeater / room-server login + remote administration — L

> **✅ Implemented** — dedicated repeater/room detail screen with a login flow (password field, saved-password store in `prefs.cpp`), an admin command console, and live response polling. `sendLogin()`/`sendLogout()`/`sendCommand()`/`isLoggedIn()`/`getLoginPermission()` in `mesh_wrapper.h` (PR #259). Kept here for the upstream reference.

A companion logs into a repeater or room server with a password, receiving a permission level (guest / admin), then issues CLI admin commands over the mesh (`set freq`, `reboot`, `set name`, etc.) as encrypted COMMAND-type datagrams. This is how the official app administers remote infrastructure.

**MeshCore reference:**
- [`src/helpers/BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h) — `sendLogin(recipient, password, est_timeout)`, `sendCommandData(recipient, timestamp, attempt, text, est_timeout)`, `startConnection()` / `stopConnection()` (keep-alive session)
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_LOGIN` (26) and `CMD_LOGOUT` (29) handlers; `onContactResponse()` parses `RESP_SERVER_LOGIN_OK`, extracts keep-alive + permission/ACL bytes, pushes `PUSH_CODE_LOGIN_SUCCESS` / `PUSH_CODE_LOGIN_FAIL`; `onCommandDataRecv()` / `onSignedMessageRecv()` show how admin command replies arrive
- [`examples/simple_repeater/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_repeater/MyMesh.cpp) — the server side: how a repeater authenticates a login and executes admin commands

---

### Status request (repeater / room server health) — M

> **✅ Implemented** — `requestStatus()` / `hasStatusResponse()` / `getStatusResult()` in `mesh_wrapper.h` (parses the `RepeaterStats` blob into a `NodeStatus` struct) with a node-status detail panel (PR #253). Kept here for the upstream reference.

`CMD_SEND_STATUS_REQ` asks an infrastructure node for a status blob (uptime, battery, airtime, TX/RX queue depth, free heap). The companion displays this so a user can check a remote repeater without physical access.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_STATUS_REQ` (27) handler; `onContactResponse()` builds `PUSH_CODE_STATUS_RESPONSE`
- [`examples/simple_repeater/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_repeater/MyMesh.cpp) — `REQ_TYPE_GET_STATUS` handler builds the status payload (the canonical field layout)
- [`src/helpers/StatsFormatHelper.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/StatsFormatHelper.h) — status/stats field formatting

---

### Telemetry queries (remote + self, CayenneLPP) — M

> **⚠️ Partially implemented** — the *remote query* is done: `requestTelemetry()` (`mesh_wrapper.h`) sends `REQ_TYPE_GET_TELEMETRY_DATA` and decodes the CayenneLPP reply (voltage, temp, humidity, lat-lon). The *answer side* is **missing**: `SigurdMeshV2::onContactRequest()` (`sigurd_mesh_v2.h`) is a skeleton that returns `0`, so the T-Deck never reports its own battery/sensors over the mesh, and there is no per-category response policy.

MeshCore carries sensor telemetry in CayenneLPP format. `CMD_SEND_TELEMETRY_REQ` queries a remote node's telemetry (battery voltage, environment sensors, GPS); a length-4 self-request returns the device's own telemetry.

**What's needed:** Implement `onContactRequest()` to answer `REQ_TYPE_GET_TELEMETRY_DATA` with a `CayenneLPP` reply (`addVoltage(TELEM_CHANNEL_SELF, …)`, optionally location/environment). Add the `telemetry_mode_base`/`telemetry_mode_loc`/`telemetry_mode_env` policy fields (deny / allow-by-flags / allow-all) to gate what is shared.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_TELEMETRY_REQ` (39): remote variant calls `sendRequest(*recipient, REQ_TYPE_GET_TELEMETRY_DATA, …)`; `len==4` self variant fills a `CayenneLPP telemetry` with `addVoltage(TELEM_CHANNEL_SELF, …)` + `sensors.querySensors()` and pushes `PUSH_CODE_TELEMETRY_RESPONSE`; `onContactRequest()` answers `REQ_TYPE_GET_TELEMETRY_DATA`
- `REQ_TYPE_GET_TELEMETRY_DATA` (0x03) is defined at the top of [`examples/companion_radio/MyMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.h)
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `telemetry_mode_base` / `telemetry_mode_loc` / `telemetry_mode_env`; `TELEM_MODE_DENY` (0) / `TELEM_MODE_ALLOW_FLAGS` (1) / `TELEM_MODE_ALLOW_ALL` (2). [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SET_OTHER_PARAMS` (38) sets them; `onContactRequest()` honours them
- [`src/helpers/SensorManager.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/SensorManager.h) and [`src/helpers/sensors/`](https://github.com/meshcore-dev/MeshCore/tree/main/src/helpers/sensors) — the sensor/telemetry framework and CayenneLPP usage

---

### Path discovery request — M

> **✅ Implemented** — `discoverPath()` / `hasPathTo()` / `getContactPathLen()` in `mesh_wrapper.h` with a "Discover Path" action on contact detail. Distinct from Trace. Kept here for the upstream reference.

Distinct from Trace (which probes an *already-known* path). `CMD_SEND_PATH_DISCOVERY_REQ` actively asks the mesh to discover a route to a contact whose path is unknown, returning the path in a `PAYLOAD_TYPE_RESPONSE`.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_PATH_DISCOVERY_REQ` (52) handler; `onContactPathRecv()` matches `pending_discovery` against `PAYLOAD_TYPE_RESPONSE` extra data and pushes `PUSH_CODE_PATH_DISCOVERY_RESPONSE`

---

### Reset path to a contact — S

> **✅ Implemented** — "Reset Path" button on the contact detail screen → `resetPathTo()` (`mesh_wrapper.h`) clears `out_path_len` so the next message floods and re-learns. Kept here for the upstream reference.

When a learned path goes stale (a repeater moves or dies), messages keep failing on the dead route until the path is cleared. `CMD_RESET_PATH` wipes the stored out-path so the next message floods and re-learns.

**MeshCore reference:**
- [`src/helpers/BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h) — `resetPathTo(ContactInfo& recipient)`
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_RESET_PATH` (13) handler

---

### Generic binary request framework (REQ/RESPONSE) — M

> **✅ Implemented** — `sendRequest()` / `sendRequestWithData()` + a tag-matched `getResponse()` dispatch in `mesh_wrapper.h`. Status, telemetry, path-discovery, and room-fetch all build on it (PR #251). Kept here for the upstream reference.

`CMD_SEND_BINARY_REQ` is the generalised request primitive that status/telemetry are now built on: send arbitrary `req_data` to a contact, match the `RESPONSE` by a 4-byte tag.

**MeshCore reference:**
- [`src/helpers/BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h) — both `sendRequest()` overloads
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_BINARY_REQ` (50) handler; `onContactResponse()` matches `pending_req` by tag, pushes `PUSH_CODE_BINARY_RESPONSE`

---

## Protocol / Packet Types

### Message delivery status (ACK display) — M

> **✅ Implemented** — `PendingAck` ring buffer in `SigurdMeshV2` (`sigurd_mesh_v2.h`); `processAck()` matches the 4-byte SHA-256 ACK against pending outgoing DMs; `registerAckedMessage()`/`isMessageAcked()` bridge to the UI, which shows a ✓ on self-sent chat bubbles (PR #232). Kept here for the upstream reference.

The chat screen tracks sent/pending/delivered state and shows a status tick in chat bubbles.

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

> **✅ Implemented** — `sendGroupDataToChannel(channel, data_type, data, len)` plus a received-datagram type dispatch and polling API (`getGroupDataRecvEntry()`) in `mesh_wrapper.h`; a `groupdata` Terminal command drives it (PR #265). Kept here for the upstream reference.

The companion radio exposes binary group datagrams via `CMD_SEND_CHANNEL_DATA` with a 16-bit type namespace, enabling shared-state sync, sensor broadcasts, etc.

**MeshCore reference:**
- [`src/Packet.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — `#define PAYLOAD_TYPE_GRP_DATA 0x06`
- [`src/helpers/BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h) — `sendGroupData(channel, path, path_len, data_type, data, data_len)`
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_CHANNEL_DATA` (62); `onChannelDataRecv()` shows the `data_type` dispatch and `RESP_CODE_CHANNEL_DATA_RECV`
- [`src/helpers/TxtDataHelpers.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/TxtDataHelpers.h) — `DATA_TYPE_RESERVED` (0x0000) / `DATA_TYPE_DEV` (0xFFFF) reserved type-code boundaries

---

### Anonymous requests (PAYLOAD_TYPE_ANON_REQ 0x07) — M

> **✅ Implemented** — `sendAnonMessage(pubkey_hex, text)` in `mesh_wrapper.h` (exposes `BaseChatMesh::sendAnonReq()`), with an `anon` Terminal command (PR #260). Kept here for the upstream reference.

Anonymous requests let a node contact another without a prior advert exchange — the sender's pubkey is embedded in the packet. Used for first-contact login to room servers. (Receiving was already handled.)

**MeshCore reference:**
- [`src/Packet.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — `#define PAYLOAD_TYPE_ANON_REQ 0x07`
- [`src/Mesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Mesh.h) — `Mesh::createAnonDatagram()` (send), `onAnonDataRecv()` (receive)
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_ANON_REQ` (57) handler
- [`examples/simple_room_server/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_room_server/MyMesh.cpp) — room server uses anon requests for first-contact login

---

### Direct request/response (PAYLOAD_TYPE_REQ 0x00 / RESPONSE 0x01) — M

> **✅ Implemented** — covered by the generic binary-request framework above (`sendRequest()` send path + tag-matched dispatch). Room-server fetch and path discovery build on it. Kept here for the upstream reference.

Direct-encrypted REQ (0x00) / RESPONSE (0x01) share the TEXT/PATH wire format (dest hash + encrypted data).

**Core Protocol Spec reference:**
- `§2.9` — Direct-encrypted REQ (0x00) / RESPONSE (0x01) share the TEXT/PATH wire format (dest hash + encrypted data)
- [`examples/simple_room_server/MyMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_room_server/MyMesh.h) — `REQ_TYPE_GET_STATUS`, `REQ_TYPE_GET_TELEMETRY_DATA`, `REQ_TYPE_KEEP_ALIVE`

---

### Control packets (PAYLOAD_TYPE_CONTROL 0x0B) — L

> **⚠️ Partially implemented** — `SigurdMeshV2::onControlDataRecv` (`sigurd_mesh_v2.h`) implements a SigurdOS-specific PING/PONG over zero-hop control packets (the Finder "Ping Nearby" feature). No *other* control sub-types (capability query, neighbour hello, sub-mesh management) are handled.

The library only dispatches control packets for direct-routed, previously-unseen, zero-hop packets where `payload[0] & 0x80` is set; subtypes are application-defined.

**What's needed:** Design which control sub-types SigurdOS should answer. Note the companion radio exposes this generically via `CMD_SEND_CONTROL_DATA` / `PUSH_CODE_CONTROL_DATA`.

**MeshCore reference:**
- [`src/Packet.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — `#define PAYLOAD_TYPE_CONTROL 0x0B`
- [`src/Mesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Mesh.h) — `Mesh::createControlData()`, `onControlDataRecv()`
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SEND_CONTROL_DATA` (55) handler

---

### Raw custom payloads (PAYLOAD_TYPE_RAW_CUSTOM 0x0F) — ❌ NOT DOING

- **Reason:** User declined — not implementing.
- **Reference (for posterity):** `onRawDataRecv` is a stub. (Note: SigurdOS uses `createRawData()` internally for PING/PONG, but there is no general app dispatch.) [`src/Packet.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — `#define PAYLOAD_TYPE_RAW_CUSTOM 0x0F`

---

## Radio Configuration

### RX gain boost toggle — S

> **✅ Implemented** — `rx_boosted_gain` in `NodePrefs` + a toggle in Radio Setup, applied via `applyRadioParams()` at radio init. Kept here for the upstream reference.

The SX1262 supports a boosted RX sensitivity mode.

**MeshCore reference:**
- [`src/helpers/radiolib/RadioLibWrappers.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/radiolib/RadioLibWrappers.h) — `RadioLibWrapper::setRxBoostedGainMode(bool)`
- [`src/helpers/radiolib/CustomSX1262Wrapper.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/radiolib/CustomSX1262Wrapper.h) — concrete override
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `NodePrefs::rx_boosted_gain`

---

### Temporary radio config (no reboot) — ❌ NOT NEEDED

- **Reason:** User declined — not implementing. The live-apply plumbing exists (`applyRadioParams()` / `revertRadioParams()` in the wrapper) but no auto-revert timer / "Try" UI is planned.
- **Reference (for posterity):** MeshCore's CLI supports `tempradio freq,bw,sf,cr,timeout_mins` — a trial config that auto-reverts on reboot without writing NVS.
  - [`src/helpers/CommonCLI.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/CommonCLI.cpp) — `handleCommand()` `tempradio` branch; `temp_radio_timeout` in NodePrefs
  - [`src/helpers/radiolib/RadioLibWrappers.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/radiolib/RadioLibWrappers.h) — `setParams(freq, bw, sf, cr)`

---

### Duty cycle enforcement — M

> **✅ Implemented** — `duty_cycle` in `NodePrefs` + a Settings cycle control (0/1/5/10/25/50/100); `SigurdMeshV2` overrides `getAirtimeBudgetFactor()` and the remaining hourly budget is shown on the Signal screen via `getRemainingTxBudget()`. Kept here for the upstream reference.

EU ISM 868 MHz limits airtime to 1%/hour.

**MeshCore reference:**
- [`src/Dispatcher.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Dispatcher.h) — `tx_budget_ms`, `duty_cycle_window_ms`, `getAirtimeBudgetFactor()`, `getRemainingTxBudget()`
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `airtime_factor`

---

### Auto-add contact configuration — M

> **✅ Implemented** — `autoadd_config` (per-type bitmask) + `autoadd_max_hops` in `NodePrefs`; `onAdvertRecv` gates auto-add by type; Settings exposes "Auto-add: All types" + "Add max hops" cycles (PR #228). Kept here for the upstream reference.

Per-type bitmask: `AUTO_ADD_CHAT` (0x02), `AUTO_ADD_REPEATER` (0x04), `AUTO_ADD_ROOM_SERVER` (0x08), `AUTO_ADD_SENSOR` (0x10), `AUTO_ADD_OVERWRITE_OLDEST` (0x01).

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.h) — `isAutoAddEnabled()`, `shouldAutoAddContactType()`, `shouldOverwriteWhenFull()`, `getAutoAddMaxHops()`, `AUTO_ADD_*`
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SET_AUTOADD_CONFIG` (58) / `CMD_GET_AUTOADD_CONFIG` (59); `onDiscoveredContact()` filtering; `onContactOverwrite()`
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `autoadd_config`, `autoadd_max_hops`, `manual_add_contacts`

---

### Custom variables (key-value store) — S

> **✅ Implemented** — SPIFFS-backed `key=value` store with `setvar` / `getvar` / `delvar` / `listvars` Terminal commands (PR #229). Kept here for the upstream reference.

`CMD_GET_CUSTOM_VARS` / `CMD_SET_CUSTOM_VAR` provide a named `name:value` key-value store for vendor-specific config (GPS tuning, sensor calibration).

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_GET_CUSTOM_VARS` (40) formats comma-separated `name:value`; `CMD_SET_CUSTOM_VAR` (41) parses `name:value` → `sensors.setSettingValue()`; pushes `RESP_CODE_CUSTOM_VARS`
- `Core Protocol Spec Part 2 §2.5.9` — wire format

---

### Multi-ACK reliability toggle — S

The companion can send **extra redundant ACK transmissions** for direct messages, improving delivery on lossy links at the cost of a little airtime. The count comes from `NodePrefs::multi_acks` (0/1) via the `getExtraAckTransmitCount()` hook. SigurdOS does not override this hook, so it always sends the minimum, and there is no setting (no `multi_acks` field in `NodePrefs`).

**What's needed:** Add `multi_acks` (0/1) to `NodePrefs`; override `getExtraAckTransmitCount()` in `SigurdMeshV2` to return it; add a Settings toggle.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `getExtraAckTransmitCount()` returns `_prefs.multi_acks`; `CMD_SET_OTHER_PARAMS` (38) sets it
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `multi_acks`

---

## GPS and Location

### Advert location-share policy (privacy) — S

> **✅ Implemented** — `share_location` in `NodePrefs` (default on) + a Settings toggle gating the lat/lon advert overload. Kept here for the upstream reference.

The companion `NodePrefs::advert_loc_policy` lets the user decide whether their position is broadcast in adverts.

**MeshCore reference:**
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `ADVERT_LOC_NONE` / `ADVERT_LOC_SHARE`, `NodePrefs::advert_loc_policy`
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SET_ADVERT_LATLON` (14) handler

---

### GPS enable / read-interval control — S

> **✅ Implemented** — `gps_enabled` (bool) + `gps_interval` (uint16_t seconds) in `NodePrefs`; `sigurdos_gps_init()`/`sigurdos_gps_loop()` are gated on them; Settings exposes a GPS ON/OFF toggle + interval cycle (0/1/5/10/30/60 s) (PR #227). Kept here for the upstream reference.

The companion stores `gps_enabled` and `gps_interval` (seconds) — affecting battery life.

**MeshCore reference:**
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `gps_enabled`, `gps_interval`
- [`examples/companion_radio/MyMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.h) — `applyGpsPrefs()` (sets `gps` / `gps_interval` setting values)

---

### Periodic auto-advert — S

> **✅ Implemented** — `advert_interval` (half-minutes, 0 = off) in `NodePrefs` + Settings cycle + a re-advert timer in `mesh::loop()`. Kept here for the upstream reference.

Companion nodes can broadcast periodic adverts so they stay discoverable. Optional, user-toggled.

**MeshCore reference:**
- [`src/helpers/CommonCLI.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/CommonCLI.h) — `NodePrefs::advert_interval` (minutes/2), `flood_advert_interval` (hours); `CommonCLICallbacks::updateAdvertTimer()` / `updateFloodAdvertTimer()`

---

### Contact locations on Map screen — M

The Map screen shows offline tiles and own GPS position but not other nodes. Contacts already carry `has_location` / `latitude` / `longitude` (advert parsing is done, surfaced via `sigurdos::mesh::ContactInfo`) — the data is available, only the rendering is missing.

**What's needed:** Render labelled contact markers on the map canvas; update on new adverts; tap a marker → contact detail.

*(No additional MeshCore reference — the coordinates are already parsed.)*

---

## Contacts and Discovery

### Contact removal — S

> **✅ Implemented** — "Remove contact" action on the contact detail screen (confirmation dialog) → `removeContact()` (`mesh_wrapper.h`). Kept here for the upstream reference.

The list previously had LRU eviction only when full; the user can now manually remove a stale or unwanted contact.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_REMOVE_CONTACT` (15) handler; `PUSH_CODE_CONTACT_DELETED` (0x8F)

---

### Identity backup — export / import private key — M

The node's identity *is* its private key — losing it means losing your address on the mesh, and there is no recovery. The companion exposes `CMD_EXPORT_PRIVATE_KEY` / `CMD_IMPORT_PRIVATE_KEY` so a user can back up and restore identity. SigurdOS has no export, import, or backup of its identity.

**What's needed:** A "Back up identity" action (export hex/QR) and an "Import identity" path (re-key the node). Handle the re-key carefully — contacts' shared secrets must be recomputed.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_EXPORT_PRIVATE_KEY` (23) → `RESP_CODE_PRIVATE_KEY`; `CMD_IMPORT_PRIVATE_KEY` (24)
- [`src/helpers/IdentityStore.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/IdentityStore.h) — identity load/save to filesystem

---

### QR code generation for contact/channel sharing — L

MeshCore uses a deep-link URI scheme for sharing (`meshcore://contact/add?name=…&public_key=<64hex>&type=<1-4>` and `meshcore://channel/add?name=…&secret=<hex32>`). The 320×240 display can show a QR; a tiny MIT-licensed QR encoder (~2 KB) fits.

**What's needed:** Add a QR library (check GPL-3.0 compatibility — a rejection trigger if not). "Share" buttons on Contact Detail and Channels. Render to an LVGL canvas.

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

> **✅ Implemented** — `sendRoomMsgFetchRequest()` + `getRoomMsgFetchEntry()` (`mesh_wrapper.h`): log into a room server, fetch stored posts, merge into the message store. Built on the binary-request framework (PR #263). Kept here for the upstream reference.

A companion out of range of a room server later wants to sync missed messages. Detects `ADV_TYPE_ROOM` contacts.

**MeshCore reference:**
- [`examples/simple_room_server/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_room_server/MyMesh.cpp) — `PostInfo`, cyclic post queue (`MAX_UNSYNCED_POSTS` = 32), `pushPostToClient()` (how stored messages are encoded back to a client)

---

### Channel removal — S

> **✅ Implemented** — delete/× button on each channel row → `removeChannel(idx)` (`mesh_wrapper.h`) shifts the channel array and persists via `saveChannels()`. Kept here for the upstream reference.

Previously the 8-slot list filled with no eviction.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SET_CHANNEL` (32) with an empty/zero channel clears a slot (the protocol-level removal idiom)

---

### Message timestamps in chat bubbles — S

> **✅ Implemented** — `format_time()` renders `HH:MM` in the bubble header (sender + timestamp); messages >24 h old show the date. Kept here for the upstream reference.

`MeshMessage::timestamp` holds the packet timestamp — this was UI-only work.

*(No MeshCore reference — data already present in `MeshMessage`.)*

---

### Message search — M

> **✅ Implemented** — an 'S' button in the chat top bar toggles an inline search bar; case-insensitive substring filter over text + sender; trackball Up/Down cycles matches with auto-scroll + highlight (PR #234). Kept here for the upstream reference.

*(No MeshCore reference — local UI work.)*

---

### Per-contact RSSI/SNR history graph — L

> **✅ Implemented** — a 64-entry per-contact circular buffer in `SigurdMeshV2` (`sigurd_mesh_v2.h`) feeds an `lv_chart` line sparkline on the Signal screen (PR #236). Kept here for the upstream reference.

*(No MeshCore reference — local UI work.)*

---

## Settings and Configuration

### Factory reset — S

> **✅ Implemented** — Settings → System "Factory reset" (double-confirm) → `mesh::factoryReset()` clears NVS prefs + contacts + channels, regenerates identity, delays for flash, then reboots (PR #275). Kept here for the upstream reference.

`CMD_FACTORY_RESET` wipes prefs, contacts, channels, and identity to a clean state.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_FACTORY_RESET` (51) handler (guarded by a literal `"reset"` payload)

---

### Message signing — S *(niche)*

`CMD_SIGN_START` / `CMD_SIGN_DATA` / `CMD_SIGN_FINISH` sign arbitrary data with the node's private key (for signed announcements / authenticated messages). SigurdOS does not expose signing.

**What's needed:** Port the streaming-sign API; expose via Terminal command. Low priority for the handheld use case.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_SIGN_START` (33) / `CMD_SIGN_DATA` (34) / `CMD_SIGN_FINISH` (35); `RESP_CODE_SIGN_START` / `RESP_CODE_SIGNATURE`; `onSignedMessageRecv()` (receive side)

---

### Keyboard backlight control — S

> **✅ Implemented** — `kbd_backlight` (0–255) in `NodePrefs` + a brightness dialog in Settings wired to the keyboard MCU and `prefs_set()`. Kept here for the upstream reference.

*(No MeshCore reference — T-Deck HAL.)*

---

### Message history cap control — S

> **✅ Implemented** — `chat_msg_cap` in `NodePrefs` + a "Chat Message Cap" dialog in Settings, consumed by `chat_screen.cpp`. Kept here for the upstream reference.

*(No MeshCore reference — local.)*

---

### Node type selection — M

The node's `advert_type` field exists in `NodePrefs` (default `ADV_TYPE_CHAT`) and is read at boot, but there is **no Settings UI** to change it. A fixed T-Deck might advertise as repeater/room. Note: non-CHAT types change forwarding behaviour (companions don't relay by default) — a significant protocol change. (The companion's *opportunistic relay* flag is tracked separately as "Client-repeat mode" below.)

**What's needed:** An "Advanced" Settings node-type selector writing `NodePrefs::advert_type`; for repeater mode, enable the relay path and raise advert frequency.

**MeshCore reference:**
- [`src/helpers/AdvertDataHelpers.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/AdvertDataHelpers.h) — `ADV_TYPE_CHAT` (1), `ADV_TYPE_REPEATER` (2), `ADV_TYPE_ROOM` (3), `ADV_TYPE_SENSOR` (4); `AdvertDataBuilder` takes type as first arg
- [`src/helpers/CommonCLI.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/CommonCLI.h) — `NodePrefs::advert_type`

---

### Client-repeat mode (companion also relays) — M

A companion can optionally relay/forward packets while staying a chat node — the `client_repeat` flag. This is the *companion* version of repeating (opportunistic relay), distinct from full repeater node-type (see "Node type selection" above). SigurdOS has no packet-forwarding path, no `client_repeat` pref, and no toggle.

**What's needed:** Add `client_repeat` to `NodePrefs`; gate forwarding on it in `SigurdMeshV2`; an "Advanced" Settings toggle. Relaying raises airtime — respect the duty-cycle budget.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — forwarding gated on `_prefs.client_repeat != 0`; `CMD_SET_TUNING_PARAMS` carries the repeat flag
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `client_repeat`

---

### Message-arrival notification buzzer + quiet toggle — M

The companion beeps the buzzer on message arrival and exposes a `buzzer_quiet` mute. SigurdOS defines `PIN_BUZZER` (GPIO 46) in `tdeck_pins.h` but **never drives it** — there is no buzzer HAL, no audible notification, and no `buzzer_quiet` pref.

**What's needed:** A small buzzer HAL (active-low on GPIO 46) + mock + test; beep on incoming DM/channel message; a `buzzer_quiet` pref and a Settings "Notification sound ON/OFF" toggle.

**MeshCore reference:**
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `buzzer_quiet`; companion UI calls `buzzer.quiet(_node_prefs->buzzer_quiet)`

---

## Diagnostics and Statistics

### Node stats query (CMD_GET_STATS) — S

> **✅ Implemented** — Node Stats diagnostics panel (sent/recv flood+direct counters, airtime totals, dups/drops), reachable via the `nodestats` nav entry (PR #277). Kept here for the upstream reference.

The companion `CMD_GET_STATS` returns a typed stats blob.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_GET_STATS` (56) handler (second byte = stats type); `RESP_CODE_STATS`
- [`src/helpers/StatsFormatHelper.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/StatsFormatHelper.h)

---

### Terminal command documentation (help system) — S

> **✅ Implemented** — the Terminal has a `help` command listing available commands (`help status advert ping anon fetchmsgs groupdata emoji-list getvar setvar delvar listvars`); see the dispatcher in `src/ui/screens.cpp`. Kept here for the upstream reference.

*(No MeshCore reference — local UI.)*

---

### Advert path query (diagnostic) — S *(niche)*

`CMD_GET_ADVERT_PATH` reports the network path an advert took to reach this node (a per-pubkey `advert_paths[]` table). A useful diagnostic for understanding routing; SigurdOS keeps no advert-path table and exposes nothing.

**What's needed:** Track recent advert paths keyed by pubkey prefix; surface the path for a contact on Contact Detail or the Signal/Trace screen.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_GET_ADVERT_PATH` (42) / `RESP_CODE_ADVERT_PATH`; the `advert_paths[]` table and `AdvertPath` struct

---

### Storage usage display — S *(niche)*

`CMD_GET_BATT_AND_STORAGE` returns battery millivolts **plus filesystem storage used/total (KB)**. SigurdOS shows battery but never surfaces flash/SD usage — `hal/sdcard.cpp` already computes SD capacity/free, so the data exists; only the display is missing.

**What's needed:** Add used/total (SPIFFS + SD) to the Node Stats panel or a storage line on Settings → System.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_GET_BATT_AND_STORAGE` (20) / `RESP_CODE_BATT_AND_STORAGE` (battery mV + used/total KB via `_store->getStorageUsedKb()` / `getStorageTotalKb()`)

---

## User Interface

### Zero-hop ping in Finder screen — M

> **✅ Implemented** — "Ping Nearby" button in the Finder screen with a 3 s collection window, results list sorted by RSSI, and a 30 s cooldown indicator. Backend (`sendPingNearby`, `pingIsActive`, `activePingRemaining`, `getPingResult*`) wired through `mesh_wrapper.h`. Kept here for the upstream reference.

*(Backend in `SigurdMeshV2::sendPingNearby` / `onControlDataRecv`.)*

---

### Universal trackball back-swipe — M

> **✅ Implemented** — `handle_back_swipe()` (two-swipe-commit) in `navigation.cpp`, called from the global `handle_trackball_event()` in `ui.cpp` for all non-Home/non-Chat screens (Chat keeps its own left-swipe for the channel list). Kept here for the upstream reference.

*(No MeshCore reference — local UI.)*

---

### Graceful shutdown from UI — S

> **✅ Implemented** — a "Shut down" button on Settings (confirmation dialog) → `saveState()` + `saveChannels()`, delay, then deep sleep. Kept here for the upstream reference.

*(No MeshCore reference — T-Deck HAL.)*

---

## Security

### ACL / contact permissions — L

MeshCore defines permission levels (guest / read-only / read-write / admin). SigurdOS treats all contacts identically.

**What's needed:** Add a `perm` field to the contact model; promote contacts in Contact Detail; gate sensitive actions behind permission checks.

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

MeshCore supports `start ota` over BLE/serial. SigurdOS requires a USB cable + flashing tool.

**What's needed:** An OTA partition layout in `platformio.ini`; a download mechanism (WiFi or BLE — both present on ESP32-S3, neither initialised); a UI progress indicator. Transfer uses ESP-IDF `esp_ota_ops.h` (outside MeshCore). The biggest single item.

**MeshCore reference:**
- [`src/helpers/CommonCLI.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/CommonCLI.cpp) — `handleCommand()` `start ota` branch (when OTA mode is triggered + pre-OTA cleanup)

---

### Reboot action — S *(low value)*

> **Largely covered.** SigurdOS already reboots via "Save & Reboot" in Radio Setup and after a Factory Reset, and `tdeck_board.h` has `reboot()`. A dedicated standalone "Reboot" button is the only gap.

`CMD_REBOOT` simply restarts the device.

**What's needed:** Optionally a dedicated "Reboot" button on Settings → System next to Factory Reset (flush pending saves, ~100 ms delay, `esp_restart()`).

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_REBOOT` (19, guarded by a literal `"reboot"` payload)

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

A niche build target for running under `bmorcelli/Launcher`. Not relevant to the standalone companion experience. (See `KNOWN_ISSUES.md`.)

*(No MeshCore reference — local build/HAL.)*

---

*Last reviewed: 2026-06-02 against companion firmware v1.15.0 (`FIRMWARE_VER_CODE 12`, [`examples/companion_radio/`](https://github.com/meshcore-dev/MeshCore/tree/main/examples/companion_radio)) and against the current code on `dev`. Status of every entry was verified in-tree, not from changelogs. Currently ✅ implemented: repeater login, status request, path discovery, reset-path, binary-request framework, ACK display, group data, anonymous send, direct REQ/RESPONSE, RX gain, duty cycle, auto-add config, custom vars, location-share policy, GPS enable/interval, periodic auto-advert, contact removal, room fetch, channel removal, message timestamps, message search, RSSI/SNR graph, factory reset, keyboard backlight, message-cap, node stats, terminal help, zero-hop ping, universal back-swipe, graceful shutdown. ⚠️ partial: telemetry (remote-query only; answer side missing), control packets (PING/PONG only). Still missing: telemetry answer-side, contact-on-map, identity backup, QR gen/import, message signing, node-type selector UI, multi-ACK toggle, client-repeat, buzzer notify, advert-path query, storage display, ACL, device PIN, OTA, dedicated reboot. ❌ declined: multipart, raw custom payloads, temporary radio config. Bugs in implemented features are tracked in `KNOWN_ISSUES.md`.*
