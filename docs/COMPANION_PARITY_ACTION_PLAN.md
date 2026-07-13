# SigurdOS-TDeck vs MeshCore Companion Nodes — Gap Analysis & Action Plan

**Date:** 2026-07-12  
**Repo:** `hermes-gadget/SigurdOS-tdeck`  
**Branch audited:** `dev` (post-RC6 fix commits through `7c91dd6`)  
**MeshCore submodule:** `60ea4a91bf14363e837037a79ce1bff7fa37483f` (companion family ~v1.15.0 + patches)  
**Primary references:** `examples/companion_radio/MyMesh.{h,cpp}`, `src/helpers/BaseChatMesh.*`, `src/comms/companion_bridge.*`, `src/mesh/sigurd_mesh_v2.*`, `docs/MISSING_FEATURES.md`, `docs/COMPANION_SUPPORT.md`, `docs/ROADMAP.md`, `RC6.md`, `audit.md`

This document is the **actionable worklist** for bringing SigurdOS-TDeck to durable parity with MeshCore **companion-radio** behaviour (protocol + field workflows), while preserving the project identity as a standalone handheld with its own UI — not a dumb modem.

It does **not** re-plan declined infrastructure roles (repeater deny-flood gating, room-server role, sensor role as primary identity).

---

## 1. How to read this document

| Symbol | Meaning |
|--------|---------|
| **P0** | Release / reliability blocker or user-visible breakage |
| **P1** | Companion parity gap that breaks official-app or field workflows |
| **P2** | Hardening / polish that exceeds stock companion or prevents regressions |
| **P3** | Nice-to-have or deferred security-sensitive families |
| **S / M / L** | Effort: small / medium / large |
| **HW** | Requires physical T-Deck (+ often a second MeshCore node or phone app) |
| **Native** | Can be proven with `pio test -e native_test` (and sanitizer) |

**Workflow rule (repo):** issue-first → branch → PR → green CI → Codex review for agent PRs → hardware verification when the change touches T-Deck behaviour → squash merge to `dev`.

**Architectural rule:** MeshCore is consumed as a **submodule** (`lib/meshcore/`). Do not fork protocol behaviour in the submodule for SigurdOS features; implement host-side behaviour in `src/mesh/` + `src/comms/` + UI.

---

## 2. Current baseline (what we already have)

SigurdOS is already a **full companion-class node with a real UI**, not an empty shell.

### 2.1 Mesh / protocol (BaseChatMesh path)

| Capability | Status | Where |
|------------|--------|-------|
| `SigurdMeshV2 : BaseChatMesh` | Done | `src/mesh/sigurd_mesh_v2.*` |
| DMs + channel text, ACKs, paths | Done | mesh + chat UI |
| Contacts (discovery, import/export, paths, lastmod) | Done | mesh + contact store |
| Channels get/set, channel data | Done | mesh + channels UI |
| Adverts (name, lat/lon policy, self advert) | Done | mesh + advertise UI |
| Regions / flood scope stamping | Done | `regions.*`, `sendFloodScoped` |
| Path hash mode | Done | prefs + companion CMD |
| Client-repeat + multi-ACK prefs | Done | prefs + mesh overrides |
| Login / keep-alive / CLI command data | Done | `sendLogin*`, `sendCommandData*`, repeaters UI |
| Status / telemetry request paths | Done | mesh + companion bridge |
| Trace path + path discovery | Done | mesh + UI |
| Signing + private key import/export | Done | companion + terminal |
| Companion BLE/USB bridge | Done (experimental until HW app validation) | `companion_bridge.*`, `companion_adapter.*` |
| Offline queue seeded from message store | Done | bridge + `/companion_msgs` |
| Message metadata for companion V3 frames | Done | `StoredMessage` (`txt_type`, `path_len`, RSSI/SNR, `companion_sent`) |

### 2.2 Companion command matrix (vs stock `MyMesh`)

Bridge enum covers **58 command IDs** (1–65 with upstream gaps).

**Explicitly unsupported today** (recognized, return `ERR_CODE_UNSUPPORTED_CMD`) — see `docs/COMPANION_SUPPORT.md`:

