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

## Infrastructure-only (documented, not planned)

> These turn the T-Deck into something other than a handheld companion. Listed for completeness; excluded from the implementation plan.

### BLE companion protocol (expose T-Deck as a radio modem) — L

MeshCore's BLE UART companion protocol (Nordic UART service, UUID `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`) lets a phone app use the radio as a modem. The T-Deck *is* the companion already, so this is a different product.

**MeshCore reference:**
- [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — the authoritative `CMD_*` / `RESP_CODE_*` / `PUSH_CODE_*` frame protocol
- [`src/helpers/BaseSerialInterface.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseSerialInterface.h) — frame transport (`MAX_FRAME_SIZE = 172`); [`ArduinoSerialInterface.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/ArduinoSerialInterface.cpp) — concrete impl

---

### Repeater-side region gating (`RegionMap` deny-flood) — ❌ NOT DOING

- **Reason:** This is the *repeater* half of regions — deciding which floods to re-transmit (`RegionMap::findMatch` + `REGION_DENY_FLOOD`). A handheld companion does not relay floods. The **companion** half (stamping our own outgoing floods with a scope) is a planned feature — see [Regions — Companion Flood Scope](#regions--companion-flood-scope-planned--m) above.
- **Reference (for posterity):** [`src/helpers/RegionMap.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/RegionMap.h) / [`RegionMap.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/RegionMap.cpp) — `RegionMap`, `RegionEntry`, `REGION_DENY_FLOOD`/`REGION_DENY_DIRECT`, `MAX_REGION_ENTRIES`; [`examples/simple_repeater/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/simple_repeater/MyMesh.cpp) — `allowPacketForward`, `region_map.findMatch`

---

### Launcher compatibility — M

A niche build target for running under `bmorcelli/Launcher`. Not relevant to the standalone companion experience. (See `KNOWN_ISSUES.md`.)

*(No MeshCore reference — local build/HAL.)*

---

*Last reviewed: 2026-06-03 against companion firmware v1.15.0 and dev branch. Planned: Regions — Companion Flood Scope. All other previously tracked features are ✅ implemented; declined items remain above for reference.*
