# Mesh Networking

**SigurdOS-TDeck's mesh networking layer — architecture, protocol integration, and feature reference.**

The firmware implements a full MeshCore protocol stack on the LilyGo T-Deck (ESP32-S3 + SX1262 LoRa radio). The mesh layer is built around `SigurdMeshV2`, a `BaseChatMesh` subclass, with a clean wrapper API (`mesh_wrapper.h/cpp`) that the LVGL UI and terminal interface consume.

---

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [Class Hierarchy & Initialisation](#class-hierarchy--initialisation)
  - [Init Sequence](#init-sequence)
  - [Init Order Dependency with SPI Bus](#init-order-dependency-with-spi-bus)
- [SigurdMeshV2 — Core Mesh Subclass](#sigurdmeshv2--core-mesh-subclass)
  - [Dependency Injection](#dependency-injection)
  - [Virtual Overrides](#virtual-overrides)
- [Channel-Based Messaging](#channel-based-messaging)
  - [Hashtag Channels](#hashtag-channels)
  - [PSK Channels](#psk-channels)
  - [Channel Limits](#channel-limits)
  - [Channel Hash Lookup](#channel-hash-lookup)
- [Direct Messages (DMs)](#direct-messages-dms)
  - [Send Path](#send-path)
  - [Receive Path](#receive-path)
  - [Path-Aware Routing](#path-aware-routing)
- [Contact Discovery & Management](#contact-discovery--management)
  - [Advert Broadcasting](#advert-broadcasting)
  - [10-Second Cooldown](#10-second-cooldown)
  - [Contact List (64-entry LRU)](#contact-list-64-entry-lru)
  - [Anonymous Data Reception](#anonymous-data-reception)
- [Ping Nearby (Zero-Hop Discovery)](#ping-nearby-zero-hop-discovery)
  - [Protocol](#protocol)
  - [3-Second Collection Window](#3-second-collection-window)
  - [30-Second Cooldown](#30-second-cooldown)
- [Path Learning & Route Discovery](#path-learning--route-discovery)
  - [SimpleMeshTables](#simplemeshtables)
  - [Peer Path Callbacks](#peer-path-callbacks)
  - [Flood Path Callbacks](#flood-path-callbacks)
- [Trace Route](#trace-route)
- [Packet Logging (Heard Screen)](#packet-logging-heard-screen)
- [Signal Strength Display (Network Screen)](#signal-strength-display-network-screen)
- [Persistence & State](#persistence--state)
  - [Identity Persistence](#identity-persistence)
  - [Channel Persistence (NVS)](#channel-persistence-nvs)
  - [Chat History Persistence](#chat-history-persistence)
- [RTC & System Time](#rtc--system-time)
- [Radio Configuration](#radio-configuration)
- [Message Queue](#message-queue)
- [Source Files](#source-files)
- [Related Documents](#related-documents)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                       LVGL UI                            │
│   Chat, Contacts, Finder, Packets, Trace, Signal, Map    │
└────────────────────┬────────────────────────────────────┘
                     │ calls
┌────────────────────▼────────────────────────────────────┐
│                 mesh_wrapper.h/cpp                        │
│   Public API layer: init, send, poll, contacts, channels  │
│   Message queue, identity persistence, NVS channel store   │
└────────────────────┬────────────────────────────────────┘
                     │ owns
┌────────────────────▼────────────────────────────────────┐
│                    SigurdMeshV2 (sigurd_mesh_v2.h)                 │
│   BaseChatMesh subclass: routing, channels, crypto,       │
│   contacts, requests/responses, trace, control packets    │
└────────────────────┬────────────────────────────────────┘
                     │ inherits
┌────────────────────▼────────────────────────────────────┐
│                  MeshCore (lib/meshcore/)                  │
│   Protocol core: Mesh, PacketManager, Dispatcher,         │
│   Radio wrappers, encryption, routing tables              │
└────────────────────┬────────────────────────────────────┘
                     │ drives
┌────────────────────▼────────────────────────────────────┐
│              RadioLib SX1262 (hardware)                    │
└─────────────────────────────────────────────────────────┘
```

---

## Class Hierarchy & Initialisation

### Init Sequence

The mesh subsystem is initialised during `setup()` in `src/main.cpp`. The boot order is:

```
Serial → board.begin() → battery → SPIFFS → GPS → display → mesh → UI → debug → SD card
```

The mesh initialisation call chain:

```
main.cpp
  └─ sigurdos::mesh::init(spiffs_ok)          [mesh_wrapper.cpp:186]
       ├─ fallback_clock.begin()
       ├─ rtc_clock.begin(Wire)
       ├─ Read NodePrefs (freq, bw, sf, cr, tx_power)
       ├─ Hard-reset SX1262 via RST pin      [line 212-216]
       │    ├─ RST LOW for 100µs
       │    ├─ RST HIGH then 10ms wait (TCXO stabilization)
       ├─ sigurdos_shared_spi_begin(SCK, MISO, MOSI) [line 221]
       ├─ radio_module.std_init(&sigurdos_shared_spi())  [line 225]
       ├─ radio_module.setFrequency(freq)
       ├─ radio_module.setBandwidth(bw)
       ├─ radio_module.setSpreadingFactor(sf)
       ├─ radio_module.setCodingRate(cr)
       ├─ radio_module.setOutputPower(tx_power)
       ├─ fast_rng.begin(radio_module.random(...))
       ├─ new SigurdMeshV2(...)                   [line 242]
       │    └─ SigurdMeshV2::SigurdMeshV2(...)       [sigurd_mesh_v2.h:421-431]
       │         ├─ _own_name[0] = '\0'
       │         ├─ _prefs.set_defaults()
       │         └─ All contacts out_path = OUT_PATH_UNKNOWN
       ├─ g_mesh->setMessageCallback(onMeshMessage)  [line 247]
       ├─ g_mesh->setOwnName(own_name)       [line 248]
       ├─ Load or generate identity           [line 251-258]
       │    ├─ loadIdentity(g_mesh->self_id)  → SPIFFS /mesh_id
       │    └─ or generate LocalIdentity(&fast_rng) and save
       ├─ g_mesh->begin()                     [line 260]
       ├─ loadChannels()                      [line 263]  (restore from NVS)
       └─ broadcastAdvert(own_name)           [line 269]  (only if prefs configured)
```

### Init Order Dependency with SPI Bus

The LoRa radio, display (ST7789), and microSD card all share the **same SPI bus** (SPI2_HOST) on these pins:

| Signal | GPIO Pin |
|--------|----------|
| SCK    | 40       |
| MOSI   | 41       |
| MISO   | 38       |
| LoRa CS  | 9     |
| Display CS | 12  |
| SD CS   | 39      |

SPI is **not** initialised globally with a single `SPI.begin()`. Each driver initialises the bus independently from its own entry point:

1. **Display init** (`sigurdos_display_init`) — configures SPI for the ST7789 via LovyanGFX
2. **Mesh init** (`sigurdos::mesh::init`) — calls `sigurdos_shared_spi_begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI)` for the SX1262
3. **SD card init** (`sigurdos_sdcard_init`) — SPI is already configured from step 1 or 2

The mesh init **must happen after display init** (display init is at step 5 in main.cpp, mesh at step 7). In remote test mode (`SIGURDOS_REMOTE_TEST`), `mesh::init()` still calls `sigurdos_shared_spi_begin()` so the SPI bus is available for SD card, even though the radio is not used.

> **`wifi_sta::connect()` removed.** The blocking `wifi_sta::connect()` method
> has been deprecated and removed. Only **`beginConnect()`** (non-blocking start)
> and **`getStatus()`** (poll for completion) remain. Callers must use the
> async pattern: `beginConnect(ssid, password)` → poll `getStatus()` →
> `Status::Connected` or `Status::Failed`. See `src/hal/wifi_ota.h`.

---

## SigurdMeshV2 — Core Mesh Subclass

`SigurdMeshV2` (defined in `src/mesh/sigurd_mesh_v2.h`) is a `BaseChatMesh` subclass from the MeshCore library. It overrides the BaseChatMesh hooks that integrate SigurdOS-specific behaviour: contact/channel storage, request/response handling, path learning, trace route, Ping Nearby, Node Discovery, ACK tracking, packet logging, and handheld UI bridges.

### Dependency Injection

```
SigurdMeshV2(::mesh::Radio& r,           // CustomSX1262Wrapper wrapper
         ::mesh::MillisecondClock& ms, // ArduinoMillis
         ::mesh::RNG& rng,            // StdRNG (seeded from radio)
         ::mesh::RTCClock& rtc,       // AutoDiscoverRTCClock (DS3231 or ESP32)
         ::mesh::PacketManager& mgr,  // StaticPoolPacketManager(16 slots)
         ::mesh::MeshTables& tbl)     // SimpleMeshTables
```

These are all static globals in `mesh_wrapper.cpp`:

```cpp
static CustomSX1262Wrapper       radio_driver(radio_module, board);
static ESP32RTCClock             fallback_clock;
static AutoDiscoverRTCClock      rtc_clock(fallback_clock);
static StdRNG                    fast_rng;
static SimpleMeshTables          tables;
static ArduinoMillis             millis_clock;
static StaticPoolPacketManager   pkt_mgr(16);
static sigurdos::mesh::SigurdMeshV2*   g_mesh = nullptr;
```

### Virtual Overrides

| Virtual Method | Override | Purpose |
|---------------|----------|---------|
| `searchPeersByHash(hash)` | `sigurd_mesh_v2.h:82` | Match incoming packet's public key hash against contact list; populates `_matchIdxs` |
| `getPeerSharedSecret(dest, peer_idx)` | `sigurd_mesh_v2.h:91` | Return shared secret for a matched contact index |
| `onPeerDataRecv(pkt, type, idx, secret, data, len)` | `sigurd_mesh_v2.h:97` | Handle incoming DM/REQ/RESPONSE payloads |
| `onAdvertRecv(pkt, id, timestamp, app_data, len)` | `sigurd_mesh_v2.h:124` | Discover or update contacts from advert broadcasts |
| `searchChannelsByHash(hash, out, max)` | `sigurd_mesh_v2.h:181` | Look up group channels by their 32-byte hash |
| `onGroupDataRecv(pkt, type, ch, data, len)` | `sigurd_mesh_v2.h:209` | Handle incoming group channel messages |
| `onAnonDataRecv(pkt, secret, sender, data, len)` | `sigurd_mesh_v2.h:242` | Handle anonymous (no prior advert) messages |
| `onPeerPathRecv(pkt, idx, secret, path, len, extra_type, extra, extra_len)` | `sigurd_mesh_v2.h:258` | Learn a direct path from a peer response |
| `onPathRecv(pkt, sender, path, len, extra_type, extra, extra_len)` | `sigurd_mesh_v2.h:272` | Learn a path from a flood-routed path packet |
| `onTraceRecv(pkt, tag, auth, flags, snrs, hashes, len)` | `sigurd_mesh_v2.h:307` | Store trace route result |
| `onControlDataRecv(pkt)` | `sigurd_mesh_v2.h:324` | Handle PING/PONG control packets |
| `onRawDataRecv(pkt)` | `sigurd_mesh_v2.h:393` | Stub — reserved for future use |
| `logRx(pkt, ...)` | `sigurd_mesh_v2.h:398` | Log every received packet to the circular packet log |

### ACK Tracking

SigurdMeshV2 maintains a `_pending_acks[]` array (`MAX_PENDING_ACKS = 16`) to
match incoming ACK packets against recently sent messages. Each `PendingAck`
struct carries **in-class default initializers** so the array is fully zeroed
on construction — without them, `dest_name`, `timestamp`, `expected_ack`, and
`sent_at_ms` would hold garbage until first write, which is fragile for ACK
matching:

```cpp
struct PendingAck {
    char     dest_name[32]  = {};
    uint32_t timestamp      = 0;
    uint32_t expected_ack   = 0;
    uint32_t sent_at_ms     = 0;
    bool     in_use         = false;
};
```

`addPendingAck(name, ts, expected_ack)` registers an entry; `processAck(data)`
(from `BaseChatMesh`) walks the array and returns the matching `ContactInfo*`
or `nullptr`.

---

## Channel-Based Messaging

Group channels allow any number of nodes to communicate on a shared, encrypted conversation. All nodes that know the channel's secret can read and write.

### Hashtag Channels

Hashtag channels derive their encryption key deterministically from the channel name. The name is SHA-256 hashed to produce a 16-byte key (via `CIPHER_KEY_SIZE`), which is then SHA-256 hashed again to produce the 32-byte channel hash.

```
channel_name → SHA-256 → channel_secret (16 bytes)
channel_secret → SHA-256 → channel_hash (32 bytes)
```

The `addHashtagChannel()` method (`sigurd_mesh_v2.h:556`) normalises the input:
- Trims leading whitespace
- Prepends `#` if absent
- Strips trailing whitespace/newlines
- Rejects names longer than 31 characters or empty after `#`
- Deduplicates against existing channels

### PSK Channels

PSK (Pre-Shared Key) channels are added with `addChannel(name, psk_base64)` (`sigurd_mesh_v2.h:522`). The PSK is provided as a Base64-encoded string (either 16 or 32 bytes after decode). The channel hash is the SHA-256 of the decoded PSK.

The default Public channel PSK is `izOH6cXN6mrJ5e26oRXNcg==` (16 bytes after decode), joined via `joinPublicChannel()`.

### Channel Limits

- **Maximum channels: 8** (`MAX_GROUP_CHANNELS`, from `platformio.ini`)
- **Minimum channel name length:** 2 characters (including `#`)
- **Maximum channel name length:** 31 characters

Channels are stored in a fixed-size `ChannelDetails[8]` array (from BaseChatMesh). There is no eviction policy — if the channel list is full, `addChannel()` and `addHashtagChannel()` return `false`.

### Channel Hash Lookup

When a group message arrives over the radio, MeshCore calls `searchChannelsByHash(hash, out, max)` (`sigurd_mesh_v2.h:181`). The implementation does a linear scan over the `_channels` array, comparing the 32-byte `channel.hash` field using `memcmp`. Returns all matching channels (typically one).

### Sending Group Messages

`sendGroupText(channel_idx, text)` (`sigurd_mesh_v2.h:597`) builds a payload in the **BaseChatMesh-compatible wire format**:

```
[4-byte LE RTC timestamp][1-byte text_type=0]["<sender_name>: <message>\0"]
```

- Payload is capped at 150 bytes of total content (5 byte header + up to 150 byte body)
- UTF-8 safe truncation via `sigurdos::utf8_truncate_bytes()`
- Messages are flood-routed (`sendFlood()`)
- The prefix `<sender_name>: ` matches what `onGroupDataRecv` expects to parse

### Receiving Group Messages

`onGroupDataRecv` (`sigurd_mesh_v2.h:209`) processes `PAYLOAD_TYPE_GRP_TXT` packets:
1. Validates payload length > 5 bytes
2. Null-terminates the data
3. Skips the 5-byte header (timestamp + text_type)
4. Looks up the channel name by matching channel hash
5. Parses the `"<sender_name>: <message>"` format via `parse_group_sender()`
6. Forwards to the message callback: `_onMessage(sender, channel, text)`

---

## Direct Messages (DMs)

Direct messages are peer-to-peer encrypted messages sent from one specific node to another.

### Send Path

`sendTextTo(dest_name, text)` (`sigurd_mesh_v2.h:450`):

1. Linear scan contacts for matching `dest_name`
2. Build payload: `[4-byte LE timestamp][1-byte flags=0][null-terminated text]`
3. Create encrypted datagram via `createDatagram(PAYLOAD_TYPE_TXT_MSG, ...)`
4. If the contact has a known direct path (`out_path_len != OUT_PATH_UNKNOWN`), send via `sendDirect()`; otherwise fall back to `sendFlood()`
5. Caps text at 150 bytes (with 5-byte header + 1-byte null, total plaintext ≤ 156 bytes fits within MeshCore's encrypted payload limit of `MAX_PACKET_PAYLOAD - 16 = 168`)

### Receive Path

`onPeerDataRecv(pkt, type, sender_idx, secret, data, len)` (`sigurd_mesh_v2.h:97`):

1. Only accepts `PAYLOAD_TYPE_TXT_MSG`, `PAYLOAD_TYPE_REQ`, and `PAYLOAD_TYPE_RESPONSE`
2. Skips the 5-byte header (timestamp + flags) for TXT_MSG, or uses raw data for REQ/RESPONSE
3. Looks up the sender's name from `_matchIdxs[sender_idx]`
4. Forwards to `_onMessage(sender, "", text)` (empty channel string indicates DM)

### Path-Aware Routing

When sending a DM, the system checks if a direct path has been learned for the destination contact:
- **Direct path known:** sends point-to-point via `sendDirect()`
- **No path known:** floods the message across the network via `sendFlood()`

Paths are learned via the `onPeerPathRecv` and `onPathRecv` callbacks (see [Path Learning](#path-learning--route-discovery)).

---

## Contact Discovery & Management

### Advert Broadcasting

Nodes announce their presence by broadcasting **advert packets** (flood-routed, TTL-limited). Each advert carries:

- **Name** — the node's human-readable name (max 31 chars)
- **Type** — always `ADV_TYPE_CHAT` (1) currently
- **Optional GPS coordinates** — latitude and longitude if a GPS fix is available

`s broadcastAdvert(name)` and `broadcastAdvert(name, lat, lon)` (`sigurd_mesh_v2.h:480-494`) build the advert using MeshCore's `AdvertDataBuilder` and flood-routing.

### 10-Second Cooldown

Advert broadcasting has a **10-second cooldown** enforced in `sendAdvert()` (`mesh_wrapper.cpp:410`):

```cpp
static uint32_t last_advert_ms = 0;
if (last_advert_ms != 0 && now_ms - last_advert_ms < 10000)
    return false;  // Rate-limited
```

This cooldown is enforced at the wrapper level (not in `SigurdMeshV2`) so it protects against both UI-triggered and programmatic (Terminal command) advert calls. The UI also enforces a visual cooldown via button state.

Adverts are **only broadcast on boot** if the user has explicitly configured radio parameters via Settings → Radio Setup. Compile-time defaults do not trigger a boot advert, preventing accidental transmissions on potentially illegal frequencies.

### Contact List (350-entry, BaseChatMesh-managed)

Contacts now live in BaseChatMesh's contact table (capacity `-D MAX_CONTACTS=350` in `platformio.ini`) rather than a separate SigurdOS-side array. `SigurdMeshV2` configures the behavior through BaseChatMesh overrides (`src/mesh/sigurd_mesh_v2.h`):

1. **Auto-add** — `isAutoAddEnabled()` returns true; `shouldAutoAddContactType()` and `getAutoAddMaxHops()` (from `NodePrefs`) gate which adverts become contacts.
2. **Discovery hook** — `onDiscoveredContact(contact, is_new, path_len, path)` updates UI state and persistence when an advert is parsed.
3. **Eviction** — `shouldOverwriteWhenFull()` returns true, so a full table overwrites the oldest entry; `onContactsFull()` additionally pushes a companion notification.
4. **Persistence** — contacts are saved through the versioned contact store (`src/mesh/contact_store.cpp`, magic header + bounds checks).

Each BaseChatMesh `ContactInfo` plus SigurdOS wrapper metadata stores:

| Field | Size | Description |
|-------|------|-------------|
| `id` | `Identity` | Node's public key identity |
| `secret` | 32 bytes | Shared secret (ECDH) |
| `name` | 32 chars | Human-readable node name |
| `last_seen` | uint32_t | RTC timestamp of last advert |
| `last_rssi` | int | RSSI of last received packet (dBm) |
| `out_path_len` | uint8_t | Direct path length (`OUT_PATH_UNKNOWN=0xFF` if none) |
| `out_path` | 8 bytes | Direct routing path |

### Anonymous Data Reception

`onAnonDataRecv` (`sigurd_mesh_v2.h:242`) handles messages from nodes that have never sent an advert. The sender is identified by a generated name `"anon_<pub_key[0]>"` (first byte of public key as hex). This allows receiving messages from unknown nodes but provides no return-path — there is currently no way to send back to an anonymous node.

---

## Ping Nearby (Zero-Hop Discovery)

The Ping Nearby feature actively discovers nodes within immediate radio range (one hop) without needing prior advert exchange from those nodes.

### Protocol

The implementation (`sigurd_mesh_v2.h:642-678`, `onControlDataRecv` at line 324) uses zero-hop raw control packets:

```
1. Initiator sends:  "PING:<8-hex-tag>"       (via sendZeroHop)
2. Responder replies: "PONG:<tag>:<name>:<rssi>" (via sendZeroHop)
```

- The tag is a unique value derived from `(millis() XOR this_pointer)` to avoid collision
- Both packets have `payload[0] |= 0x80` to set the control-disco bit (required for `onControlDataRecv` dispatch)
- PONG responses include the responding node's name and the RSSI measured at the responder's radio

### 3-Second Collection Window

```cpp
static constexpr uint32_t PING_WINDOW_MS = 3000;
```

After sending a PING, the system collects PONG responses for 3 seconds. `pingIsActive()` returns `true` during this window. PONGs received outside the window are silently ignored.

Up to **32 results** (`PING_RESULTS_MAX`) can be collected in a single ping window. Each result stores:

| Field | Size | Description |
|-------|------|-------------|
| `name` | 32 chars | Node name from PONG response |
| `rssi` | int | Signal strength reported by responder |

### 30-Second Cooldown

```cpp
static constexpr uint32_t PING_COOLDOWN_MS = 30000;
```

After a ping completes (or its 3-second window expires), `pingOnCooldown()` returns `true` for 30 seconds. `pingCooldownRemaining()` returns the milliseconds until another ping can be sent. The cooldown is enforced at both the API level (`sendPingNearby()` returns `false` during cooldown) and in the UI (button state).

### Usage

The Finder screen (`src/ui/screens/screen_finder.cpp`) provides the "Ping Nearby" UI:
- Button to initiate a ping
- Shows active listening state during the 3-second window
- Displays results sorted by RSSI
- Cooldown indicator on the button

---

## Path Learning & Route Discovery

MeshCore uses a distributed routing table (`MeshTables`) to learn paths between nodes. SigurdOS uses `SimpleMeshTables` and stores learned per-contact paths for direct routing.

### SimpleMeshTables

Instantiated as a static global in `mesh_wrapper.cpp:42`:

```cpp
static SimpleMeshTables tables;
```

Passed to the `SigurdMeshV2` constructor and forwarded to `mesh::Mesh`'s constructor. `SimpleMeshTables` is the reference routing table implementation — it maintains a matrix of known paths between nodes using the MeshCore path-discovery protocol.

### Peer Path Callbacks

When a direct message or response is received from a peer, MeshCore may include a return path. `onPeerPathRecv` (`sigurd_mesh_v2.h:258`) stores this path:

```cpp
_contacts[idx].out_path_len = Packet::copyPath(_contacts[idx].out_path, path, path_len);
```

MeshCore then automatically sends a reciprocal return path back to the peer.

### Flood Path Callbacks

When a path-discovery packet arrives via flood routing, `onPathRecv` (`sigurd_mesh_v2.h:272`) matches the sender identity against the contact list and stores the path:

```cpp
_contacts[i].out_path_len = Packet::copyPath(_contacts[i].out_path, path, path_len);
```

If `path_len` is invalid, the path is cleared (`out_path_len = OUT_PATH_UNKNOWN`).

---

## Trace Route

The Trace feature sends a probe packet along a known direct path to a contact and collects per-hop SNR measurements and node hashes from the return path.

### Sending a Trace

`sendTrace(contact_idx, tag)` (`sigurd_mesh_v2.h:292`):

1. Validates contact index and that a direct path exists (`out_path_len != OUT_PATH_UNKNOWN`)
2. Creates a trace packet via `createTrace(tag, 0, 0)`
3. Encodes the path hashes from the contact's `out_path`
4. Sends directly via `sendDirect()`

The wrapper layer (`mesh_wrapper.cpp:512`) maintains a monotonic `trace_tag_counter` for unique trace identifiers.

### Receiving a Trace Result

`onTraceRecv(pkt, tag, auth_code, flags, path_snrs, path_hashes, path_len)` (`sigurd_mesh_v2.h:307`):

1. Stores the result in `_last_trace_*` fields
2. Sets `_has_trace_result = true`
3. Path is capped at `MAX_PATH_SIZE` (8 bytes)

### UI — Trace Screen

The Trace screen (`src/ui/screens/screen_trace.cpp`) presents:
- A list of contacts with known paths (marked with path indicator)
- Tapping a contact sends a trace probe
- The returned path is displayed as hop-by-hop SNR values and node hashes

---

## Packet Logging (Heard Screen)

Every received and transmitted radio packet is logged to a circular buffer, powering the **Heard** (Packets) screen.

### RX Logging (`logRx`)

`logRx` (`sigurd_mesh_v2.h:398`) is called by MeshCore's dispatcher for each incoming packet. It classifies the packet type and records RSSI and SNR:

| Payload Type | Log Label |
|-------------|-----------|
| `PAYLOAD_TYPE_ADVERT` | `ADVERT_RX` |
| `PAYLOAD_TYPE_ACK` | `ACK` |
| `PAYLOAD_TYPE_TXT_MSG` | `DM_RX` |
| `PAYLOAD_TYPE_GRP_TXT` / `PAYLOAD_TYPE_GRP_DATA` | `GRP_RX` |
| `PAYLOAD_TYPE_ANON_REQ` | `ANON_RX` |
| `PAYLOAD_TYPE_TRACE` | `TRACE` |
| Other | `PKT_RX` |

### TX Logging

The wrapper layer also logs transmitted packets:

| Operation | Log Label |
|-----------|-----------|
| DM sent | `TX_DM` |
| Channel message sent | `TX_CHAN` |
| Advert broadcast | `TX_ADV` |
| System boot | `BOOT` |

### Circular Buffer

```cpp
static constexpr int MAX_PACKET_LOG = 50;       // mesh_wrapper.cpp:146
```

Each `PacketLogEntry` stores:

| Field | Type | Description |
|-------|------|-------------|
| `timestamp` | uint32_t | RTC time of reception |
| `source` | char[32] | Node name or "RADIO" for mesh RX |
| `rssi` | int | Signal strength (dBm) |
| `snr` | float | Signal-to-noise ratio (dB) |
| `type` | char[16] | Packet type classification |

### UI — Heard Screen

The Heard screen (also called Packets screen, `heard_screen_show()` in `src/ui/screens/screen_packets.cpp`) renders a live-updating list:
- Timestamp column
- Source column (node name or "RADIO")
- RSSI column (dBm)
- SNR column (dB)
- Type column (ADVERT_RX, DM_RX, TX_DM, etc.)

A timer at `g_packets_timer` polls `getPacketLogCount()` every refresh cycle and rebuilds the list when the count changes.

---

## Signal Strength Display (Network Screen)

The Network screen (mapped to both the REPEATERS and FINDER home tiles) and the Signal screen provide mesh radio diagnostics.

### Network / Repeaters Screen

Navigated to via `Screen::Network`, calls `finder_screen_show()`. Shows:
- Nearby nodes sorted by signal strength (RSSI)
- Contact names and their last-seen RSSI

### Signal Screen

`signal_screen_show()` (in `src/ui/screens/screen_signal.cpp`) provides real-time radio metrics:

| Metric | API | Description |
|--------|-----|-------------|
| RSSI | `getLastRSSI()` | Last received packet RSSI (dBm) |
| SNR | `getLastSNR()` | Last received packet SNR (dB) |
| Noise Floor | `getNoiseFloor()` | Background noise level (dBm) |

The Signal screen renders these as visual bars for quick assessment.

---

## Persistence & State

### Identity Persistence

The node's cryptographic identity (private key + public key) is persisted in SPIFFS at `/mesh_id`:

- **Format:** Raw binary — either 64 bytes (private key only) or 96 bytes (private + public)
- **Loading:** `loadIdentity()` (`mesh_wrapper.cpp:108`) reads from SPIFFS, validates via `LocalIdentity::validatePrivateKey()`
- **Saving:** `saveIdentity()` (`mesh_wrapper.cpp:125`) writes via `id.writeTo()`, validates written length
- **Generation:** If no saved identity exists (or the file is corrupt), a new `LocalIdentity(&fast_rng)` is generated and saved
- **Fallback:** If SPIFFS is unavailable (`spiffs_ok = false`), the identity is ephemeral — regenerated on every boot

### Channel Persistence (NVS)

Channels are persisted using ESP32 **NVS** (Non-Volatile Storage) under the `"sigurdos"` namespace:

```cpp
void saveChannels()   // mesh_wrapper.cpp:561
void loadChannels()   // mesh_wrapper.cpp:581
```

Each channel stores three NVS keys:

| Key Pattern | Content |
|-------------|---------|
| `ch_<N>_name` | String — channel display name (e.g. `"#general"`) |
| `ch_<N>_sec` | Bytes — 32-byte channel secret key |
| `ch_<N>_hash` | Bytes — 32-byte channel hash |

Channel count is stored as `ch_cnt` (uint8_t). On boot, `loadChannels()` is called during `init()` after `g_mesh->begin()` to restore all previously joined channels.

### Persistence Store Module (`persistence_store.h/cpp`)

The `sigurdos::mesh::persistence_store` module provides the low-level NVS and
SPIFFS access functions that the wrapper layer uses for channel and identity
persistence:

| Function | Backend | Purpose |
|----------|---------|---------|
| `channelStoreSave(count, read, ctx)` | NVS (`"sigurdos"` namespace) | Save all channels via caller-provided `ChannelReadFn` |
| `channelStoreLoad(load, ctx)` | NVS | Load channels via caller-provided `ChannelLoadFn`; returns count loaded |
| `identityStoreSave(data, len)` | SPIFFS (`/mesh_id`) | Save raw identity bytes |

These are called by `saveChannels()` / `loadChannels()` and `saveIdentity()` /
`loadIdentity()` respectively in `mesh_wrapper.cpp`.

### Chat History Persistence

*(Not part of the mesh layer directly, but relevant)*

Message history is persisted in the bounded SPIFFS `message_store` log and
restored by `chat_load_messages()` in `src/ui/chat_screen.*`. The same records
drive companion offline sync; the retired `/msgs` UI snapshot is read only for
one-time upgrade migration.

### saveState()

`saves State()` (`mesh_wrapper.cpp:602`) is a convenience function that persists the identity key to SPIFFS. Called periodically and on critical events to ensure the node's identity is preserved.

---

## RTC & System Time

MeshCore requires a real-time clock for message timestamps and contact `last_seen` tracking.

### Clock Hierarchy

```
AutoDiscoverRTCClock
  ├── DS3231 external RTC (if detected on I2C)
  └── ESP32RTCClock (ESP32's built-in RTC — fallback)
```

`AutoDiscoverRTCClock` (`lib/meshcore/src/helpers/AutoDiscoverRTCClock.h`) probes the I2C bus for a DS3231 RTC module. If found, it uses the hardware RTC; otherwise it falls back to the ESP32's internal RTC.

### Public Time API

| Function | Description |
|----------|-------------|
| `getCurrentTime()` | Returns current Unix epoch timestamp |
| `setSystemTime(epoch, source)` | Sets both clocks and records Manual, Companion, or GPS provenance |
| `getTimeSyncStatus()` | Returns the last explicit source, sync epoch, and current age |
| `getCurrentLocalDateTime(y,m,d,h,min)` | Decomposes epoch into local date/time components using `gmtime()` |
| `makeEpoch(year,month,day,hour,minute)` | Builds an epoch timestamp from date/time components (UTC) |

### Time Sync

- MeshCore synchronises time across the network automatically via protocol messages
- Time can be set manually via Terminal commands or programmatically via `setSystemTime()`
- GPS parsing exposes a valid UTC fix from `hal/gps.cpp`; the main loop applies it through `setSystemTime()` only while user-enabled GPS polling is already active. GPS is never enabled solely to maintain the clock.

---

## Radio Configuration

### Hardware

- **Radio:** Semtech SX1262 LoRa transceiver
- **Interface:** SPI (NSS=9, SCK=40, MISO=38, MOSI=41)
- **Control:** DIO1=45, RST=17, BUSY=13
- **Driver:** RadioLib wrappers via CustomSX1262Wrapper (in `lib/meshcore/`)

### Compile-Time Defaults

```cpp
#define LORA_FREQ   869.618f    // MHz
#define LORA_BW     62.5f       // kHz
#define LORA_SF     8           // Spreading factor
#define LORA_CR     5           // Coding rate denominator (4/5)
#define LORA_TX_PWR 22          // dBm
```

These are defined in `src/hal/tdeck_pins.h`.

### Runtime Configuration

Radio parameters are stored in `NodePrefs` (NVS-backed) and can be changed via the **Radio Setup** screen (accessible through Settings):

| Parameter | Range | Description |
|-----------|-------|-------------|
| Frequency | 860–930 MHz | Centre frequency |
| Bandwidth | 7.8–500 kHz | LoRa bandwidth |
| Spreading Factor | 5–12 | SF (higher = longer range, slower) |
| Coding Rate | 5–8 | CR denominator (4/5 — 4/8) |
| TX Power | 2–22 dBm | Transmission power |

See `src/hal/prefs.h` for the full `NodePrefs` struct.

### Debug/Test Override: `SIGURDOS_DEBUG_FORCE_RADIO_PARAMS`

Defined in `platformio.ini` for remote-test and automation builds, this flag
**overrides** NVS-stored radio parameters with the compile-time defaults
(`LORA_FREQ`, `LORA_BW`, `LORA_SF`, `LORA_CR`, `LORA_TX_PWR`) to ensure
consistent RF behaviour regardless of stale NVS values from prior firmware
versions or manual configuration:

```cpp
// mesh_wrapper.cpp:799
#ifdef SIGURDOS_DEBUG_FORCE_RADIO_PARAMS
    freq = LORA_FREQ;
    bw   = LORA_BW;
    sf   = LORA_SF;
    // ...
#endif
```

This is **not** enabled by `SIGURDOS_DEBUG` — that flag is for diagnostic
logging only. `SIGURDOS_DEBUG_FORCE_RADIO_PARAMS` is intended for CI/remote-test
environments. When defined, it also auto-joins the `#testingsigurdos` test channel
(on frequency 869.525/SF10/BW250/CR5) so the device is fully operational without
requiring Settings → Radio Setup.

### SX1262 Hard Reset

On each boot, `mesh::init()` performs a **hardware reset** of the SX1262:

```cpp
pinMode(P_LORA_RESET, OUTPUT);
digitalWrite(P_LORA_RESET, LOW);
delayMicroseconds(100);
digitalWrite(P_LORA_RESET, HIGH);
delay(10);  // TCXO stabilization
```

This is necessary because the SX1262 may retain state across ESP32 reboots. If the BUSY pin is stuck HIGH from a previous crash, `std_init()` would hang in `waitForBusyPin()` and cause a watchdog reset loop.

### Transmit Gating

On first boot (or when radio parameters have not been explicitly configured), the firmware **will not transmit**. This prevents accidental broadcasts on frequencies that may be illegal in the user's region. The user must open Settings → Radio Setup to configure and save parameters before the device sends any adverts.

---

## Message Queue

Incoming messages (DMs and channel messages) are placed into a **circular message queue** before being consumed by the UI.

```cpp
static constexpr int MAX_QUEUED = 64;        // mesh_wrapper.cpp:57
static MeshMessage   msg_buf[MAX_QUEUED];    // Circular buffer
static int           msg_head, msg_tail, msg_count;
```

Each `MeshMessage` stores:

| Field | Type | Description |
|-------|------|-------------|
| `sender` | char[32] | Sender's node name |
| `channel` | char[32] | Channel name (empty string for DMs) |
| `text` | char[256] | Message body |
| `timestamp` | uint32_t | RTC timestamp from mesh layer |
| `is_self` | bool | True if this is a self-sent message (loopback) |

### API

| Function | Description |
|----------|-------------|
| `pollMessages(out, max)` | Dequeue up to `max` messages |
| `pendingMessageCount()` | Number of messages waiting in queue |
| `injectMessage(sender, channel, text)` | Inject a simulated message (for remote test mode) — no radio transmission |

### Callback Chain

```
onPeerDataRecv / onGroupDataRecv / onAnonDataRecv
  └─ _onMessage(sender, channel, text)
       └─ queue_push(sender, channel, text)  [mesh_wrapper.cpp:61]
            └─ msg_buf[msg_head++] = message
```

---

## Source Files

| File | Purpose |
|------|---------|
| `src/mesh/sigurd_mesh_v2.h` | Core `SigurdMeshV2` class — all virtual overrides, path learning, trace, ping, packet logging |
| `src/mesh/mesh_wrapper.h` | Public API declarations — structs, function signatures |
| `src/mesh/mesh_wrapper.cpp` | Implementation — init, loop, message queue, persistence, adverts, contacts, channels, time |
| `lib/meshcore/` | MeshCore library (git submodule) — protocol implementation, Mesh base class, routing |
| `src/hal/prefs.h` / `prefs.cpp` | `NodePrefs` — radio config, NVS storage |
| `src/hal/tdeck_pins.h` | Pin definitions, compile-time LoRa defaults |
| `src/main.cpp` | Boot sequence — init order and dependencies |

---

## Related Documents

| Document | Description |
|----------|-------------|
| [`FEATURES_OVERVIEW.md`](FEATURES_OVERVIEW.md) | High-level feature catalog — all 12 home screen tiles and system capabilities |
| [`CHAT_SCREEN.md`](CHAT_SCREEN.md) | Chat screen UI — channel tabs, DM conversations, message history |
| [`TERMINAL.md`](TERMINAL.md) | Terminal commands — mesh diagnostics, advert, trace, ping, inject |
| [`MISSING_FEATURES.md`](MISSING_FEATURES.md) | Companion parity audit — implemented, declined, and out-of-scope MeshCore deltas |
| [`KNOWN_ISSUES.md`](KNOWN_ISSUES.md) | Tracked bugs, limitations, and workarounds |
| [`AGENTS.md`](../AGENTS.md) | Full architecture guide — hardware, UI conventions, boot sequence |