| CMD | ID | Stock companion does | SigurdOS today |
|-----|----|----------------------|----------------|
| `CMD_SEND_RAW_PACKET` | 65 | Inject arbitrary parsed packet | Unsupported |

Everything else used for normal app setup / contacts / channels / messaging / offline sync / status / telemetry / trace / signing / config is implemented in the bridge host path.

### 2.3 Product identity constraints (do not regress)

From project identity and `MISSING_FEATURES.md`:

| Item | Decision |
|------|----------|
| Multipart messages (`PAYLOAD_TYPE_MULTIPART`) | **NOT DOING** — keep ~150-byte send cap |
| General raw custom app dispatch (`PAYLOAD_TYPE_RAW_CUSTOM` as open API) | **NOT DOING** (internal PING/PONG is fine) |
| Temporary radio config with auto-revert UI | **NOT DOING** (live-apply plumbing may exist; no Try/timer UX) |
| Repeater `RegionMap` deny-flood gating | **NOT DOING** — handheld does not re-flood for others |
| Turning T-Deck into dedicated repeater/room/sensor product | **NOT DOING** — companion/chat role only |

---

## 3. Gap summary (what is still missing or incomplete)

Gaps fall into **three buckets**:

1. **Stateful dual-UI reconciliation** (phone app + LVGL UI sharing one identity).
2. **Field UX parity on-device** (what a phone+companion pair does, but on the T-Deck alone).
3. **Validation debt** (official app + RF interop + soak not yet release-grade evidence).

```
                    ┌─────────────────────────────┐
                    │  Official MeshCore phone app │
                    └──────────────┬──────────────┘
                                   │ BLE/USB companion protocol
                    ┌──────────────▼──────────────┐
                    │ CompanionBridge + Adapter    │  ← raw packet deliberately refused
                    │ offline queue + V3 frames    │  ← app validation incomplete
                    └──────────────┬──────────────┘
                                   │
          ┌────────────────────────┼────────────────────────┐
          ▼                        ▼                        ▼
   Message store            SigurdMeshV2              LVGL UI
   /companion_msgs          BaseChatMesh              chat/contacts/
   (shared source)          login/CLI/scope           map/repeaters
                                                     (detail UX gaps)
```

---

## 4. Priority action list

### P1 — Companion protocol & official-app parity

#### P1-1. Official MeshCore app hardware validation matrix

**Why:** Code coverage ≠ app compatibility. Handshake, contact incremental sync, offline drain, channel set, login/status, and reconnect are the real product gate for “works with MeshCore companion nodes/apps”.

**Required scenarios (each BLE and USB if both ship):**

| # | Scenario | Pass criteria |
|---|----------|---------------|
| A | Pair with PIN + MITM bond | App connects; DEVICE_QUERY + APP_START succeed |
| B | Reconnect after kill-app / range drop | Cached bond reconnect; no stuck offline queue |
| C | Contact sync `since` high-water | Local rename/favourite appears after `GET_CONTACTS` |
| D | DM send app→RF→peer and peer→RF→app | History on T-Deck + app; ACK/`SEND_CONFIRMED` |
| E | Channel join/send/receive | Channel list + messages consistent |
| F | Offline: receive while app disconnected, then `SYNC_NEXT_MESSAGE` | Messages delivered once; **T-Deck store intact** |
| G | Time set from app | RTC updated; Settings shows source=app |
| H | Repeater login + CLI from app | No hang/timeout; CLI replies not mislabeled as chat |
| I | Identity export/import (explicit user action) | Keys round-trip; sessions cleared |
| J | Malformed / oversized frames | `ERR_*` only; no crash/heap corruption |

**Implementation plan:**
1. Create a checklist issue + scriptable serial capture notes (no PII/device IDs in logs).
2. Capture golden frames from a stock companion radio for any failing RESP layouts.
3. Fix failures in bridge/adapter only; add native golden-frame tests before retest.

**Effort:** M (process) + fix-dependent · **HW:** mandatory

---

#### P1-5. `CMD_SEND_RAW_PACKET` (65) — **defer / default refuse**

**Risk:** Arbitrary packet injection is the highest abuse surface.

