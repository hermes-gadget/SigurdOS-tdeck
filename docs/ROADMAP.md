# SigurdOS-TDeck Implementation Roadmap

**Audience: AI agents implementing features.** This is the *ordered, how-to* companion to [`MISSING_FEATURES.md`](MISSING_FEATURES.md). Read both:

| Document | Answers | Use it for |
|----------|---------|-----------|
| [`MISSING_FEATURES.md`](MISSING_FEATURES.md) | **What** is missing and **where** to find it in upstream MeshCore | The catalog: per-feature description + clickable `meshcore-dev/MeshCore` source links |
| **`ROADMAP.md`** (this file) | **In what order** to build it and **how** | The plan: dependencies, step-by-step guidance, pitfalls, test plans, done-criteria |

> **This file replaces the "Implementation Plan" section that used to live at the bottom of `MISSING_FEATURES.md`.** That section was removed; this is its expanded successor.

---

## ⚠️ READ THIS FIRST — mandatory context for any agent

You are working on embedded C++ firmware. Mistakes here are expensive (they require a hardware reflash to observe). **Slow down and follow the process.**

### Required reading before you touch anything
1. [`CLAUDE.md`](../CLAUDE.md) / [`AGENTS.md`](../AGENTS.md) — architecture, conventions, the **Code Audit Checklist**, and **Rejection triggers**. Do not skip the rejection triggers — they are the exact reasons a PR gets auto-declined.
2. [`CONTRIBUTING.md`](../CONTRIBUTING.md) — the contribution workflow. **Issue-first is mandatory: no issue = no PR.**
3. [`docs/KNOWN_ISSUES.md`](KNOWN_ISSUES.md) — known bugs. Several roadmap items overlap; check before starting.
4. [`docs/MISSING_FEATURES.md`](MISSING_FEATURES.md) — the upstream source references for the feature you're building.

### The non-negotiable loop for every task
```
1. Find or open a GitHub issue on hermes-gadget/SigurdOS-tdeck
2. git checkout dev && git pull origin dev
3. git checkout -b <type>/<short-name>-<issue#>
4. pio test -e native_test -v        # confirm baseline GREEN before you change anything
5. ... make changes + ADD TESTS ...
6. pio test -e native_test -v        # all green
7. pio run -e SigurdOS_TDeck           # firmware must build
8. commit (conventional message), push, open PR targeting dev
9. PR body MUST declare hardware testing: "Remote test", "Physical hardware test", or both
```
If you cannot run hardware, say so explicitly in the PR and use **remote test mode** where the feature allows it (see CLAUDE.md → Remote Test Controller). **Never** switch the build to `SigurdOS_TDeck_remote_test` or touch the radio config without the user's explicit say-so.

### The architecture you must respect
```
UI layer (src/ui/*)                ← screens, never touches MeshCore directly
      │  calls only
      ▼
mesh_wrapper.h  (sigurdos::mesh::*)   ← THE SEAM. Public API the UI depends on.
      │  wraps
      ▼
SlopMesh  (src/mesh/slop_mesh.h)    ← our subclass of MeshCore
      │  extends
      ▼
::mesh::Mesh  (lib/meshcore/)       ← upstream library
```
**Golden rule: keep the `mesh_wrapper.h` public API stable.** The UI is insulated by it. If you change mesh internals, translate back to the existing wrapper structs (`sigurdos::mesh::ContactInfo`, `MeshMessage`, etc.) so screens don't change. This seam is what makes large refactors (see Phase 0) survivable.

### Traps that have already bitten this codebase (do not repeat)
- **ACK value is NOT a CRC-32.** It is the first 4 bytes of SHA-256 over a recipient-pubkey-dependent buffer. Computing a CRC will *never* match. (`MISSING_FEATURES.md` → "Message delivery status".)
- **Channel hash matching uses ONE byte** in the packet header, not the full hash. `searchChannelsByHash` compares `hash[0]` only — matching upstream `BaseChatMesh`. ~11% collision on 8 channels is expected; do not "fix" it to a full memcmp or you break interop.
- **`strncpy` does not null-terminate** when the source is too long. Always `dest[n-1] = '\0'`.
- **UTF-8 truncation** can split a 4-byte emoji mid-codepoint → invalid UTF-8 over the mesh. Use `sigurdos::utf8_truncate_bytes()` (already used in `slop_mesh.h`).
- **`lv_obj_del` inside an event handler** must be `lv_obj_del_async()`.
- **`lv_scr_load_anim(..., true)`** deletes the old screen + all children; register `LV_EVENT_DELETE` to null any globals pointing into it.
- **Stack arrays as LVGL `user_data`** dangle on click. Use `static` arrays.
- **Adding a `NodePrefs` field**: old saved prefs won't have it. `prefs_get()` zero-fills missing keys — follow the existing default-value pattern in `NodePrefs::set_defaults()` (`src/hal/prefs.h`).
- **`ESP.restart()`** before a flash write completes loses the write. `saveState()`/`saveChannels()` then delay ~100 ms before restart.
- **Debug output** must be guarded by `#if defined(SIGURDOS_DEBUG)` — unconditional `Serial.printf` is a rejection trigger.

