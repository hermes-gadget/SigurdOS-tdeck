# Feature Status (vs MeshCore)

This document catalogs MeshCore protocol features that SigurdOS-TDeck does **not** implement. Implemented features are tracked in the Git history and ROADMAP.md — this file only lists what's still missing or explicitly declined.

SigurdOS-TDeck is a standalone **companion-radio firmware** for the LilyGo T-Deck. It interoperates with any MeshCore node and is designed for the end-user handheld experience — not for infrastructure roles (dedicated repeaters, room servers, sensors).

> **Status legend:**
> - ❌ **NOT DOING / NOT NEEDED** — explicitly declined.
> - *(unmarked)* — still missing.

## Where to find things in upstream MeshCore

Every reference below links directly into **`https://github.com/meshcore-dev/MeshCore`** (main branch) so other agents can jump straight to the source. The repo submodule (`lib/meshcore/`) is pinned to companion firmware **v1.15.0 / `FIRMWARE_VER_CODE 12`** ([`examples/companion_radio/MyMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.h)). Line numbers drift between versions — references cite **symbol names**, so grep the linked file if a line has moved. If a symbol can't be found upstream, check `lib/meshcore/` directly (the pinned commit is authoritative for what SigurdOS actually builds against).

The single most useful reference is the companion radio command dispatcher:
[`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `handleCmdFrame()` defines all **58 `CMD_*` request codes** (numbered up to 65, with gaps), **28 `RESP_CODE_*` reply codes**, and **17 `PUSH_CODE_*` async push codes**. Almost every protocol feature below has a worked example in this one file.

### ⚠️ Architectural note — read before estimating effort

The companion radio (`MyMesh`) extends **`BaseChatMesh`** ([`src/helpers/BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h)), which provides ready-made high-level helpers: `sendLogin()`, `sendCommandData()`, `sendRequest()`, `resetPathTo()`, `processAck()` with an `expected_ack_table`, an offline message queue, and contact-by-pubkey lookup.

**SigurdOS's `SigurdMeshV2` now also extends `BaseChatMesh`** (`src/mesh/sigurd_mesh_v2.h`). The migration off the old raw `::mesh::Mesh` subclass is **complete**. It therefore *inherits* those helpers, which is why most protocol features are thin **wrapper + UI** work rather than bespoke protocol code. Contacts use `BaseChatMesh`'s `::ContactInfo` (with `out_path` / `out_path_len`); the UI is insulated behind `sigurdos::mesh::ContactInfo` in `mesh_wrapper.h`.

---

## How to read effort levels

Effort levels on the still-missing items below estimate the work remaining:

- **S** — small: isolated change, few files, testable in native tests
- **M** — medium: touches mesh layer + UI, needs device testing
- **L** — large: architectural change, multiple screens or protocol work

---

## Protocol / Packet Types

### Multipart messages (PAYLOAD_TYPE_MULTIPART 0x0A) — ❌ NOT DOING

- **Reason:** User declined — not implementing. 150-byte send cap remains.
- **Reference (for posterity):** MeshCore defines a multipart packet type for segmenting large payloads across LoRa frames. [`src/Packet.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — `#define PAYLOAD_TYPE_MULTIPART 0x0A`

---

### Raw custom payloads (PAYLOAD_TYPE_RAW_CUSTOM 0x0F) — ❌ NOT DOING

- **Reason:** User declined — not implementing.
- **Reference (for posterity):** `onRawDataRecv` is a stub. (Note: SigurdOS uses `createRawData()` internally for PING/PONG, but there is no general app dispatch.) [`src/Packet.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — `#define PAYLOAD_TYPE_RAW_CUSTOM 0x0F`

---

## Radio Configuration

### Temporary radio config (no reboot) — ❌ NOT NEEDED

- **Reason:** User declined — not implementing. The live-apply plumbing exists (`applyRadioParams()` / `revertRadioParams()` in the wrapper) but no auto-revert timer / "Try" UI is planned.
- **Reference (for posterity):** MeshCore's CLI supports `tempradio freq,bw,sf,cr,timeout_mins` — a trial config that auto-reverts on reboot without writing NVS.
  - [`src/helpers/CommonCLI.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/CommonCLI.cpp) — `handleCommand()` `tempradio` branch; `temp_radio_timeout` in NodePrefs
  - [`src/helpers/radiolib/RadioLibWrappers.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/radiolib/RadioLibWrappers.h) — `setParams(freq, bw, sf, cr)`

---

## Regions — Companion Flood Scope (PLANNED) — M

**Goal:** let the user pick a *region* (a named flood scope) so that outgoing flood traffic is stamped with a MeshCore **transport code**. Region-aware repeaters then only re-flood packets for the regions they serve, keeping traffic contained. This is the **companion half** of MeshCore regions — send-side scope stamping plus a small management UI. It deliberately does **not** implement the repeater half (the `RegionMap` deny-flood gating that decides what to forward); a handheld does not relay floods.

### What a "region" actually is on the wire

A region is just a **16-byte transport key**. Two flavours, distinguished by the first char of the name:

| Prefix | Type | Key derivation | Shared how |
|--------|------|----------------|------------|
| `#name` | **public hashtag** | `key = SHA256("#" + name)[0..15]` | anyone who knows the name |
| `$name` | **private** | a user-supplied 16-byte secret (key, not name) | out-of-band (base64/hex) |
| `*` | **wildcard / unscoped** | no key — plain flood, no codes | the default global mesh |

When a packet is flooded *with* a region, the sender computes a 2-byte **transport code** and writes it into `packet->transport_codes[0]`, and sets the route type to `ROUTE_TYPE_TRANSPORT_FLOOD` (0x00) instead of plain `ROUTE_TYPE_FLOOD` (0x01).

- **Code formula** (must match upstream byte-for-byte or repeaters won't recognise it):
  `code = HMAC_SHA256(key, payload_type_byte ‖ payload[0..payload_len])` truncated to the first 2 bytes; `0x0000`→`0x0001` and `0xFFFF`→`0xFFFE` are reserved. Implemented in `TransportKey::calcTransportCode()` ([`src/helpers/TransportKeyStore.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/TransportKeyStore.cpp)) — **reuse this function directly, do not reimplement.**
- `transport_codes[1]` = the *return / home* region. Upstream still has this as a `REVISIT` (`MyMesh::sendFloodScoped` sets `codes[1] = 0`). **We set it to 0 too** until upstream finalises it.
- Codes `{0, 0}` is the special "send to nowhere" sentinel — never emit that.

### Why this is "companion only" and what it does *not* touch

| Concern | Repeater (NOT us) | Companion (us) |
|---------|-------------------|----------------|
| Decide which regions to re-flood | `RegionMap::findMatch(pkt, REGION_DENY_FLOOD)` drops non-matching floods ([`simple_repeater/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_repeater/MyMesh.cpp) `allowPacketForward`) | — not needed; a handheld doesn't relay |
| Region hierarchy / parent-child tree | `RegionEntry.parent`, `putRegion`, deny flags | — not needed; flat list of scopes is enough |
| Stamp our own outgoing floods with a scope | — | **yes — the whole feature** |
| Receive packets in any region | — | **no filtering** — the mesh already decided to deliver it to us; `filterRecvFloodPacket` stays `return false` |

So for the companion the work is: **(1)** store a chosen scope, **(2)** stamp it onto outgoing floods, **(3)** a UI to manage/select it. That's it.

### Current state in SigurdOS

Everything is sent **unscoped** today. `SigurdMeshV2` extends `BaseChatMesh`, whose `sendFloodScoped()` is a no-op pass-through:

```cpp
// lib/meshcore/src/helpers/BaseChatMesh.cpp
void BaseChatMesh::sendFloodScoped(const ContactInfo& r, mesh::Packet* p, uint32_t d) { sendFlood(p, d); }
void BaseChatMesh::sendFloodScoped(const mesh::GroupChannel& c, mesh::Packet* p, uint32_t d) { sendFlood(p, d); }
```

Both are `virtual` (declared in [`BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h) ~L120-121) and are the funnel for **every** flood the base class emits — DMs (`BaseChatMesh::sendMessage`), channel messages (`sendGroupMessage`), ACKs, and return-path replies. Overriding the two methods is the **single hook point** for the entire feature. (Adverts call `sendFlood(pkt)` *directly* in `SigurdMeshV2::sendAdvert` — `src/mesh/sigurd_mesh_v2.h:1455/1463` — so they bypass scoping and stay wildcard-floodable. That is correct: you want everyone to discover you regardless of region.)

> ⚠️ **Not to be confused with** the existing `add_act(... "Regions", "region", ...)` row in `src/ui/screens.cpp:2678`. That sends the literal text `region` as a **remote admin CLI command to a repeater contact** — it is unrelated to this feature and should stay as-is.

### Recommended approach — lightweight companion scope store

The library's `RegionMap` + `TransportKeyStore` are built for repeaters (tree, deny-flood gating) and the private-key keystore methods (`loadKeysFor`/`saveKeysFor`) are unimplemented stubs. **Do not pull in `RegionMap` for the companion.** Mirror what the companion-radio firmware itself does — it stores a single default scope as `default_scope_name[31]` + `default_scope_key[16]` in `NodePrefs` ([`companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h)) plus a transient override. We extend that to a small saved list so the UI can offer a picker.

**Data model (companion-specific, new):**

```cpp
// proposed: src/mesh/regions.h
struct SigurdRegion {
    char    name[31];     // "#london" or "$crew" — includes the # / $ prefix
    uint8_t key[16];      // public: SHA256("#"+name)[0..15];  private: user secret
};
#define SIGURD_MAX_REGIONS 8   // companion needs far fewer than MAX_REGION_ENTRIES(32)
```

- **Persistence:** a tiny SPIFFS file (e.g. `/regions.dat`: count + array of `SigurdRegion`) **or** an NVS blob. Keep it separate from `NodePrefs` so the existing prefs schema/migration is untouched. Store the *selected* region's name in `NodePrefs` (one new `char active_region[31]` field) so the choice survives reboot. Default empty ⇒ wildcard ⇒ **identical to today's behaviour** (opt-in, zero interop regression).
- **Transient state in `SigurdMeshV2`:** `TransportKey _send_scope; bool _send_unscoped;` mirroring `MyMesh::send_scope` / `send_unscoped`, for a per-session "send this one unscoped" override.

### File-by-file plan

1. **`src/mesh/regions.h` / `regions.cpp` (new)** — `SigurdRegion`, the saved list, load/save (SPIFFS), `deriveKey(name, out16)` for `#`/auto regions (`SHA256` over the full `#`-prefixed name — reuse `SHA256` already linked by the mesh layer; matches `TransportKeyStore::getAutoKeyFor`), and base64/hex decode for `$` private keys. **S**
2. **`src/hal/prefs.h` / `prefs.cpp`** — add `char active_region[31]` to `NodePrefs`, default `""` in `set_defaults()`, load/save it in the NVS path. **S**
3. **`src/mesh/sigurd_mesh_v2.h`** — override the two `sendFloodScoped()` virtuals. Logic (lifted from `MyMesh::sendFloodScoped`, [`companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) ~L486-520):
   ```cpp
   void sendFloodScoped(const ::ContactInfo& r, ::mesh::Packet* pkt, uint32_t d=0) override {
       sendScopedImpl(pkt, d);
   }
   void sendFloodScoped(const ::mesh::GroupChannel& c, ::mesh::Packet* pkt, uint32_t d=0) override {
       sendScopedImpl(pkt, d);
   }
   void sendScopedImpl(::mesh::Packet* pkt, uint32_t d) {
       if (_send_unscoped || _active_scope.isNull()) { sendFlood(pkt, d); return; }   // wildcard
       uint16_t codes[2];
       codes[0] = _active_scope.calcTransportCode(pkt);   // reuse library impl
       codes[1] = 0;                                      // home/return region: REVISIT upstream
       sendFlood(pkt, codes, d /*, path_hash_size=1 */);  // route type → TRANSPORT_FLOOD
   }
   ```
   Hold `TransportKey _active_scope` (loaded from the selected region; null = wildcard). Add `setActiveRegion(name)` / `clearActiveRegion()` that fills `_active_scope`. **M** (this is the protocol-critical part; needs device interop testing).
4. **`src/mesh/mesh_wrapper.h` / `.cpp`** — public C-style API for the UI: `int listRegions(SigurdRegion* out, int max)`, `bool addRegion(const char* name, const char* key_b64_or_null)` (null ⇒ derive for `#`), `bool removeRegion(const char* name)`, `bool setActiveRegion(const char* name)` (empty ⇒ wildcard), `const char* getActiveRegion()`, `void setSendUnscopedOnce(bool)`. Persist + push down to `SigurdMeshV2`. **S**
5. **`src/ui/screens.cpp` (+ `navigation` / `home_screen` if a launcher icon is wanted)** — a **Regions screen**: list saved regions with the active one checked; "Add region" dialog (name field; for `$` private a 16-byte key field, base64); tap to set active; long-press/▶ to delete; a "Public (unscoped)" entry that maps to wildcard. Follow screen conventions (`make_screen_full`, `apply_dark_bg`, `theme.h`, zero-radius, static `user_data` for callbacks). Surface the active region somewhere persistent (e.g. a chip on the chat top bar or a Settings → Network row). **M**
6. **Settings hook** — a "Region: #london / Public" row under Settings → Network that deep-links to the Regions screen.

### Design decisions (call these out in the PR)

- **Adverts & path-returns:** adverts stay **unscoped** (discovery must cross regions). Return-path replies and ACKs ride through `sendFloodScoped`, so they inherit the active scope — consistent with upstream `MyMesh`.
- **`path_hash_mode`:** upstream passes `_prefs.path_hash_mode + 1` as `path_hash_size`. SigurdOS uses the default `1` everywhere today; keep `path_hash_size = 1` unless/until a `path_hash_mode` pref is also added (out of scope here).
- **Opt-in:** empty `active_region` ⇒ wildcard flood ⇒ byte-identical to current firmware. No silent behaviour change.
- **Private key entry:** validate decoded length is exactly 16 bytes; reject otherwise. Never log raw keys (debug builds).

### Testing

- **Native (`pio test -e native_test`):** a `test_regions` module that (a) checks `deriveKey("#test")` equals `SHA256("#test")[0..15]`, (b) feeds a known packet + key into `TransportKey::calcTransportCode` and asserts a **fixed expected code** (golden vector — capture one from a real companion-radio build or the upstream unit, so we prove wire compatibility), (c) asserts `_send_unscoped`/null scope produces `ROUTE_TYPE_FLOOD` and a set scope produces `ROUTE_TYPE_TRANSPORT_FLOOD` with the right code, (d) save/load round-trips the region list. Mock SPIFFS via the existing `test/mocks` filesystem stub.
- **Device interop:** with a region-aware repeater (or a second companion-radio node) configured to deny-flood everything except `#test`, confirm a SigurdOS message scoped to `#test` is re-flooded and one scoped to `#other` is dropped. State the method in the PR ("Physical hardware test").

### MeshCore reference

- [`src/helpers/TransportKeyStore.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/TransportKeyStore.h) / [`.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/TransportKeyStore.cpp) — `TransportKey`, `calcTransportCode()` (reuse), `getAutoKeyFor()` (public-hashtag key = `SHA256(name)`)
- [`src/Packet.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Packet.h) — `transport_codes[2]`, `ROUTE_TYPE_TRANSPORT_FLOOD`(0x00) vs `ROUTE_TYPE_FLOOD`(0x01), `hasTransportCodes()`
- [`src/Mesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/Mesh.h) / `Mesh.cpp` — `sendFlood(pkt, uint16_t* transport_codes, delay, path_hash_size)` (sets `header |= ROUTE_TYPE_TRANSPORT_FLOOD`)
- [`src/helpers/BaseChatMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.cpp) — the `sendFloodScoped()` pass-throughs we override
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `sendFloodScoped()` reference logic; `CMD_SET_FLOOD_SCOPE_KEY` (54), `CMD_SET/GET_DEFAULT_FLOOD_SCOPE` (63/64); `send_scope` / `send_unscoped`
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `default_scope_name`, `default_scope_key`, `path_hash_mode`
- [`src/helpers/RegionMap.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/RegionMap.h) / [`.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/RegionMap.cpp) — repeater-side gating, **for reference only; not used by the companion**

---

## Companion BLE — Connect to the Official MeshCore App (PLANNED) — L

**Goal:** let the T-Deck pair with and serve the **official MeshCore phone app** (Android/iOS) over Bluetooth LE, speaking the same companion frame protocol as a stock companion radio. The phone becomes a full client of the T-Deck's radio — sync contacts, read/send DMs and channel messages, configure the radio — *alongside* the built-in LVGL UI.

> **The crux of this feature is clean two-way sync, not the transport.** The T-Deck must **keep its own message history (persisted to flash — see below)** and never lose it to app sync; messages and changes made on the T-Deck must show up in the app and vice-versa. The transport (BLE) is the easy part — the [State sync & message persistence](#state-sync--message-persistence--the-hard-part-read-this) section is the part that must be gotten right.

> **Reframed from "not planned".** This doc previously listed BLE as a *different product* ("the T-Deck is already a companion"). The repo owner has now requested it. The value: the official app as a richer / backup client on SigurdOS hardware, and standard MeshCore companion interop for users who prefer their phone. The built-in UI stays; BLE is additive.

### Transport — reuse the library's ESP32 BLE interface

The MeshCore lib already ships the exact transport the app expects: [`src/helpers/esp32/SerialBLEInterface.{h,cpp}`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/esp32/SerialBLEInterface.cpp). **Reuse it as-is** — do not write a new BLE stack.

- **Nordic UART Service (NUS):** service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`; **RX** `…0002` (phone→device, `WRITE`); **TX** `…0003` (device→phone, `NOTIFY`). The official app scans for this service UUID.
- **MTU** = `MAX_FRAME_SIZE` (172). One BLE write / one notify = **one protocol frame** (`frame[0]` = command/response/push code).
- **Pairing:** static PIN + MITM bonding (`ESP_LE_AUTH_REQ_SC_MITM_BOND`). The phone prompts for the PIN once, then bonds.
- **Device name:** `BLE_NAME_PREFIX` + `node_name` (prefix defaults to `"MeshCore-"`).
- **Decoupling:** the interface has 4-deep RX/TX frame queues; BLE-stack callbacks only touch those queues, the mesh is touched only from the main loop. `BLE_WRITE_MIN_INTERVAL = 60 ms` paces notifies.

### Protocol surface

`frame[0]` selects the operation. Companion firmware v1.15.0 defines **60+ `CMD_*`**, **28 `RESP_CODE_*`**, **16 `PUSH_CODE_*`** — all in one ~1150-line dispatcher: [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) `handleCmdFrame()`.

**Connect handshake the app drives (must be byte-accurate or the app reports "incompatible"):**

1. `CMD_DEVICE_QUERY` (22) → `RESP_CODE_DEVICE_INFO` (13): `FIRMWARE_VER_CODE` (12), `MAX_CONTACTS/2`, `MAX_GROUP_CHANNELS`, `ble_pin` (4B), build date (12B), manufacturer (40B), firmware version str (20B), `client_repeat`, `path_hash_mode`.
2. `CMD_APP_START` (1) → `RESP_CODE_SELF_INFO` (5): adv type, tx power, **public key** (`PUB_KEY_SIZE`), lat/lon (int32 ×1e6), `multi_acks`, advert-loc policy, telemetry modes, manual-add flag, freq/bw (×1000), sf, cr, node name.

Then the app pulls state: `CMD_GET_CONTACTS` (4) → `CONTACTS_START`/`CONTACT`×N/`END_OF_CONTACTS`; `CMD_GET_CHANNEL` (31); `CMD_SYNC_NEXT_MESSAGE` (10) → `…MSG_RECV` / `NO_MORE_MESSAGES`. Outbound: `CMD_SEND_TXT_MSG` (2) → `RESP_CODE_SENT` (6), later `PUSH_CODE_SEND_CONFIRMED` (0x82). Async events the device pushes: `PUSH_CODE_MSG_WAITING` (0x83) "tickle", `PUSH_CODE_ADVERT`/`NEW_ADVERT`, `PUSH_CODE_PATH_UPDATED`, `PUSH_CODE_LOGIN_*`, etc. App protocol level negotiated via `app_target_ver` (`cmd_frame[1]` of DEVICE_QUERY/APP_START) — honour the `>= 3` message-format branches.

### Architectural seam — the crux

The protocol host (`MyMesh`) and SigurdOS's mesh (`SigurdMeshV2`) are **sibling `BaseChatMesh` subclasses**. Every mesh operation the dispatcher needs — `lookupContactByPubKey`, `sendMessage`, `getContactByIdx`, channel get/set, `sendLogin`, `createAdvert` — already exists on `SigurdMeshV2` via inheritance. So this is a **protocol port onto the existing mesh**, not a mesh rewrite.

- **Port the protocol as a standalone `CompanionBridge`** holding `SigurdMeshV2&` + `BaseSerialInterface&`. Do **not** turn `SigurdMeshV2` into `MyMesh` — keep the phone protocol decoupled from the UI-facing mesh. The bridge implements `handleCmdFrame` + response/push framing + the offline queue + the contact/channel sync iterators, calling the inherited `BaseChatMesh` API.
- **Dual-consumer problem (main new integration point):** `SigurdMeshV2`'s `on*Recv` overrides already push events into the UI queue. The phone needs those *same* events (to offline-queue + emit `PUSH_CODE_MSG_WAITING`). Add a lightweight **event-listener hook** in `SigurdMeshV2` (message recv, advert recv, ACK/send-confirmed, path updated) that the bridge subscribes to, so both the UI **and** the phone are notified. Without this, messages arriving while the app is open won't reach it.
- **Offline queue:** port `OFFLINE_QUEUE` (16 frames) so messages received while the phone is disconnected are buffered and drained on `CMD_SYNC_NEXT_MESSAGE` after reconnect.
- **Persistence:** `MyMesh` uses `DataStore`/`DataStoreHost` for contacts/channels/prefs/blobs. SigurdOS already persists these via `mesh_wrapper` (SPIFFS). **Map the mutating commands** (`CMD_ADD_UPDATE_CONTACT`, `REMOVE_CONTACT`, `SET_CHANNEL`, `IMPORT/EXPORT_PRIVATE_KEY`) onto SigurdOS's existing storage + identity store. Do **not** introduce a second `DataStore`.

### State sync & message persistence — the hard part, read this

The stock companion is a **stateless dumb modem**: the phone app is the *only* UI and the *only* message store, and the radio's offline queue is **drain-on-read** (`getFromOfflineQueue()` *removes* each message as the app syncs it — see [`MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) `getFromOfflineQueue`). SigurdOS breaks both assumptions: it has **its own UI and its own message history**. So this is not "radio + app" — it is **two stateful peers sharing one identity**, i.e. a reconciliation problem. Three hard requirements, and how to meet each:

#### R1 — the T-Deck must NEVER lose its messages

1. **Persist messages to flash (prerequisite — should already be standard).** Today SigurdOS messages are **RAM-only**: per-channel arrays in [`src/ui/chat_screen.cpp`](../src/ui/chat_screen.cpp) (`ch_msgs[]` / `ch_msg_count[]`, capped at `NodePrefs.chat_msg_cap`, default 200) — **lost on every reboot** (contacts and channels persist; messages do not). Add an **append-only message log in SPIFFS** (per-conversation file or a single ring file, size-capped/rotated), written on every send/receive and **loaded into the chat UI on boot**. This is independent of BLE and overdue on its own — treat it as a foundational step that lands first.
2. **Keep the app-sync queue separate from the store.** The drain-on-read offline queue must be a **mirror for the phone**, never SigurdOS's store. On message arrival, **fan out to BOTH**: (i) the persistent log + chat UI, and (ii) the offline queue. Draining the queue to the app must never touch the log. → the T-Deck keeps its history regardless of what the app syncs or deletes.

#### R2 — sent messages sync both directions

- **App → T-Deck (clean, fully supported):** when the bridge handles `CMD_SEND_TXT_MSG`, after calling `sendMessage()` it must **also append the outgoing text to the persistent log + chat UI** (as a self/outgoing entry keyed by recipient + `msg_timestamp`). → messages composed on the phone show up on the T-Deck.
- **T-Deck → app (the protocol gap — be explicit):** the companion protocol has **no "device-originated send" frame.** The app only learns of sends *it* initiated (`CMD_SEND_TXT_MSG` → `RESP_CODE_SENT`, later `PUSH_CODE_SEND_CONFIRMED`) and messages *received* (the offline queue). There is **no `RESP_CODE_*`/`PUSH_CODE_*` meaning "the device sent this on its own."** So with the **unmodified official app**, a message composed on the T-Deck's own keyboard cannot appear in the app's history. Options:
  1. **Accept the limitation** (official-app compatible): T-Deck-composed messages still transmit fine over LoRa; they just aren't mirrored into the official app's thread.
  2. **Protocol extension:** define a new `PUSH_CODE_*` ("device sent message") that **our own client** (`SlopOS-client`, the Flutter app) renders. The official app ignores unknown push codes → true two-way authored-message sync only with a cooperating client.
  - **Recommendation:** ship R1 + the app→T-Deck direction now (works with the official app), and add the extension to `SlopOS-client` for full bidirectional authoring. **Dedup hazard:** do **not** echo a self-sent message back as a `RESP_CODE_CONTACT_MSG_RECV` — the app would mis-attribute it to the *recipient* as an incoming message.

#### R3 — other changes reflect both ways

- **Contacts:** use the built-in **incremental sync** — `CMD_GET_CONTACTS` carries an optional `since`; the radio replies only with contacts whose `contact.lastmod > since` and returns the new high-water mark in `RESP_CODE_END_OF_CONTACTS` (`_most_recent_lastmod`). SigurdOS shares `BaseChatMesh`'s contact table + `ContactsIterator`, so this works **provided SigurdOS bumps `contact.lastmod` (and calls `saveContacts()`) on every local change** — auto-add, favourite toggle, rename, manual add/remove — and applies app edits (`CMD_ADD_UPDATE_CONTACT` / `CMD_REMOVE_CONTACT`) to the *same* table + refreshes the UI. Emit `PUSH_CODE_CONTACT_DELETED` / `PUSH_CODE_CONTACTS_FULL` when the device evicts a contact.
- **Channels:** `CMD_GET_CHANNEL` / `CMD_SET_CHANNEL` ↔ SigurdOS's channel store (NVS) + chat channel-list refresh, both directions.
- **Clock:** `CMD_GET/SET_DEVICE_TIME` keeps clocks aligned — message timestamps are the ordering/dedup key, so drift causes mis-ordered or duplicated threads.
- **Read / unread state:** the protocol does **not** sync per-message read state — each client tracks its own. Document as a known limitation.

#### Cross-cutting: message identity & dedup

Key every message by **(conversation, sender pubkey-prefix, `sender_timestamp`)**. The persistent log dedups on this key so a message that arrives over LoRa *and* via an app round-trip is stored once; the offline-queue mirror uses the same key so a reconnecting app isn't handed duplicates. Where it hooks in: the **dual-consumer event hook** (above) is the fan-out point — extend it so message-arrival writes to *(persistent log + UI)* **and** *(offline queue)*, and so app-initiated sends also write to *(persistent log + UI)*. The persistent log becomes the shared store that both the chat UI and the sync layer read.

### Concurrency

BLE host-task callbacks (`onWrite`) must only enqueue into the interface RX queue (the library design already does this). **All mesh access stays on the main loop** via `bridge.loop()` → `_serial->checkRecvFrame()` → `handleCmdFrame()`. Never call mesh methods from a BLE callback. Per-loop work must be bounded (queues are 4 deep; LVGL must not starve).

### Build & resource considerations (call these out in the PR)

- **Flash:** the Arduino-ESP32 Bluedroid BLE stack adds ~0.7–1.3 MB. The board uses `default_16MB.csv` (app partition ~6.5 MB) — fits, but verify headroom after linking.
- **RAM / coexistence:** Bluedroid lives in **internal DRAM**, not PSRAM. SigurdOS already has **WiFi OTA** (`hal/wifi_ota.cpp`, `github_ota`). BLE **and** WiFi up simultaneously is tight on the ESP32-S3. Mitigations: bring BLE up on demand (`enable()`/`disable()`), don't run OTA and BLE at once, and/or evaluate **NimBLE** (much smaller) — but note the lib's `SerialBLEInterface` is **Bluedroid** (`BLEDevice.h`); a NimBLE path means a new interface impl.
- **New build env, off by default:** add `[env:SigurdOS_TDeck_ble]` with `-D SIGURDOS_COMPANION_BLE=1` (and a `BLE_PIN_CODE`-style enable) so the heavy stack only links where wanted, until validated. Ensure the ESP-IDF BT/BLE sdkconfig is enabled (Arduino default includes it, but it's dead-stripped if unreferenced).
- **Power:** the 2.4 GHz radio is independent of the SX1262 SPI bus (no bus conflict), but adds draw — flag battery impact and respect the auto-off/sleep paths.

### PIN / pairing / security

- Reuse the existing `NodePrefs.device_pin` as the BLE pairing PIN (maps to the protocol's `ble_pin`). MITM bonding ⇒ one-time PIN prompt on the phone.
- **A paired phone gets full device access**, including `CMD_EXPORT_PRIVATE_KEY` (exports the node's Ed25519 private key) and `CMD_IMPORT_PRIVATE_KEY`. This matches the official app's backup/restore, and is gated by PIN pairing — but it is sensitive. Decide policy: support it (interop) with a UI indicator when BLE is connected, and consider a toggle to disable private-key export.

### Phased implementation

- **Phase 0 — foundational persistence (lands first, independent of BLE):** add the SPIFFS message log (R1.1) so chat history survives reboot, and make message arrival/send fan out through the dual-consumer hook into *(persistent log + UI)*. Without this, none of the sync requirements can hold.
- **Phase 1 — MVP (app connects + basic DMs):** wire `SerialBLEInterface`; handshake (`CMD_DEVICE_QUERY`, `CMD_APP_START`); `CMD_GET_CONTACTS`; `CMD_SYNC_NEXT_MESSAGE` + offline queue **as a non-destructive mirror** of the persistent log (R1.2) + `PUSH_CODE_MSG_WAITING`; `CMD_SEND_TXT_MSG` + `RESP_CODE_SENT` + `PUSH_CODE_SEND_CONFIRMED`, **also appending the sent text to the log + UI** (R2 app→T-Deck); `CMD_GET/SET_DEVICE_TIME`; `CMD_GET_BATT_AND_STORAGE`. → app connects, lists contacts, reads & sends DMs, and the T-Deck keeps its own copy.
- **Phase 2:** channels (`CMD_GET/SET_CHANNEL`, `CMD_SEND_CHANNEL_TXT_MSG`, channel sync); adverts (`CMD_SEND_SELF_ADVERT`, `SET_ADVERT_NAME/LATLON`, `PUSH_CODE_ADVERT/NEW_ADVERT`); radio params (`CMD_SET_RADIO_PARAMS/TX_POWER/TUNING_PARAMS`); contact CRUD (`add/update/remove/share/export/import`).
- **Phase 3:** repeater login (`CMD_SEND_LOGIN`, `PUSH_CODE_LOGIN_*`), trace / path discovery (`CMD_SEND_TRACE_PATH`, `CMD_RESET_PATH`), telemetry, message signing (`CMD_SIGN_*`), private-key export/import, factory reset, and the flood-scope commands (`CMD_SET_DEFAULT_FLOOD_SCOPE` etc.) — which tie into the [Regions](#regions--companion-flood-scope-planned--m) feature.

### File-by-file plan

1. **`platformio.ini`** — new `[env:SigurdOS_TDeck_ble]` (extends the release env) with `-D SIGURDOS_COMPANION_BLE=1` + BLE name/PIN defines; confirm partition headroom; ensure BT/BLE enabled. **S**
2. **`src/mesh/message_store.{h,cpp}` (new — Phase 0)** — append-only SPIFFS chat log (per-conversation, size-capped/rotated), dedup-keyed on (conversation, sender prefix, `sender_timestamp`); load into the chat UI on boot. Becomes the shared store the chat UI **and** the BLE sync layer read; the offline queue is a non-destructive mirror of it. **M**
3. **`src/comms/companion_bridge.{h,cpp}` (new)** — owns the `SerialBLEInterface` + a `SigurdMeshV2&`; ports `handleCmdFrame`, `writeOKFrame`/`writeErrFrame`/`writeContactRespFrame`, the offline queue, and the contacts/sync iterators. Driven by `loop()`. **L**
4. **`src/mesh/sigurd_mesh_v2.h`** — add an event-listener hook (message/advert/ack/path-updated) so the **persistent log, the UI, and the bridge** all receive the same mesh events (the fan-out point for R1/R2); bump `contact.lastmod` + `saveContacts()` on local contact changes for R3; expose any `BaseChatMesh` accessors the bridge needs. **M**
5. **`src/mesh/mesh_wrapper.{h,cpp}`** — construct/init/loop the bridge; map the protocol's persistence + identity commands onto existing SigurdOS storage; wire the message store into the existing message path. **M**
6. **`src/hal/prefs.{h,cpp}`** — surface `device_pin` as the BLE PIN; add `ble_enabled`. **S**
7. **`src/main.cpp`** — bring up the bridge after mesh init; call `bridge.loop()` in the main loop. **S**
8. **`src/ui/screens.cpp` (+ home/nav)** — a **Bluetooth / Phone App** screen: enable toggle, PIN display, connection status; a connected indicator on the top bar; optional connect buzzer. Follow screen conventions (`make_screen_full`, `apply_dark_bg`, `theme.h`). **M**

### Testing

- **Native (`pio test -e native_test`):** a `test_companion_protocol` module with a `MockSerialInterface` (implements `BaseSerialInterface` over in-memory buffers). Feed `CMD_DEVICE_QUERY` / `CMD_APP_START` frames and assert the `RESP_CODE_DEVICE_INFO` / `RESP_CODE_SELF_INFO` **byte layouts** match **golden frames captured from companion-radio v1.15.0** (this is what proves app compatibility). Round-trip `CMD_SEND_TXT_MSG` → assert `sendMessage` invoked + `RESP_CODE_SENT`. Cover offline-queue ordering and the `app_target_ver` branches. No BLE hardware needed.
- **Native — sync & persistence (`test_message_store`):** assert messages survive a simulated reboot (write → reload → present); assert **draining the offline queue does NOT remove from the persistent log** (R1.2); assert an `app→T-Deck` `CMD_SEND_TXT_MSG` appends a self/outgoing entry to the log (R2); assert the (conversation, sender, timestamp) dedup so a message seen twice is stored once; assert a local contact change bumps `lastmod` so a subsequent `CMD_GET_CONTACTS since=<hwm>` returns it (R3). Mock SPIFFS via the existing filesystem stub.
- **Device:** flash the BLE env, pair the official MeshCore Android/iOS app via PIN, verify handshake → contact sync → send/receive DM + channel message over the air. Declare **"Physical hardware test"** in the PR (remote-test mode cannot validate the BLE stack).

### MeshCore reference

- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `handleCmdFrame()` (the `CMD_*`/`RESP_CODE_*`/`PUSH_CODE_*` dispatcher), `startInterface()`, `loop()`, `checkSerialInterface()`, offline queue, `getBLEPin()`, `CMD_DEVICE_QUERY`/`CMD_APP_START` handshake
- [`examples/companion_radio/MyMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.h) — class layout (`BaseChatMesh` + `DataStoreHost`), frame structs, `FIRMWARE_VER_CODE 12`, `OFFLINE_QUEUE_SIZE`, `BLE_NAME_PREFIX`
- [`examples/companion_radio/main.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/main.cpp) — wiring: `serial_interface.begin(BLE_NAME_PREFIX, node_name, getBLEPin())` + `the_mesh.startInterface(serial_interface)` + `loop()`
- [`src/helpers/esp32/SerialBLEInterface.{h,cpp}`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/esp32/SerialBLEInterface.cpp) — the ESP32 NUS transport (UUIDs, MTU, PIN/MITM, frame queues) — **reuse directly**
- [`src/helpers/BaseSerialInterface.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseSerialInterface.h) — abstract interface (`MAX_FRAME_SIZE 172`, `writeFrame`/`checkRecvFrame`) the bridge **and the test mock** implement
- [`examples/companion_radio/NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `ble_pin`, `node_name`

---

## Infrastructure-only (documented, not planned)

> These turn the T-Deck into something other than a handheld companion. Listed for completeness; excluded from the implementation plan.

### BLE companion protocol (connect the official phone app) — ✅ NOW PLANNED

- **Moved.** Previously listed here as "a different product." The repo owner has requested it, so it is now a planned feature with a full implementation plan — see [Companion BLE — Connect to the Official MeshCore App](#companion-ble--connect-to-the-official-meshcore-app-planned--l) above. (A USB-serial variant via `ArduinoSerialInterface` is a trivial subset of the same protocol once the bridge exists.)

---

### Repeater-side region gating (`RegionMap` deny-flood) — ❌ NOT DOING

- **Reason:** This is the *repeater* half of regions — deciding which floods to re-transmit (`RegionMap::findMatch` + `REGION_DENY_FLOOD`). A handheld companion does not relay floods. The **companion** half (stamping our own outgoing floods with a scope) is a planned feature — see [Regions — Companion Flood Scope](#regions--companion-flood-scope-planned--m) above.
- **Reference (for posterity):** [`src/helpers/RegionMap.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/RegionMap.h) / [`RegionMap.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/RegionMap.cpp) — `RegionMap`, `RegionEntry`, `REGION_DENY_FLOOD`/`REGION_DENY_DIRECT`, `MAX_REGION_ENTRIES`; [`examples/simple_repeater/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_repeater/MyMesh.cpp) — `allowPacketForward`, `region_map.findMatch`

---

### Launcher compatibility — M

A niche build target for running under `bmorcelli/Launcher`. Not relevant to the standalone companion experience. (See `KNOWN_ISSUES.md`.)

*(No MeshCore reference — local build/HAL.)*

---

*Last reviewed: 2026-06-04 against companion firmware v1.15.0 and dev branch. Planned: Regions — Companion Flood Scope; Companion BLE — Connect to the Official MeshCore App. All other previously tracked features are ✅ implemented; declined items remain above for reference.*