**Plan unless a concrete MeshCore app feature requires it:**
- Keep `ERR_CODE_UNSUPPORTED_CMD`.
- Document in `COMPANION_SUPPORT.md`.
- Only enable behind compile flag + device PIN confirmation if ever needed for lab tools.

**Effort:** L if enabled · **Policy:** default no

---

#### P1-7. Regions interop proof (companion flood scope)

**Code status:** implemented. **Field status:** still needs hardware proof.

**Actions:**
1. Scoped send vs unscoped send against a region-aware repeater.
2. `$` private key add/persist/reload.
3. Active region chip visible in chat/settings.
4. Companion CMDs `SET/GET_DEFAULT_FLOOD_SCOPE` + `SET_FLOOD_SCOPE_KEY` exercised from app/tests.
5. Confirm adverts remain unscoped (discovery must cross regions).

**Effort:** S–M (mostly HW) · **Native:** golden transport codes already partially covered

---

### P2 — Standalone field UX parity (exceed phone+modem)

A stock companion radio is a modem; the **phone** supplies UX. SigurdOS must supply that UX on-device.

#### P2-5. Map parity extras (optional vs companion)

Companion nodes don’t require maps; this **exceeds** companion. Keep offline-first.

| Item | Priority |
|------|----------|
| Online tile fetch over WiFi | P3 (battery/privacy) |

**Effort:** M–L

---

### P3 — Explicitly deferred or policy-gated

| Item | Why deferred |
|------|--------------|
| `CMD_SEND_RAW_PACKET` | Packet injection risk |
| Multipart messages | Product decision |
| Temp radio auto-revert UI | Product decision |
| WiFi companion bridge / WebUI | Optional local transport; security policy required first |
| Full NimBLE migration | Size/RAM win but rewrite of `SerialBLEInterface` |
| Device-authored PUSH extension | Needs cooperating client |

---

## 5. Proposed implementation program (phases)

Each phase should land as **multiple small PRs**, each with its own issue. Do not combine store rewrite + protocol + UI polish in one PR.

### Phase B — Official app & store foundation (2–4 weeks)

| # | Work item | Priority | Effort | Exit criteria |
|---|-----------|----------|--------|---------------|
| B1 | Official app validation matrix A–J; file bugs per failure | P1 | M | Written results attached to issue |
| B6 | Regions HW interop | P1 | S–M | Scoped vs unscoped proof |

**Deliverable:** phone app is a supported second client; T-Deck history is single-source-of-truth.

---

### Phase C — Preserve the protocol safety boundary

| # | Work item | Priority | Effort | Exit criteria |
|---|-----------|----------|--------|---------------|
| C1 | Keep `CMD_SEND_RAW_PACKET` unsupported unless lab flag | P3 | S | Explicit policy |

**Deliverable:** raw packet injection remains an explicit, documented no.

---

## 6. File-by-file touch map (for implementers)

### Protocol / mesh

| Area | Files |
|------|-------|
| Bridge dispatcher | `src/comms/companion_bridge.{h,cpp}` |
| Host adapters | `src/mesh/companion_adapter.{h,cpp}` |
| Mesh behaviour | `src/mesh/sigurd_mesh_v2.{h,cpp}` |
| UI-facing API | `src/mesh/mesh_wrapper.{h,cpp}` |
| Message durability | `src/mesh/message_store.{h,cpp}` |
| Legacy chat durability | `src/ui/chat_history_store.{h,cpp}` (migrate away) |
| Regions | `src/mesh/regions.{h,cpp}`, prefs `active_region` / scope key |
| Contacts persistence | `src/mesh/contact_store.*`, `persistence_store.*` |

### UI

| Area | Files |
|------|-------|
| Chat | `src/ui/chat_screen.*`, history checkpoint |
| Repeaters/rooms | `src/ui/screens/screen_repeaters.cpp` |
| Contacts | `src/ui/screens/screen_contacts.cpp` |
| Bluetooth | `src/ui/screens/screen_bluetooth.cpp` |
| Regions | `src/ui/screens/screen_regions.cpp` |
| Navigation/lifetime | `navigation.*`, `screen_lifetime.*`, `screens_common.*` |