### Where things live (verified paths)
| Concern | File |
|---------|------|
| Mesh subclass | `src/mesh/slop_mesh.h` |
| Wrapper / public API | `src/mesh/mesh_wrapper.cpp` / `.h` |
| Persisted settings | `src/hal/prefs.cpp` / `.h` (`struct NodePrefs`, `prefs_get/set/save`) |
| GPS NMEA | `src/hal/gps.cpp` / `.h` |
| Keyboard (I2C) | `src/hal/keyboard.cpp` / `.h` |
| Settings / Radio Setup / Finder / Signal / Contacts screens | `src/ui/screens.cpp` |
| Chat (DM + channels) | `src/ui/chat_screen.cpp` |
| Navigation / back-stack | `src/ui/navigation.cpp` |
| Theme helpers / colors | `src/ui/theme.h` |
| Mesh tests | `test/test_mesh_messaging/`, `test/test_mesh_wrapper/` |
| Mocks (Arduino, lvgl, RadioLib, etc.) | `test/mocks/` |
| Upstream MeshCore | `lib/meshcore/` (submodule, v1.15.0) |

> **Note for the agent:** the old MISSING_FEATURES plan referenced `test/slop_mesh_test.cpp` — that file does **not** exist. The real mesh tests are the two dirs above.

---

## Phase 0 — Migrate `SlopMesh` onto `BaseChatMesh` (foundational — do before Phase 4) ✅ COMPLETED

**Status: ✅ Done.** PR #223 (V2 behind flag), PR #224 (cutover: V2 as default). All parity items pass.

**This is the decided architectural direction.** It is the single highest-leverage change on the board: it converts most of Phase 4 (and chunks of 3 and 5) from large bespoke protocol work into small "expose an existing method" PRs. **Do not start any Phase 4 feature until this migration has cut over.**

> **Agent note:** This is large and risky. Do it incrementally behind a flag, prove parity before deleting anything, and expect several PRs — not one. If anything in this section is unclear, stop and ask the user rather than guessing.

### Why we're doing it

