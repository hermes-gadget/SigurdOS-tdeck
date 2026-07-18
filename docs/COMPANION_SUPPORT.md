## Companion command support

SigurdOS T-Deck implements the companion commands used for normal setup,
contacts, channels, text messaging, offline sync, status, telemetry, tracing,
signing, and configuration. Recognition of a command identifier does not imply
that its operation is supported.

This matrix describes the pinned MeshCore protocol at submodule commit
`516ba4aef02adc9a73c568cab968834c60a06ae4`. SigurdOS advertises firmware
protocol code 12. It will remain at 12 until the protocol-13 path-discovery,
scope, login, and contact behaviours are all covered by interoperability tests.

| Command family | Status | Notes |
|---|---|---|
| App/device query and time | Supported | Includes app start, device query, connection, time, battery/storage, and stats |
| Contacts and adverts | Supported | Import/export/update/remove/share, advert path/name/location, self advert, and path reset |
| Channels and text messages | Supported with one wire-format exception | Channel configuration/data/text, direct text, login, path discovery, and offline message sync are supported. `CMD_SET_CHANNEL` accepts the 16-byte-secret form; the 32-byte-secret form returns `ERR_CODE_UNSUPPORTED_CMD` |
| Direct raw data (`CMD_SEND_RAW_DATA`) | Restricted | Accepts explicit 0–63-byte one-byte-hash paths only; the `0xFF` flood sentinel is rejected. Received raw payloads use `PUSH_CODE_RAW_DATA` |
| Zero-hop control data (`CMD_SEND_CONTROL_DATA`) | Supported | Requires the control high bit and always uses zero-hop routing; invalid non-control payloads return `ERR_CODE_ILLEGAL_ARG`. Received controls use `PUSH_CODE_CONTROL_DATA` |
| Radio, tuning, flood scope, and custom variables | Supported | Subject to T-Deck radio-region and TX-safety gates. The T-Deck sensor manager currently exposes `gps` and the 32-bit `gps_interval`; those are therefore the complete custom-variable set for this hardware |
| Binary and anonymous peer requests (`CMD_SEND_BINARY_REQ`, `CMD_SEND_ANON_REQ`) | Supported | Requests are sent through `BaseChatMesh`; matching replies emit `PUSH_CODE_BINARY_RESPONSE`. Anonymous requests may use transient contacts |
| Status, telemetry, and trace | Supported | Standard request/response and async push flows |
| Identity import/export and signing | Supported | Signing accepts the upstream 8192-byte maximum. Private-key import/export can be disabled independently at build time with `ENABLE_PRIVATE_KEY_IMPORT=0` and `ENABLE_PRIVATE_KEY_EXPORT=0`; factory reset remains guarded by the authenticated protocol contract |
| `CMD_SEND_RAW_PACKET` | Unsupported | Returns `ERR_CODE_UNSUPPORTED_CMD`; arbitrary packet injection is not exposed |

Of the 58 defined command IDs, `CMD_SEND_RAW_PACKET` is the only command that is
fully refused. This is an explicit policy decision: arbitrary parsed-packet
injection is not exposed. `CMD_SEND_RAW_DATA` is available only for an explicit
one-byte-hash path and rejects flood routing; `CMD_SEND_CONTROL_DATA` is
available only for the zero-hop high-bit control form. Clients must also handle
`ERR_CODE_UNSUPPORTED_CMD` if they send the unsupported 32-byte-secret variant
of `CMD_SET_CHANNEL`.

All 17 `PUSH_CODE_*` identifiers are defined. `PUSH_CODE_LOG_RX_DATA` (`0x88`)
is intentionally not emitted: it exposes raw received RF diagnostics to every
paired companion and is disabled as a privacy policy. `PUSH_CODE_BINARY_RESPONSE` (`0x8C`) is emitted by
`pushBinaryResponse()` when a matching binary or anonymous request response
arrives; unmatched or expired tags are discarded.

Release interoperability should cover the official Android and iOS apps plus a
stock MeshCore companion radio. Core text messaging compatibility is broader
than the deliberately refused raw-packet injection command.

## Transports and resource budget

The normal `SigurdOS_TDeck` build enables the BLE NUS companion transport.
`SigurdOS_TDeck_companion_usb` provides a mutually exclusive USB CDC build.
Hardware-UART companion mode is not provided because the T-Deck GPS occupies
UART pins 43/44. Wi-Fi is used for OTA, but companion TCP is not exposed because
the protocol has no transport-level authentication policy suitable for a LAN
listener. Transport selection remains compile-time, matching the single-client
upstream architecture.

The 2026-07-18 release build used 128,944 of 327,680 bytes of internal RAM
(39.4%) and 2,621,781 of 6,553,600 bytes of application flash (40.0%). That
leaves 198,736 bytes of internal RAM and 3,931,819 bytes of application flash.
The 8 KiB signing accumulator is allocated from PSRAM first, with internal RAM
as a fallback, and exists only during an active signing transaction. Runtime
PSRAM headroom is workload-dependent because the display and map caches also
use it; release validation must therefore include BLE plus the map workload.

The application deliberately exact-pins the ESP32 platform and its major radio,
UI, and sensor dependencies. Protocol golden tests target both this pinned
MeshCore SHA and current upstream behaviour. The pinned MeshCore branch contains
local anonymous-contact fixes, so it must not be fast-forwarded without
reconciling contact allocation, persistence indices, and room keepalive tests.

## Offline delivery policy

Incoming direct text is durable and drains strictly oldest-first. The bridge
deduplicates those frames by message-store ID and marks a record sent only after
both transport acceptance and a successful store update. Compaction preserves
unsent incoming text ahead of already-sent and locally-authored history. If the
unsent backlog alone exceeds the 512-record store limit, the oldest excess is
dropped and counted by `messageStoreCompanionBacklogDropCount()`.

Channel-data frames remain volatile, matching their diagnostic/best-effort use.
They may replace older volatile frames when the 16-frame page is full, but may
never evict or overtake durable text. Other raw, control, trace, status,
telemetry, login, advert, and path-update pushes are live-connection events and
are not persisted.

## Device-authored message visibility

The companion protocol has **no official PUSH code for device-authored
messages**. The protocol's push model only surfaces:

* messages the connected app **sent** (`CMD_SEND_TXT_MSG` → `SEND_CONFIRMED`)
* messages **received over RF** (offline queue → `SYNC_NEXT_MESSAGE`)

A message **typed on the T-Deck keyboard** will transmit correctly over LoRa
but will **not appear in the official app thread**. The T-Deck is the source of
truth for locally-authored messages; the app sees only what it sent or what
arrived from the mesh.

SigurdOS does **not** synthesize fake `CONTACT_MSG_RECV` frames for self-sent
messages — this would misattribute the sender and break reply/ack logic.
Future protocol extensions to close this gap require a cooperating client.
