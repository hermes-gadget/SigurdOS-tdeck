## Companion command support

SigurdOS T-Deck implements the companion commands used for normal setup,
contacts, channels, text messaging, offline sync, status, telemetry, tracing,
signing, and configuration. Recognition of a command identifier does not imply
that its operation is supported.

This matrix describes the pinned MeshCore protocol at submodule commit
`a75f24ecc98889e2beb4f702dc89e737aa82739e`. Its numeric companion
command/response/PUSH/error contract also matches reviewed public-upstream
commit `a3a1aa5e3be34b42d8ac8c2cc244d30af6cdd71e`; the reproducible verification
procedure is in [`RELEASE_EVIDENCE.md`](RELEASE_EVIDENCE.md#companion-interop-and-golden-frames).

| Command family | Status | Notes |
|---|---|---|
| App/device query and time | Supported | Includes app start, device query, connection, time, battery/storage, and stats |
| Contacts and adverts | Supported | Import/export/update/remove/share, advert path/name/location, self advert, and path reset |
| Channels and text messages | Supported with one wire-format exception | Channel configuration/data/text, direct text, login, path discovery, and offline message sync are supported. `CMD_SET_CHANNEL` accepts the 16-byte-secret form; the 32-byte-secret form returns `ERR_CODE_UNSUPPORTED_CMD` |
| Direct raw data (`CMD_SEND_RAW_DATA`) | Restricted | Accepts explicit 0–63-byte one-byte-hash paths only; the `0xFF` flood sentinel is rejected. Received raw payloads use `PUSH_CODE_RAW_DATA` |
| Zero-hop control data (`CMD_SEND_CONTROL_DATA`) | Supported | Requires the control high bit and always uses zero-hop routing; invalid non-control payloads return `ERR_CODE_ILLEGAL_ARG`. Received controls use `PUSH_CODE_CONTROL_DATA` |
| Radio, tuning, flood scope, and custom variables | Supported | Subject to T-Deck radio-region and TX-safety gates |
| Binary and anonymous peer requests (`CMD_SEND_BINARY_REQ`, `CMD_SEND_ANON_REQ`) | Supported | Requests are sent through `BaseChatMesh`; matching replies emit `PUSH_CODE_BINARY_RESPONSE`. Anonymous requests may use transient contacts |
| Status, telemetry, and trace | Supported | Standard request/response and async push flows |
| Identity import/export and signing | Partial | Signing accepts up to 8192 bytes. Private-key import remains supported; export is disabled by default because the pinned asynchronous BLE queue cannot erase completed slots. Trusted development builds may explicitly set `SIGURDOS_ENABLE_PRIVATE_KEY_EXPORT=1`. Sensitive command, signing, key-temporary, and receive-queue storage is erased at lifecycle boundaries. BLE administration relies on bonding; companion USB has no protocol authentication |
| `CMD_SEND_RAW_PACKET` | Unsupported | Returns `ERR_CODE_UNSUPPORTED_CMD`; arbitrary packet injection is not exposed |

Of the 58 defined command IDs, `CMD_SEND_RAW_PACKET` is the only command that is
fully refused. This is an explicit policy decision: arbitrary parsed-packet
injection is not exposed. `CMD_SEND_RAW_DATA` is available only for an explicit
one-byte-hash path and rejects flood routing; `CMD_SEND_CONTROL_DATA` is
available only for the zero-hop high-bit control form. Clients must also handle
`ERR_CODE_UNSUPPORTED_CMD` if they send the unsupported 32-byte-secret variant
of `CMD_SET_CHANNEL`.

All 17 `PUSH_CODE_*` identifiers are defined. `PUSH_CODE_LOG_RX_DATA` (`0x88`)
is not currently emitted. `PUSH_CODE_BINARY_RESPONSE` (`0x8C`) is emitted by
`pushBinaryResponse()` when a matching binary or anonymous request response
arrives; unmatched or expired tags are discarded.

Release interoperability should cover the official Android and iOS apps plus a
stock MeshCore companion radio. Core text messaging compatibility is broader
than the deliberately refused raw-packet injection command.

## Persistence and upgrade boundary

SigurdOS atomically persists its own identity, contacts, channels, advert
blobs, messages, and flood-scope configuration. It does not import the stock
MeshCore companion firmware's filesystem or preference layouts in place.
Before flashing SigurdOS over stock firmware, export the private identity and
contact/channel configuration through the companion app (or contact/channel
URIs), then import them after provisioning SigurdOS. This explicit backup and
restore boundary avoids guessing at stock layouts that vary by board and
filesystem.

Every identity import follows the same sequence: the candidate key is
validated and durably committed, login sessions and identity-derived contact
secrets are invalidated, contacts are reloaded, and bridge session state is
reset. A failed durable write leaves the running identity unchanged.

## BLE and forwarding policy

BLE is a single-client, compile-time-selected companion transport; BLE and USB
are not served simultaneously and there is no runtime transport switch. BLE
re-advertises after disconnect. Clients should use write-with-response. Because
the installed Arduino callback runs after the ATT acknowledgement, an invalid,
oversized, or queue-saturating write is treated as a transport fault: the peer
is disconnected and must reconnect and resynchronize. The observer's
`ble_write_drop_count` therefore counts connection-fatal RX faults, not silent
telemetry loss.

Existing bonded devices may authenticate whenever BLE is enabled. A new device
is accepted only during the two-minute window opened locally from Bluetooth →
Pair new device. Failed, timed-out, or abandoned authentication applies
peer-aware and global exponential advertising backoff, bounded at five minutes.
A successful authentication or explicitly opening a new pairing window clears
the counters, preventing a remote permanent lockout.

BLE bonds authorize an administrative companion. USB companion mode instead
trusts physical access to the cable and host; the four-digit device PIN does
not authenticate that protocol. Factory reset and identity commands inherit
the selected transport's boundary. See [Security model](SECURITY_MODEL.md) for
bond revocation, reset, private-key, and physical-access assumptions.

| PlatformIO environment | Companion transport | Intended use |
|---|---|---|
| `SigurdOS_TDeck` | BLE NUS, enabled by default | Normal release firmware; users may disable advertising at runtime |
| `SigurdOS_TDeck_ble_validation` | BLE NUS + validation instrumentation | Compile/memory validation of the production BLE path |
| `SigurdOS_TDeck_ble_agent` | BLE NUS + radio + remote test telemetry | Automated advertising and pairing diagnostics on hardware |
| `SigurdOS_TDeck_companion_usb` | USB CDC binary framing; BLE compiled out | Host protocol matrix and wired companion operation; core debug level is forced to 0 |

Build a variant with `pio run -e <environment>`. The USB companion build owns
the CDC byte stream, so it cannot simultaneously expose the text remote-test
controller or general serial console; Arduino/ESP-IDF debug output is suppressed
to keep binary frames uncorrupted. Use `firmware-merged.bin` for a direct flash.
Hardware commands and host tooling are documented in
[`COMPANION_BLE_TEST_ENV.md`](COMPANION_BLE_TEST_ENV.md).

Changing `CMD_SET_DEVICE_PIN` is deliberately disruptive. SigurdOS atomically
persists the replacement PIN with a bond-reset marker, allows a short interval
for the `OK` response, stops BLE, removes every bonded peer, and restarts only
after the security database is empty. If power is lost during rotation, the
next boot withholds advertising and resumes the purge before enabling BLE. A
PIN value of zero selects a newly generated six-digit PIN on restart. Every
phone must pair again after this command.
Command responses have a bridge-owned reserved
retry slot: when the transport queue is congested, the bridge stops consuming
commands until the response is admitted. Unsolicited live-event pushes are
best-effort and may be dropped under congestion; durable messages remain in the
message store and are retried by offline sync.

Protocol target version and encoded offline pages are connection-scoped and
cleared on disconnect. Clients must complete `CMD_DEVICE_QUERY` before
`CMD_SYNC_NEXT_MESSAGE` after reconnecting; an early sync returns
`ERR_CODE_BAD_STATE`. `CMD_APP_START` may still precede negotiation, and any
page it primes is discarded and rebuilt after the version query.

The server requests a 517-byte ATT MTU and admits a protocol frame only when it
fits the negotiated peer payload (`ATT_MTU - 3`). Failed notifications stay in
the bounded transport queue for retry. Offline message sync is at-least-once:
a durable record remains in flight until the same authenticated connection
requests the next record. Disconnecting before that request causes the record
to be replayed after reconnect, so clients must tolerate duplicate delivery.

`client_repeat` matches stock companion behavior: when disabled the handheld
does not forward packets; when enabled it forwards without applying
repeater-oriented RegionMap deny-flood flags. Region selection controls the
scope stamped on originated floods, not receive or forwarding policy.

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