### HAL / power

| Area | Files |
|------|-------|
| Prefs | `src/hal/prefs.{h,cpp}` |
| Sleep | `src/hal/tdeck_board.h` |
| Keyboard contract | `src/hal/keyboard.cpp` |
| BLE transport | MeshCore `SerialBLEInterface` + `observed_ble_interface.*` |

### Docs to keep in sync on every phase

- `docs/COMPANION_SUPPORT.md` — command matrix truth
- `docs/MISSING_FEATURES.md` — only remaining missing/declined (avoid stale “all done”)
- `docs/ROADMAP.md` — phase status
- `docs/KNOWN_ISSUES.md` — hardware validation leftovers
- `docs/FEATURES_OVERVIEW.md` — user-facing feature index
- This file — check off items or link issues

---

## 7. Testing strategy (non-negotiable)

### Native (every PR)

```bash
pio test -e native_test -v
# for storage/protocol:
pio test -e native_sanitize -f test_message_store -v
pio test -e native_sanitize -f test_companion_protocol -v
pio test -e native_sanitize -f test_contact_store -v
pio run -e SigurdOS_TDeck
pio run -e SigurdOS_TDeck_ble_validation   # when BLE touched
```

### Hardware gates (before claiming companion parity)

1. **Standalone T-Deck:** boot, type, touch, trackball, chat send/recv with second MeshCore node.
2. **Official app:** matrix §P1-1.
3. **Room/repeater:** login, CLI, fetch, logout, identity swap.
4. **Soak:** ≥10 min idle + active navigation; heap/PSRAM variance recorded.
5. **OTA/Launcher:** only with versioned URLs and recovery path known.

### Interop peer definition

Use at least one **stock MeshCore companion radio** or **simple_secure_chat** node built from the same protocol generation as the submodule pin. Record firmware versions, not device identifiers.

---

## 8. Suggested issue breakdown (copy/paste)

Create one GitHub issue per row (do not batch into mega-issues):

1. `parity: official MeshCore app BLE/USB validation matrix`
2. `parity: regions scoped-flood hardware interop proof`

---

## 9. Definition of “companion parity achieved”

Parity is **not** “implements every MyMesh line.” It is:

1. **RF peer parity:** adverts, contacts, DM, channels, ACKs, paths, trace, regions-as-companion, login/CLI with stock MeshCore nodes.
2. **Host protocol parity:** official app can set up, message, sync offline queue, manage contacts/channels, set time/radio, login to infrastructure — without hangs or silent data loss.
3. **Dual-client safety:** T-Deck UI + app never destroy each other’s history; identity changes reset sessions.
4. **Documented refusals:** raw packet injection, multipart, temp-radio toy mode, repeater-only features remain explicit nos.
5. **Evidence:** native tests + physical matrix + interop notes attached to the release.

**Exceeds parity** when on-device UX (maps, message detail, telemetry history, launcher/OTA recovery, structured diagnostics) is better than phone+dumb-modem for field use.

---

## 10. Immediate next moves (recommended order)

1. **Run the official app matrix** on current BLE/USB builds and attach results to the validation issue.

---

## 11. Related documents

| Doc | Role |
|-----|------|
| `docs/COMPANION_SUPPORT.md` | Live command support matrix |
| `docs/MISSING_FEATURES.md` | Declined items + historical feature plans |
| `docs/ROADMAP.md` | Broader product phases |
| `docs/FEATURES_OVERVIEW.md` | Implemented feature index |
| `docs/KNOWN_ISSUES.md` | Open hardware/validation gaps |
| `RC6.md` | Latest release-readiness audit |
| `audit.md` | Static defect audit (GPS, UAF, sleep, perf) |
| `lib/meshcore/examples/companion_radio/MyMesh.cpp` | Stock companion behaviour reference |
| `lib/meshcore/docs/companion_protocol.md` | Host protocol narrative (may lag code) |

---

*This plan is based on source review of SigurdOS-TDeck and the pinned MeshCore companion_radio implementation. Hardware interop outcomes must still be written back into this file (or linked issues) as evidence lands.*