`SlopMesh` currently extends `::mesh::Mesh` directly — a deliberately minimal subclass. One layer up in the *same* library sits `BaseChatMesh` ([`lib/meshcore/src/helpers/BaseChatMesh.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/BaseChatMesh.h)), which the reference companion radio uses. It already implements, correctly and tested:

- `sendMessage()` **with ACK tracking** (`expected_ack`), `processAck()`
- `sendLogin()`, `sendCommandData()` (remote repeater/room admin), connection keep-alive sessions
- `sendRequest()` x2 (the REQ/RESPONSE framework), `sendAnonReq()`, `sendGroupData()`
- `resetPathTo()`, `removeContact()`, `addContact()`, `lookupContactByPubKey()`
- `exportContact()` / `importContact()` (QR/URI sharing payloads)
- correct `onAckRecv`, path learning, send-timeout handling

**Almost the entire Phase 4 is already written inside `BaseChatMesh`.** Building those on raw `Mesh` would mean reimplementing them by hand — repeatedly hitting the traps listed above. Inheriting `BaseChatMesh` gets them for free.

> **This is NOT a "MeshCore version" change.** We are on current MeshCore (v1.15.0). `Mesh` and `BaseChatMesh` both ship in it. We are moving `SlopMesh` up one *layer* of subclassing — no submodule bump required.

### Why the blast radius is small

The cost is concentrated in **one place**: adopting `BaseChatMesh`'s `ContactInfo[MAX_CONTACTS]` and `ChannelDetails` models in place of our `SlopContact`/`SlopChannel`, and implementing its ~12 pure-virtual hooks. Because the UI only ever sees `sigurdos::mesh::ContactInfo` (the wrapper struct in `mesh_wrapper.h`), the change is contained to **`slop_mesh.h` + `mesh_wrapper.cpp`** — translate `BaseChatMesh::ContactInfo` → the wrapper struct and **the UI layer does not change at all.** That seam is exactly why this migration is survivable.

### Migration plan — spike, prove parity, then cut over (never big-bang)

1. **Spike branch.** Create `SlopMeshV2 : public BaseChatMesh` alongside the existing `SlopMesh`. Implement the pure virtuals (`onMessageRecv`, `onChannelMessageRecv`, `onDiscoveredContact`, `processAck`, `onContactResponse`, `onCommandDataRecv`, `onSignedMessageRecv`, `calcFloodTimeoutMillisFor`, `calcDirectTimeoutMillisFor`, `onSendTimeout`, `onContactRequest`). Use [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) as a complete reference implementation of every one of these.
2. **Define `MAX_CONTACTS` and `MAX_GROUP_CHANNELS`** via build flags (`BaseChatMesh` reads them; defaults are 32 / undefined). Match or exceed today's `SLOP_MAX_CONTACTS = 64` / `SLOP_MAX_CHANNELS = 8`.
3. **Persistence.** `BaseChatMesh` stores contacts/channels via `getBlobByKey`/`putBlobByKey` (the `DataStoreHost` pattern in the companion radio). Wire these to NVS/SPIFFS. **Note:** existing devices will lose their saved contacts on upgrade — that is acceptable because contacts are re-learned from adverts within minutes. Identity is stored separately and must be preserved.
4. **Keep the wrapper API byte-identical.** `mesh_wrapper.cpp` translates `BaseChatMesh::ContactInfo` → `sigurdos::mesh::ContactInfo`. Do not change any signature in `mesh_wrapper.h`.
5. **Parity checklist — prove ALL of these pass before cutover** (native tests + hardware/remote-test):
   - [x] DM send + receive (`test_mesh_messaging`)
   - [x] Channel text send + receive (hashtag + PSK channels)
   - [x] Advert parse → contact added with name/type/location/RSSI/SNR
   - [x] Contact LRU eviction at the cap
   - [x] Trace route round-trip
   - [x] Ping Nearby (control PING/PONG)
   - [x] Duty-cycle factor override still applied
   - [x] Channel persistence across reboot
6. **Cutover.** Replace `SlopMesh` with `SlopMeshV2`, delete the old class, update `mesh_wrapper.cpp` construction. Run the full suite again.
7. **Then** proceed to Phase 4 — each feature is now mostly wrapper plumbing + UI.

> **Sequencing tip:** the Phase 1 quick wins below are independent of this migration and make good warm-up PRs while the migration is being planned/reviewed. Everything in Phase 4, and the ACK work in Phase 3, waits for cutover.

---

## Phase 1 — Quick wins (mostly UI over backend that already exists) ✅ COMPLETED

> **Status: ✅ All 8 items done.** See overview below for the specific PRs. Safe to do before or during Phase 0 migration. Great first tasks. Each is small; do them as separate PRs.

### 1.1 — RX gain boost toggle — S ✅
- **Status:** Done. Toggle in Radio Setup → `s_rx_gain` → `applyRadioParams()` at init. Persisted in NodePrefs.
- **PR:** Part of Phase 0 development (early PRs on dev).

### 1.2 — Duty cycle UI — M ✅
- **Status:** Done. Settings cycle control (0/1/5/10/25/50/100) + remaining budget displayed on Signal screen via `getRemainingTxBudget()`.
- **PR:** Part of Phase 0 development (early PRs on dev).

### 1.3 — Zero-hop ping in Finder UI — M ✅
- **Status:** Done. "Ping Nearby" button with 3 s listening countdown, 30 s cooldown, results sorted by RSSI. All backend `sendPingNearby()`, `pingIsActive()`, etc. wired to Finder screen.
- **PR:** Part of Phase 0 development (early PRs on dev).

### 1.4 — Graceful shutdown from UI — S ✅
- **Status:** Done. "Shut down" button in Settings with confirmation dialog. Calls `saveState()` + `saveChannels()`, delay, then deep sleep.
- **PR:** Part of Phase 0 development (early PRs on dev).

### 1.5 — Message timestamps in chat bubbles — S ✅
- **Status:** Done. `format_time()` renders `HH:MM` in bubble header (sender name + timestamp row). For >24 h old messages shows date.
- **PR:** Part of Phase 0 development (early PRs on dev).

### 1.6 — Channel removal — S ✅
- **Status:** Done. Delete/× button on each channel row in channel list. Calls `removeChannel(idx)` → shifts `_channels` array → persists via `saveChannels()`.
- **PR:** Part of Phase 0 development (early PRs on dev).

### 1.7 — Contact removal — S ✅
- **Status:** Done. "Remove Contact" button on Contact Detail screen with confirmation dialog. Calls `removeContact()` through the wrapper.
- **PR:** Part of Phase 0 development (early PRs on dev).

### 1.8 — Reset path to a contact — S ✅
- **Status:** Done. "Reset Path" button on Contact Detail screen. Calls `resetPathTo()` → clears `out_path_len` → next message floods and re-learns path.
- **PR:** Part of Phase 0 development (early PRs on dev).

---

## Phase 2 — Radio & node configuration

> Some `NodePrefs` fields already exist (`advert_interval`, `advert_type`, `flood_max_hops`) — check `src/hal/prefs.h` before adding new ones.

### 2.1 — Periodic auto-advert — S ✅
- **Status:** Done. *(Audit 2026-06-01 reclassified this from "declined" — the feature is in fact wired.)* `advert_interval` (half-minutes, `0` = disabled) in `NodePrefs`, a Settings cycle in `screens.cpp`, persisted in `prefs.cpp`, and a re-advert timer in `mesh::loop()` (`mesh_wrapper.cpp`) that calls `sendAdvert()` on the interval. Defaults to `0` (off).
- **Upstream ref:** MISSING_FEATURES → "Periodic auto-advert" (`NodePrefs::advert_interval`).

### 2.2 — Temporary radio config (no NVS write) — ❌ NOT NEEDED
> User declined — not implementing.

### 2.3 — Auto-add contact configuration — M ✅
- **Status:** Done. Per-type bitmask (`autoadd_config`) in NodePrefs gates auto-add by contact type (chat/repeater/room/sensor via bits 1-4). Separate `autoadd_max_hops` for range limiting. Settings: "Auto-add: All types" cycle + "Add max hops" cycle.
- **PR:** #228.

### 2.4 — Advert location-share policy (privacy) — S ✅
- **Status:** Already done. `share_location` toggle in Settings (set_defaults: true). Wireframe already in prefs for lat/lon gating.

### 2.5 — GPS enable / read-interval — S ✅
- **Status:** Done. `gps_enabled` (bool) + `gps_interval` (uint16_t seconds) in NodePrefs. `sigurdos_gps_init()`/`sigurdos_gps_loop()` gated on the pref. Settings: GPS ON/OFF toggle + interval cycle (0/1/5/10/30/60s).
- **PR:** #227.

### 2.6 — Custom variables (key-value store) — S ✅
- **Status:** Done. SPIFFS-backed `key=value` store. Terminal commands: `setvar`, `getvar`, `delvar`, `listvars`.
- **PR:** #229.

### 2.7 — Keyboard backlight & message-cap controls — S each ✅
- **Status:** Already done. Backlight dialog (+/- slider) and chat history cap dialog in Settings, both fully wired.

---

## Phase 3 — Messaging polish ✅ COMPLETED

> **Status: ✅ All 3 items done.** See overview below for the specific PRs.

### 3.1 — Message delivery status (ACK ticks) — M ✅
- **Status:** Done. PendingAck ring buffer in SlopMeshV2, `processAck()` matches 4-byte SHA-256 ACK against pending outgoing DMs. `isMessageAcked()` bridge to UI. ✓ indicator in self-sent chat bubbles.
- **PR:** #232.

### 3.2 — Message search — M ✅
- **Status:** Done. 'S' button in top bar toggles inline search bar. Case-insensitive substring filter over message text and sender. Trackball Up/Down cycles through matches with auto-scroll and highlight. "No matching messages" when empty.
- **PR:** #234.

### 3.3 — Per-contact RSSI/SNR history graph — L ✅
- **Status:** Done. 64-entry circular buffer per-contact in SlopMeshV2, `lv_chart` line sparkline on Signal screen showing RSSI trend.
- **PR:** #236.

---

## Phase 4 — Infrastructure interaction ✅ COMPLETED

> **Status: ✅ Items 4.1–4.8 done (4.3 has an outstanding answer-side — see its note and 6.4). Items 4.9–4.10 declined — not implementing.**
> Phase 0 cutover was the prerequisite.

### 4.1 — Generic binary-request framework (REQ/RESPONSE) — M ✅
- **Status:** Done. `BaseChatMesh::sendRequest()` exposed through wrapper with tag→handler dispatch.
- **PR:** #251.

### 4.2 — Status request — M ✅
- **Status:** Done. `REQ_TYPE_GET_STATUS` request with NodeStatus UI panel.
- **PR:** #253.

### 4.3 — Telemetry queries (remote + self, CayenneLPP) — M ✅ ⚠️ answer-side outstanding
- **Status:** Remote query done. Send `REQ_TYPE_GET_TELEMETRY_DATA` and decode the CayenneLPP response (voltage, temp, humidity, lat-lon) — `requestTelemetry()` in `mesh_wrapper.cpp`. **Answering inbound telemetry is NOT implemented:** `SigurdMeshV2::onContactRequest()` (`sigurd_mesh_v2.h`) is a skeleton that returns `0`, so the T-Deck never replies with its own battery/telemetry when queried. *(Audit 2026-06-01 found the "answer inbound requests with own battery" claim inaccurate.)* The answer side + per-type policy is tracked as **6.4** below.
- **PRs:** #2907746, #0ee06ea.

### 4.4 — Path discovery request — M ✅
- **Status:** Done. Distinct from Trace — discovers route to a contact with unknown path.
- **PRs:** #944bded, #ae68b3d.

### 4.5 — Repeater/room login + remote administration — L ✅
- **Status:** Done. Dedicated repeater detail screen with login flow, password field, admin command terminal with live response polling.
- **PR:** #259.

### 4.6 — Room server message fetch — L ✅
- **Status:** Done. Login to room server, fetch stored posts, merge into message store.
- **PR:** #263.

### 4.7 — Anonymous requests (send) — M ✅
- **Status:** Done. Expose `BaseChatMesh::sendAnonReq()` through wrapper with UI.
- **PR:** #260.

### 4.8 — Group data datagrams — M ✅
- **Status:** Done. `sendGroupDatagram(channel, type_code, data, len)` + received-datagram type dispatch.
- **PR:** #265.

### 4.9 — Multipart messages — L ❌ NOT DOING
- **Reason:** User declined — not implementing.

### 4.10 — Raw custom payloads — L ❌ NOT DOING
- **Reason:** User declined — not implementing.

---

## Phase 5 — Identity, UI & security

### 5.1 — Contact locations on Map — M
- **Upstream ref:** MISSING_FEATURES → "Contact locations on Map screen". Coordinates already in `SlopContact` (`has_location`/`latitude`/`longitude`). Render labelled markers on the map canvas; tap → contact detail. Pure UI + map math.

### 5.2 — Factory reset — S ✅
- **Status:** Done. "Factory reset" action on the Settings → System screen (double-confirm) → `mesh::factoryReset()` clears NVS prefs + contacts + channels, regenerates identity, delays for flash, then reboots.
- **PR:** #275 (issue #274).

### 5.3 — Identity backup (export/import) — M
- **Upstream ref:** MISSING_FEATURES → "Identity backup". Export the private key (hex/QR); import re-keys the node. **Hard part:** on import, every contact's shared secret must be recomputed (`calcSharedSecret`). See [`src/helpers/IdentityStore.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/IdentityStore.h).

### 5.4 — QR code generation + URI import — L / M
- **Upstream ref:** MISSING_FEATURES → "QR code generation", "QR code / URI import". Add a tiny MIT QR encoder (~2 KB; check GPL-3.0 compatibility — a rejection trigger if not). Post-migration, `BaseChatMesh::exportContact`/`importContact` produce the payloads. URI scheme: `meshcore://contact/add?...` / `meshcore://channel/add?...`.

### 5.5 — Node stats query — S ✅
- **Status:** Done. Node Stats diagnostics panel surfacing sent/recv flood+direct counters, airtime totals, and duplicate/drop counts. Reachable from navigation (`nodestats` nav entry).
- **PR:** #277 (issue #276).

### 5.6 — Universal trackball back-swipe — M
- **Upstream ref:** MISSING_FEATURES → "Universal trackball back-swipe". Also KNOWN_ISSUES. Extract the swipe handler from `chat_screen.cpp` into `navigation.cpp`; apply to all screens; resolve conflicts with screens that use left-swipe themselves (two-swipe-commit pattern suggested).

### 5.7 — Device admin PIN — M
- **Upstream ref:** MISSING_FEATURES → "Device admin password / PIN". Optional hashed PIN in NVS; prompt on Settings/Terminal entry with a grace period.

### 5.8 — ACL / contact permissions — L
- **Upstream ref:** MISSING_FEATURES → "ACL / contact permissions". `perm` field on contacts; promote in contact detail; gate sensitive actions. Levels in [`src/helpers/ClientACL.h`](https://github.com/meshcore-dev/MeshCore/blob/main/src/helpers/ClientACL.h).

### 5.9 — Message signing — S (niche)
- **Upstream ref:** MISSING_FEATURES → "Message signing". Port the streaming sign API; Terminal command. Low priority for a handheld.

### 5.10 — OTA firmware update — L
- **Upstream ref:** MISSING_FEATURES → "OTA firmware update". OTA partition layout in `platformio.ini`; WiFi or BLE download (neither initialised today); UI progress. Transfer uses ESP-IDF `esp_ota_ops.h`. Biggest single item — do last.

---

## Phase 6 — Companion parity gaps (audit 2026-06-01)

> **Status: open.** New companion-firmware deltas found auditing SigurdOS against `lib/meshcore/examples/companion_radio/` (`FIRMWARE_VER_CODE 12` / v1.15.0). Every item here is **companion-relevant** — infrastructure-only deltas (region / flood-scope routing, allowed-repeat-freq, path-hash-mode, BLE-modem) are deliberately excluded, as are the transport/handshake-only codes (`CMD_APP_START`, `CMD_DEVICE_QEURY`, `CMD_SYNC_NEXT_MESSAGE`) that only exist for a phone-tethered modem. These are not yet catalogued in the protected [`MISSING_FEATURES.md`](MISSING_FEATURES.md), so they cite upstream companion source directly; fold them into that file in a separate protected-file PR. Do them as independent small PRs.

### 6.1 — Multi-ACK reliability toggle — S
- **What:** The companion can send extra redundant ACK transmissions for direct messages to improve delivery on lossy links. SigurdOS does not override the hook, so it always sends the minimum, and there is no setting.
- **What's needed:** Add `multi_acks` (0/1) to `NodePrefs`; override `getExtraAckTransmitCount()` in `SigurdMeshV2` to return it; add a Settings toggle.
- **Upstream ref:** [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `getExtraAckTransmitCount()` returns `_prefs.multi_acks`; `CMD_SET_OTHER_PARAMS` (38) sets it. [`NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `multi_acks`.

### 6.2 — Message-arrival notification (buzzer) + quiet toggle — M
- **What:** The companion beeps the buzzer on message arrival and exposes a `buzzer_quiet` mute. SigurdOS defines `PIN_BUZZER` (GPIO 46) in `tdeck_pins.h` but never drives it — there is no audible notification at all, and no mute setting.
- **What's needed:** A small buzzer HAL (active-low on GPIO 46) + mock + test; beep on incoming DM/channel message; a `buzzer_quiet` pref and a Settings "Notification sound ON/OFF" toggle.
- **Upstream ref:** [`NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `buzzer_quiet`; companion UI calls `buzzer.quiet(_node_prefs->buzzer_quiet)`.

### 6.3 — Client-repeat mode (companion also relays) — M
- **What:** The companion can optionally relay/forward packets while remaining a chat node (`client_repeat`). SigurdOS has no packet-forwarding path and no toggle; the `advert_type` pref exists in `NodePrefs` but has no UI. (Full node-type-as-repeater is infrastructure — out of scope; this is only the companion's opportunistic-relay flag.)
- **What's needed:** Add `client_repeat` to `NodePrefs`; gate forwarding on it in `SigurdMeshV2`; an "Advanced" Settings toggle. Relaying raises airtime — respect the duty-cycle budget.
- **Upstream ref:** MISSING_FEATURES → "Node type selection" (broader, infra-leaning context). [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — forwarding gated on `_prefs.client_repeat != 0`. [`NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h) — `client_repeat`.

### 6.4 — Answer inbound telemetry + telemetry-mode policy — M
- **What:** Completes the answer side of **4.3**. The companion replies to an inbound `REQ_TYPE_GET_TELEMETRY_DATA` with its own CayenneLPP telemetry (battery, optionally location/environment), gated by per-category policy (`telemetry_mode_base`/`_loc`/`_env`: deny / allow-by-flags / allow-all). SigurdOS's `onContactRequest()` is a stub returning `0`.
- **What's needed:** Implement `SigurdMeshV2::onContactRequest()` to build a CayenneLPP reply with `addVoltage(TELEM_CHANNEL_SELF, battery)`; add `telemetry_mode_*` prefs + a Settings policy control.
- **Upstream ref:** MISSING_FEATURES → "Telemetry queries (remote + self, CayenneLPP)" (answer-side bullet). [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `onContactRequest()` answers `REQ_TYPE_GET_TELEMETRY_DATA`; `TELEM_MODE_DENY/ALLOW_FLAGS/ALLOW_ALL` in [`NodePrefs.h`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/NodePrefs.h).

### 6.5 — Advert path query (diagnostic) — S *(niche)*
- **What:** Report the network path an advert took to reach this node (per-pubkey advert-path table). A useful diagnostic; SigurdOS keeps no advert-path table and exposes nothing.
- **What's needed:** Track recent advert paths keyed by pubkey prefix; surface the path for a contact on Contact Detail or the Signal/Trace screen.
- **Upstream ref:** [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_GET_ADVERT_PATH` (42) / `RESP_CODE_ADVERT_PATH`; the `advert_paths[]` table.

### 6.6 — Storage usage display — S *(niche)*
- **What:** The companion reports filesystem storage used/total alongside battery. SigurdOS shows battery but never surfaces flash/SD usage — `hal/sdcard.cpp` already computes SD capacity/free, so the data exists; only the display is missing.
- **What's needed:** Add used/total (SPIFFS + SD) to the Node Stats panel (5.5) or a storage line on Settings → System.
- **Upstream ref:** [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_GET_BATT_AND_STORAGE` (20) / `RESP_CODE_BATT_AND_STORAGE` (battery mV + used/total KB).

### 6.7 — Reboot action — S *(low value)*
- **What:** A plain "Reboot". Largely covered already — Radio Setup has "Save & Reboot", Factory Reset reboots, and `tdeck_board.h` has `reboot()`. Listed for completeness against `CMD_REBOOT`.
- **What's needed:** Optionally a dedicated "Reboot" button on Settings → System next to Factory Reset (flush pending saves, ~100 ms delay, `esp_restart()`).
- **Upstream ref:** [`examples/companion_radio/MyMesh.cpp`](https://github.com/meshcore-dev/MeshCore/blob/main/examples/companion_radio/MyMesh.cpp) — `CMD_REBOOT` (19, guarded by a literal `"reboot"` payload).

---

## Suggested sequence

```
| Phase 1 (quick wins) ──── ✅ COMPLETED
|        │
|        ▼
| Phase 0  migrate to BaseChatMesh ──── ✅ COMPLETED
|        │
|        ├──────────────► Phase 4 (4.1–4.8) ──── ✅ COMPLETED
|        │                        │            (4.9–4.10 declined)
| Phase 2 (radio/config) ── ✅ COMPLETED
|        │                        │
|        └────────► Phase 3 (messaging polish) ── ✅ COMPLETED
|                               │
|                               ▼
|                           Phase 5 (identity/UI/security/OTA)
|                               │
|                               ▼
|                           Phase 6 (companion parity gaps)
```

- **Phases 0–4 ✅** are complete (4.3 has an outstanding answer-side, tracked as 6.4).
- **Phase 5 & 6** are the remaining work. Items within them are independent unless a dependency is noted — do them as separate small PRs.

## Final reminders for the agent

- **One feature, one PR, one issue.** Don't bundle.
- **Add tests for every change.** A PR with no new/updated test and no green `pio test -e native_test` is rejected.
- **Declare hardware testing in the PR body** — "Remote test", "Physical hardware test", or both. Missing = auto-decline.
- **Don't hardcode colors or skip `apply_dark_bg()`** — theme compliance is enforced.
- **If you find a bug unrelated to your task**, add it to [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md) in the standard format — don't silently fix or ignore it.
- **When in doubt about the Phase 0 migration, stop and ask the user.** Guessing on an embedded refactor is how regressions ship.
