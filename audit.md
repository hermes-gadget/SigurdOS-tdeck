# MeshCore Companion Compatibility Audit

## Audit Scope

This is a deeper second-pass audit of SigurdOS T-Deck companion/client compatibility. It compares the current synced working tree against upstream MeshCore companion-radio behavior and focuses on implementation planning, not product-code changes.

Scope is limited to companion/client-facing behavior: BLE, USB serial, TCP/Wi-Fi companion transports, binary frame parsing, command IDs, response and push schemas, contacts, messages, channels, remote management, telemetry, location, flood scopes, QR import/export, ACK/reliability, persistence, and official client UX.

No product code was modified for this audit. The only intended repo artifact is this `audit.md`. `prompt.md` is a temporary task file and should be deleted after this update is complete.

Repository sync note: before the second pass, local untracked audit files were stashed, both remotes were fetched, the working branch was rebased to match `origin/dev`, and `lib/meshcore` was reset to the submodule commit recorded by the parent repo.

## Upstream Reference

* MeshCore repository URL: `https://github.com/meshcore-dev/MeshCore`
* MeshCore upstream reference commit checked: `e8d3c53ba1ea863937081cd0caad759b832f3028`
* MeshCore upstream reference date: `2026-06-13T00:17:36+12:00`
* Date checked: `2026-06-26`
* Local parent repo reference after sync: `origin/dev` at `c0fec27`
* Local vendored `lib/meshcore` submodule after sync: `60ea4a91bf14363e837037a79ce1bff7fa37483f`
* Local vendored submodule date: `2026-06-15T19:51:59+10:00`

Upstream folders/docs/files inspected:

* `/tmp/meshcore-audit/examples/companion_radio/MyMesh.h`
* `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp`
* `/tmp/meshcore-audit/examples/companion_radio/main.cpp`
* `/tmp/meshcore-audit/docs/companion_protocol.md`
* `/tmp/meshcore-audit/docs/qr_codes.md`
* `/tmp/meshcore-audit/docs/cli_commands.md`
* `/tmp/meshcore-audit/docs/terminal_chat_cli.md`
* `/tmp/meshcore-audit/docs/payloads.md`
* `/tmp/meshcore-audit/docs/packet_format.md`
* `/tmp/meshcore-audit/docs/stats_binary_frames.md`
* `/tmp/meshcore-audit/README.md`
* `/tmp/meshcore-audit/src/helpers/ArduinoSerialInterface.cpp`
* `/tmp/meshcore-audit/src/helpers/esp32/SerialBLEInterface.cpp`
* `/tmp/meshcore-audit/src/helpers/esp32/SerialWifiInterface.cpp`
* `/tmp/meshcore-audit/src/helpers/BaseChatMesh.cpp`
* `/tmp/meshcore-audit/src/helpers/BaseChatMesh.h`
* Local recorded submodule equivalents under `lib/meshcore/src/helpers/`

Important upstream baseline:

* `examples/companion_radio/MyMesh.h` advertises `FIRMWARE_VER_CODE 13` and default `FIRMWARE_VERSION "v1.16.0"`.
* `examples/companion_radio/MyMesh.cpp` defines companion commands through `CMD_SEND_RAW_PACKET` (65), including v13 `CMD_SEND_ANON_REQ` support for non-contact requests.
* Upstream companion firmware supports BLE, USB serial, and Wi-Fi TCP companion interfaces depending on build flags.
* BLE uses Nordic UART Service UUID `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`, RX UUID `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`, and TX UUID `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`.
* USB serial and Wi-Fi TCP use a 3-byte framing header: app-to-radio frames start with `<`, radio-to-app frames start with `>`, followed by a little-endian `uint16_t` payload length.

## Our Codebase Map

Transport layers:

* `platformio.ini` enables `-D SIGURDOS_COMPANION_BLE=1`, compiles `src/comms/*.cpp`, and directly includes `../lib/meshcore/src/helpers/esp32/SerialBLEInterface.cpp`.
* `src/comms/observed_ble_interface.h` and `.cpp` subclass MeshCore `SerialBLEInterface` and add counters for begin/enable/connect/MTU/auth/write/read state.
* `src/mesh/mesh_wrapper.cpp` initializes `ObservedSerialBLEInterface` with prefix `MeshCore-`, the node name, and `WrapperCompanionHost::blePin()`.
* `src/hal/wifi_ota.cpp` and `src/ui/screens/screen_wifi_networks.cpp` implement OTA and STA/AP Wi-Fi workflows; they are not companion TCP transports.
* No local code instantiates upstream `ArduinoSerialInterface` or `SerialWifiInterface` for the companion bridge.

Protocol parsing and encoding:

* `src/comms/companion_bridge.h` defines local command, response, push, error, text-type, contact, channel, stats, and host-interface types.
* `src/comms/companion_bridge.cpp` owns the companion frame state machine: `_cmd_frame`, `_out_frame`, `_app_target_ver`, contact iteration, 16-frame RAM offline queue, signing accumulator, and `handleFrame()`.
* `CompanionBridge::handleFrame()` parses app commands and calls `CompanionBridgeHost`.
* `CompanionBridge::buildMessageFrame()` encodes queued stored messages into v3 companion receive frames.
* `CompanionBridge::enqueueChannelData()` encodes inbound group datagrams as `RESP_CODE_CHANNEL_DATA_RECV`.

Message handling:

* `src/mesh/sigurd_mesh_v2.cpp` overrides `BaseChatMesh` receive hooks: `onMessageRecv`, `onCommandDataRecv`, `onAnonDataRecv`, `onSignedMessageRecv`, `onChannelMessageRecv`, `onChannelDataRecv`, `onContactResponse`, `onContactPathUpdated`, and `processAck`.
* `sigurdos::mesh::mesh_v2_queue_push()` in `src/mesh/mesh_wrapper.cpp` is the fan-out point from `SigurdMeshV2` into the UI queue and persistent companion message store.
* `src/mesh/companion_adapter.inc` provides `storeIncomingMessageForCompanion()` and `storeOutgoingMessageForCompanion()` and bridges live mesh events into `CompanionBridge`.
* Local outgoing app sends call `WrapperCompanionHost::sendTextByPubKeyPrefix()` and `sendChannelText()`, then store outgoing messages for the T-Deck UI/history. They are intentionally not mirrored back to the companion app as incoming messages.

Contact handling:

* `WrapperCompanionHost` maps companion contact commands to `SigurdMeshV2`/`BaseChatMesh` methods: `lookupContactByPubKey`, `addContact`, `removeContact`, `resetPathTo`, `shareContactZeroHop`, `exportContact`, and `importContact`.
* `companionContactFromInfo()` converts MeshCore `::ContactInfo` into the local companion wire struct.
* `SigurdMeshV2::processAck()` calls `registerAckedMessage()` and `mesh_v2_notify_send_confirmed()`.

Channel/room handling:

* `SigurdMeshV2` stores channels in `BaseChatMesh` slots but exposes compact non-empty channels through `getChannelCount()` and `getChannel()`.
* `WrapperCompanionHost::getChannel()` delegates to the compact channel accessor.
* `WrapperCompanionHost::setChannel()` rejects empty names and therefore cannot clear a fixed channel slot through the companion protocol.
* Room-server client flows exist in `SigurdMeshV2` pending request/response handling and UI screens, but generic companion `CMD_SEND_BINARY_REQ` is not implemented.

Location handling:

* `src/hal/gps.cpp` and `src/hal/prefs.h` store GPS enable/interval and fixed advert fallback coordinates.
* `WrapperCompanionHost::selfInfo()` returns current GPS or fixed companion-supplied lat/lon when `share_location` is enabled.
* `WrapperCompanionHost::setAdvertLatLon()` stores fixed-point lat/lon and enables `share_location`.
* `WrapperCompanionHost::selfTelemetry()` currently returns an empty blob.

ACK/reliability handling:

* `SigurdMeshV2::addPendingAck()` and `processAck()` track local ACKs for outgoing DMs.
* `registerAckedMessage()` marks local stored messages acked by conversation and timestamp.
* `CompanionBridge::notifySendConfirmed()` pushes `PUSH_CODE_SEND_CONFIRMED` to the app when a tracked ACK arrives.
* Companion sync delivery state is stored as `StoredMessage::companion_sent`, but current marking is global rather than per delivered frame.

QR/import/export:

* `src/ui/screens/screen_contacts.cpp` builds contact QR URIs.
* `src/ui/screens/screen_channels.cpp` builds channel QR URIs.
* `src/mesh/mesh_wrapper.cpp` imports `meshcore://contact/add?...` and `meshcore://channel/add?...` URIs and URL-decodes inbound names.
* QR display is in `src/app/qr_show.cpp` and `.h`.

UI surfaces:

* `src/ui/screens/screen_bluetooth.cpp` exposes BLE availability, app connection state, PIN, last sync time, and enable/disable control.
* `src/ui/screens/screen_settings.cpp` links Bluetooth, radio, GPS, Wi-Fi, system, and other settings.
* `src/ui/screens/screen_regions.cpp` manages local flood-scope regions.
* `src/ui/screens/screen_telemetry.cpp`, `screen_trace.cpp`, `screen_contacts.cpp`, `screen_channels.cpp`, and `screen_repeaters.cpp` expose local mesh/client features separate from the companion bridge.

Persistence/storage:

* `src/mesh/message_store.cpp` stores up to 64 messages in `/companion_msgs` on SPIFFS or a native test path.
* `StoredMessage` currently persists conversation, sender, text, timestamp, 6-byte sender prefix, RSSI, SNR quarters, path length, self/channel/acked/companion_sent flags.
* Contacts and channels are persisted through wrapper functions in `mesh_wrapper.cpp`.
* Preferences are in `src/hal/prefs.h` and `.cpp` via NVS.
* Regions use `src/mesh/regions.cpp` and upstream `RegionMap`, persisted at `/regions2`, plus `NodePrefs.active_region`.

Tests/build entry points:

* `test/test_companion_protocol/test_companion_protocol.cpp` covers many bridge frame shapes and command handlers.
* `test/test_message_store/test_message_store.cpp` covers append/load/dedup/path length/ack marking/rotation.
* `test/test_regions/test_regions.cpp` covers local region helpers.
* `platformio.ini` native tests use `native_test`; hardware firmware builds use `SigurdOS_TDeck`.

## Executive Summary

Overall compatibility level: partial. The synced repo implements a substantial BLE companion bridge for basic official-app workflows: device query, app start, contacts, basic channel/DM sends, channel data receive/send, stats, signing, selected remote-management requests, regions/flood-scope commands, ACK push, and a Bluetooth settings screen. Local native tests cover part of this surface.

Most serious blockers:

* Persistent companion sync can mark messages as delivered before the app has received them.
* Incoming companion messages lose critical metadata: exact sender public key, text subtype, signed-message extra data, and CLI data semantics.
* The bridge advertises firmware code 12 while upstream companion code is 13, and several current command/push families are absent.
* Private key import does not reset/reload contact cryptographic state.
* USB serial and Wi-Fi TCP companion transports are missing.

Highest-value missing features:

* Per-message companion delivery acknowledgement in storage.
* Stored message data model that preserves MeshCore text type and exact key identity.
* Path discovery, binary requests, raw/control data, anonymous requests, raw packet injection.
* Companion self telemetry and custom-variable settings.
* USB serial companion transport for web/desktop tooling.

Biggest implementation risks:

* Changing `StoredMessage` requires a storage migration or a deliberate store-version reset.
* Fixed channel-slot semantics conflict with the local UI's compact channel list.
* Adding serial/TCP transports can corrupt companion frames if debug output is mixed onto the same stream.
* Protocol-version bumps can cause official clients to send commands the device still cannot handle.
* Private-key import touches identity, contacts, ECDH shared secrets, and persistence ordering.

Recommended first PR: fix companion message delivery and metadata persistence. Add message IDs and a new stored-message schema with `txt_type`, `extra`, and exact sender prefix/pubkey fields; mark records companion-sent only when `CMD_SYNC_NEXT_MESSAGE` returns that specific frame. This removes the highest data-loss risk and creates the data model needed for CLI/signed-message compatibility.

First-pass audit reconciliation:

* Original findings 1 and 11 overlap. They are consolidated into Deep Finding 1: protocol version and missing command/push families.
* Original findings 4, 5, and 6 share one root cause: `StoredMessage` lacks companion message metadata. They are consolidated into Deep Finding 4 with separate sub-issues.
* Original finding 10 mixed advert path, path discovery, and self-advert behavior. It remains one finding here because those workflows all use route/path metadata, but the fix plan is split into separate steps.
* Original finding 13 overstated region absence. Regions and scoped sends exist; the remaining compatibility issue is private default-scope key persistence and command/default-scope semantics.
* No original finding was disproven. Several are now marked as overlapping or narrowed.

## Compatibility Matrix

| Feature area | MeshCore expected behaviour | Our current implementation | Status | Severity | Fix complexity | Recommended PR |
| --- | --- | --- | --- | --- | --- | --- |
| BLE companion transport | NUS UUIDs, one frame per BLE write/notify, static PIN/bonding, notification pacing | Uses upstream `SerialBLEInterface` through `ObservedSerialBLEInterface` | partial | medium | medium | PR 8 |
| USB serial companion transport | `ArduinoSerialInterface` with `<`/`>` + uint16 length framing | Not instantiated; USB CDC used for logs/test/terminal | missing | high | medium | PR 7 |
| Wi-Fi TCP companion transport | `SerialWifiInterface` TCP server, same frame headers | Wi-Fi is OTA/STA only, no companion TCP server | missing | medium | medium | PR 7 |
| Firmware/protocol version | Upstream `FIRMWARE_VER_CODE 13` | Local advertises `SIGURDOS_COMPANION_FIRMWARE_VER_CODE = 12` | partial | high | large | PR 6 |
| Basic handshake | `CMD_DEVICE_QUERY`, `CMD_APP_START`, self info/device info | Implemented and tested | compatible | low | low | PR 8 |
| Contacts list/CRUD | Full contact frames, add/update/remove/share/export/import | Mostly mapped through `BaseChatMesh` | partial | medium | medium | PR 3 |
| Private key import | Save identity and invalidate/reload contact secrets | Saves identity only | incompatible | high | medium | PR 3 |
| DM send | Plain and CLI outbound text, ACK metadata | Plain works; CLI uses app timestamp rather than unique local timestamp | partial | medium | small | PR 2 |
| DM receive | Exact sender key prefix, path, subtype, timestamp | Sender prefix reconstructed by name; subtype always plain | incompatible | high | medium | PR 1 |
| Channel text | Fixed channel slot index, path, timestamp, plain text | Basic send/receive works; channel slot model differs | partial | medium | medium | PR 4 |
| Channel data | `CMD_SEND_CHANNEL_DATA`, `RESP_CODE_CHANNEL_DATA_RECV` | Implemented and tested | compatible | low | low | PR 8 |
| Offline sync | Drain queued messages via `CMD_SYNC_NEXT_MESSAGE`; mark delivered after return | Persistent store marks all unsent records before delivery | incompatible | high | medium | PR 1 |
| Signed incoming messages | `TXT_TYPE_SIGNED_PLAIN` and 4-byte extra prefix | Flattened to plain text | incompatible | medium | medium | PR 1 |
| CLI command data receive | `TXT_TYPE_CLI_DATA` frames | Converted to `[CMD] name: text` plain chat | incompatible | high | medium | PR 1 |
| ACK/send confirmation | `PUSH_CODE_SEND_CONFIRMED` with ack/trip time | Implemented for tracked ACKs | partial | medium | low | PR 8 |
| Login/status/telemetry remote management | Async pushes include current upstream fields | Status/telemetry partial; login push omits newer fields | partial | medium | medium | PR 5 |
| Binary/path-discovery/raw/control/anon/raw-packet | Current upstream commands/pushes implemented | Most are absent | missing | medium | large | PR 6 |
| Self telemetry | CayenneLPP-style telemetry blob | Empty self telemetry response | missing | medium | medium | PR 5 |
| Custom variables | `GET_CUSTOM_VARS` and `SET_CUSTOM_VAR` | Empty get response; setter absent | missing | medium | medium | PR 5 |
| Channel deletion | Empty name/all-zero secret clears fixed slot | Empty names rejected; compact list hides slots | incompatible | medium | medium | PR 4 |
| Advert path/path discovery | Stored advert paths and path-discovery command/push | Path table exists but companion command returns not found; discovery command absent | partial | medium | medium | PR 6 |
| Self advert command | Optional arg chooses zero-hop vs flood-scoped | Arg ignored; local broadcast path floods | incompatible | medium | medium | PR 6 |
| Flood-scope defaults | Default scope name and 16-byte key persisted | Active region name persisted; private key not persisted through companion command | partial | medium | medium | PR 5 |
| Radio/PIN/time validation | Upstream bounds, allowed repeat ranges, six-digit PIN, monotonic time | Several local bounds/semantics diverge | partial | medium | medium | PR 5 |
| QR sharing | URL-encoded names in contact/channel URIs | Import decodes; export does not encode | partial | low | small | PR 8 |
| Official app hardware validation | Pair/reconnect/sync/settings exercised on device | Not run in this audit | unknown | medium | medium | PR 8 |

## Deep Findings

### Finding 1: Protocol version and current command surface are behind upstream

* Severity: High
* Status: **PARTIAL** (PR #735 — enum entries + stubs; full implementations deferred)
* Issue: [#734](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/734)
* PR: [#735](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/735)
* Confidence: High
* Our code: `src/comms/companion_bridge.h` (`SIGURDOS_COMPANION_FIRMWARE_VER_CODE`, `CompanionCommand`, `CompanionPush`), `src/comms/companion_bridge.cpp` (`CompanionBridge::handleFrame()`)
* Upstream reference: `/tmp/meshcore-audit/examples/companion_radio/MyMesh.h` (`FIRMWARE_VER_CODE 13`), `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp` (`CMD_*`, `PUSH_CODE_*`, `handleCmdFrame()`)
* Current behaviour: The local bridge advertises firmware protocol code 12 and does not define or handle upstream commands `CMD_SEND_RAW_DATA` (25), `CMD_SET_CUSTOM_VAR` (41), `CMD_SEND_BINARY_REQ` (50), `CMD_SEND_PATH_DISCOVERY_REQ` (52), `CMD_SEND_CONTROL_DATA` (55), `CMD_SEND_ANON_REQ` (57), and `CMD_SEND_RAW_PACKET` (65). Local push enum omits `PUSH_CODE_RAW_DATA`, `PUSH_CODE_LOG_RX_DATA`, `PUSH_CODE_BINARY_RESPONSE`, `PUSH_CODE_PATH_DISCOVERY_RESPONSE`, and `PUSH_CODE_CONTROL_DATA`.
* Expected MeshCore behaviour: Companion firmware v1.16.0 advertises protocol code 13 and implements these command/push families. Some are older than v13; `CMD_SEND_ANON_REQ` explicitly gained non-contact request support in code 13.
* Root cause: The local bridge is a hand-ported subset of upstream `MyMesh::handleCmdFrame()` and has not been updated as upstream added generic request, raw, control, path-discovery, and anon workflows.
* User impact: Official clients may hide newer features because the device reports code 12. If a client sends unsupported commands anyway, those workflows return `ERR_CODE_UNSUPPORTED_CMD`. Remote management, generic binary request/response, raw packet tooling, anonymous request flows, and control/raw data features are unavailable.
* Compatibility risk: High if the project claims full official companion compatibility. Medium if the project explicitly claims BLE basic chat/settings compatibility only.
* Detailed fix plan:
  1. Add a protocol coverage test table in `test/test_companion_protocol/test_companion_protocol.cpp` listing upstream command IDs 1-65, response IDs, push IDs, minimum firmware gate, and local support status.
  2. Add missing enum constants to `src/comms/companion_bridge.h`, but keep unsupported handlers returning deliberate errors until implemented.
  3. Implement command families in groups: raw/control, binary/anon/path discovery, custom vars, raw packet.
  4. Add host interface methods only when each command needs mesh access; do not bloat `CompanionBridgeHost` with unused methods.
  5. Bump `SIGURDOS_COMPANION_FIRMWARE_VER_CODE` only after the commands expected for that code are implemented or after a product decision that code 12 is intentionally retained.
* Pseudocode / implementation notes:
  ```cpp
  struct ProtocolCase {
      uint8_t cmd;
      const char* name;
      uint8_t min_fw;
      Support support;
  };

  // Test fails when upstream command table changes and local support has not
  // been explicitly accepted as supported/unsupported/deferred.
  ```
* Tests to add: command coverage table; byte-for-byte frames for each new response/push; malformed length tests; unsupported command tests for intentionally deferred commands.
* Hardware validation needed: Official app/dev-tool smoke after any firmware-code bump; raw/control/binary/path discovery against an upstream node.
* Suggested PR: PR 6 - "Bring companion protocol surface to current upstream command IDs"
* Estimated complexity: High

### Finding 2: USB serial and Wi-Fi TCP companion transports are missing

* Severity: High
* Status: **PARTIAL** (PR #737 — USB serial transport added; Wi-Fi TCP deferred)
* Issue: [#736](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/736)
* PR: [#737](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/737)
* Confidence: High
* Our code: `platformio.ini`, `src/comms/observed_ble_interface.*`, `src/mesh/mesh_wrapper.cpp`, `src/hal/wifi_ota.cpp`, `src/ui/screens/screen_bluetooth.cpp`
* Upstream reference: `/tmp/meshcore-audit/examples/companion_radio/main.cpp`, `/tmp/meshcore-audit/src/helpers/ArduinoSerialInterface.cpp`, `/tmp/meshcore-audit/src/helpers/esp32/SerialWifiInterface.cpp`, `/tmp/meshcore-audit/README.md`
* Current behaviour: The firmware creates one BLE serial interface and passes it to `CompanionBridge::begin()`. There is no companion bridge instance connected to USB CDC serial or a TCP server. Wi-Fi code is for OTA, scanning, and STA credentials, not companion protocol.
* Expected MeshCore behaviour: Companion firmware can be built for BLE, USB serial, or Wi-Fi TCP. Serial/TCP clients use `<` and `>` frame headers with little-endian lengths.
* Root cause: The initial bridge work reused only upstream `SerialBLEInterface` and did not add a transport abstraction/multiplexer for serial or TCP.
* User impact: Official web client, NodeJS tools, Python CLI tools, and desktop workflows that expect USB serial or TCP cannot use the T-Deck as a companion radio. BLE pairing/reconnect issues have no standard alternate companion path.
* Compatibility risk: High for web/desktop/client-tool compatibility; medium for mobile-only BLE use.
* Detailed fix plan:
  1. Add a transport manager in `src/comms/` that owns one `CompanionBridge` and one or more `BaseSerialInterface` implementations.
  2. Add a `UsbSerialInterface` wrapper using upstream `ArduinoSerialInterface` semantics, but keep debug logs off that stream when companion serial is enabled.
  3. Add optional `SerialWifiInterface` support behind a build flag or settings toggle; coordinate with existing Wi-Fi OTA/STA state.
  4. Expose transport state in Settings without conflating BLE and USB/TCP.
  5. Ensure all transports share the same command parser and storage state to avoid duplicate sync queues.
* Pseudocode / implementation notes:
  ```cpp
  class CompanionTransportManager {
      CompanionBridge bridge;
      BaseSerialInterface* active;
      void loop() {
          for (auto* iface : interfaces) {
              if (iface->isEnabled()) bridge.loopOn(iface);
          }
      }
  };
  ```
  If `CompanionBridge` stays single-serial, add `begin(serial, host)` rebind rules and reject simultaneous transports until a real multiplexer exists.
* Tests to add: frame-header parser tests for serial/TCP; no-debug-contamination test; connect/disconnect/reconnect tests; command over BLE and serial using the same fake host.
* Hardware validation needed: USB web client against `https://app.meshcore.nz`; TCP client with `meshcore.js`; BLE still works after serial/TCP additions.
* Suggested PR: PR 7 - "Add official USB serial and TCP companion transports"
* Estimated complexity: Medium to High

### Finding 3: Persistent companion sync marks messages delivered before delivery

* Severity: High
* Status: **FIXED** (PR #727)
* Issue: [#726](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/726)
* PR: [#727](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/727)
* Confidence: High
* Our code: `CompanionBridge::seedOfflineQueueFromStore()`, `CompanionBridge::enqueueMessage()` in `src/comms/companion_bridge.cpp`; `messageStoreMarkAllCompanionSent()` and `messageStoreLoadUnsent()` in `src/mesh/message_store.cpp`; `StoredMessage::companion_sent` in `src/mesh/message_store.h`
* Upstream reference: `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp` offline queue helpers and `CMD_SYNC_NEXT_MESSAGE`
* Current behaviour: The local bridge loads up to 16 unsent stored records into the RAM queue and then marks all unsent records companion-sent. Live incoming messages also call `messageStoreMarkAllCompanionSent()` after a frame is added to the RAM queue. The app has not necessarily requested or received those frames yet.
* Expected MeshCore behaviour: Upstream stores messages in a RAM offline queue and drains one frame per `CMD_SYNC_NEXT_MESSAGE`. A frame is removed only when returned to the app. Upstream does not have SigurdOS's persistent two-client store, so SigurdOS must add per-record delivery semantics.
* Root cause: `companion_sent` is a global coarse flag applied to all records to avoid duplicate re-queueing after reconnect. It is not tied to a specific frame returned to the app.
* User impact: Incoming messages can disappear from official app history after reconnect or reboot. This is most obvious with more than 16 unsent records or a reboot after marking but before `CMD_SYNC_NEXT_MESSAGE` drains the queue.
* Compatibility risk: High. This is a data-loss class bug for companion app state.
* Detailed fix plan:
  1. Add a stable message ID to `StoredMessage`, such as monotonic `uint32_t store_id` or `uint64_t` sequence.
  2. Change the offline RAM queue from raw frame only to `{store_id, frame}`.
  3. Replace `messageStoreMarkAllCompanionSent()` with `messageStoreMarkCompanionSent(store_id)` or identity-based exact marking.
  4. Mark a record sent only after `CMD_SYNC_NEXT_MESSAGE` writes that frame to the serial interface.
  5. If `writeFrame()` returns 0 because BLE/TCP is busy/disconnected, leave the record unsent and requeue later.
  6. Decide migration: bump `MESSAGE_STORE_VERSION` and rebuild the cache, or implement a migration that assigns IDs to old records.
* Pseudocode / implementation notes:
  ```cpp
  struct OfflineFrame {
      uint32_t store_id;
      uint8_t len;
      uint8_t buf[MAX_FRAME_SIZE];
  };

  if (cmd == CMD_SYNC_NEXT_MESSAGE) {
      OfflineFrame f;
      if (popOffline(f) && serial->writeFrame(f.buf, f.len) == f.len) {
          messageStoreMarkCompanionSent(f.store_id);
      }
  }
  ```
* Tests to add: 17 unsent messages with queue size 16; reboot-before-sync; disconnected `writeFrame()` returns 0; exact one-record marking per sync; migration/version behavior.
* Hardware validation needed: Phone disconnected, receive multiple RF messages, reboot T-Deck, reconnect phone, verify all messages sync exactly once.
* Suggested PR: PR 1 - "Fix companion message delivery state and persistent sync"
* Estimated complexity: Medium

### Finding 4: Stored companion messages do not preserve exact MeshCore receive metadata

* Severity: High
* Status: **FIXED** (PR #727)
* Issue: [#726](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/726)
* PR: [#727](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/727)
* Confidence: High
* Our code: `StoredMessage` in `src/mesh/message_store.h`; `CompanionBridge::buildMessageFrame()` in `src/comms/companion_bridge.cpp`; `storeIncomingMessageForCompanion()` and `fillStoredPrefixForName()` in `src/mesh/companion_adapter.inc`; `SigurdMeshV2::onMessageRecv()`, `onCommandDataRecv()`, `onSignedMessageRecv()`, `onChannelMessageRecv()` in `src/mesh/sigurd_mesh_v2.cpp`
* Upstream reference: `MyMesh::queueMessage()`, `onMessageRecv()`, `onCommandDataRecv()`, and `onSignedMessageRecv()` in `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp`; `BaseChatMesh::onPeerDataRecv()` in `/tmp/meshcore-audit/src/helpers/BaseChatMesh.cpp`
* Current behaviour: Direct-message sender prefix is reconstructed later by contact name. `buildMessageFrame()` always writes `COMPANION_TXT_PLAIN`. CLI data is formatted for local UI as `[CMD] name: text`. Signed incoming messages are stored as plain text and the 4-byte signed-message extra is discarded. Outbound CLI data uses the app-provided timestamp unless zero.
* Expected MeshCore behaviour: Upstream queues the exact sender public-key prefix from `ContactInfo`, preserves `TXT_TYPE_CLI_DATA`, preserves `TXT_TYPE_SIGNED_PLAIN`, and includes signed-message extra bytes before the text. For outbound CLI data, upstream uses the node RTC unique timestamp to avoid replay protection failures.
* Root cause: The local persistent message schema was designed around UI display fields, not companion protocol frames. It lacks `txt_type`, `extra_len`, `extra`, and exact sender identity fields.
* User impact: Official apps can place DMs in the wrong thread when contacts share names or names change. Remote-management command responses appear as ordinary chat. Signed messages lose authenticity/display semantics. CLI sends may trip replay protection if the app timestamp is stale.
* Compatibility risk: High for remote management and identity correctness; medium for basic plain-chat use with unique contact names.
* Detailed fix plan:
  1. Extend `StoredMessage` with `uint8_t txt_type`, `uint8_t extra_len`, `uint8_t extra[8]`, and exact sender prefix captured at receive time.
  2. Change `mesh_v2_queue_push()` or add a new companion-specific event function that accepts sender pubkey prefix, text type, extra, timestamp, path length, RSSI, and SNR.
  3. In `SigurdMeshV2::onMessageRecv()`, pass `contact.id.pub_key` directly.
  4. In `onCommandDataRecv()`, store app payload text as `TXT_TYPE_CLI_DATA`; keep `[CMD] ...` formatting only for local UI if desired.
  5. In `onSignedMessageRecv()`, store `TXT_TYPE_SIGNED_PLAIN` and the `sender_prefix` extra.
  6. Update `CompanionBridge::buildMessageFrame()` to emit `txt_type` and extra bytes before text for signed messages.
  7. For outbound `COMPANION_TXT_CLI_DATA`, ignore app timestamp and use `rtc_clock.getCurrentTimeUnique()` or the local equivalent.
  8. Bump `MESSAGE_STORE_VERSION`; decide whether to discard old cache or migrate old records to plain messages.
* Pseudocode / implementation notes:
  ```cpp
  struct StoredMessage {
      // existing fields...
      uint8_t txt_type;
      uint8_t extra_len;
      uint8_t extra[8];
  };

  out[i++] = msg.txt_type;
  memcpy(&out[i], &msg.timestamp, 4);
  i += 4;
  if (msg.txt_type == COMPANION_TXT_SIGNED_PLAIN) {
      memcpy(&out[i], msg.extra, msg.extra_len);
      i += msg.extra_len;
  }
  ```
* Tests to add: duplicate contact names with different keys; CLI receive frame has `txt_type == 1`; signed receive frame has `txt_type == 2` and 4-byte extra; outbound CLI uses local unique time; old store-version reset/migration test.
* Hardware validation needed: Remote-management CLI response from a repeater/room server; signed message from an upstream node; duplicate-name contact manual check in official app.
* Suggested PR: PR 1 - "Fix companion message metadata and frame reconstruction"
* Estimated complexity: Medium to High

### Finding 5: Private key import does not reset contact cryptographic state

* Severity: High
* Status: **FIXED** (PR #729)
* Issue: [#728](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/728)
* PR: [#729](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/729)
* Confidence: High
* Our code: `WrapperCompanionHost::importPrivateKey()` in `src/mesh/companion_adapter.inc`; `saveIdentity()` in `src/mesh/mesh_wrapper.cpp`
* Upstream reference: `CMD_IMPORT_PRIVATE_KEY` handling in `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp`
* Current behaviour: The companion host validates the private key, reads it into `g_mesh->self_id`, saves identity, and returns OK. Contacts remain loaded with any cached shared-secret/ECDH state.
* Expected MeshCore behaviour: Upstream saves identity, assigns `self_id`, then calls `resetContacts()` and reloads contacts to invalidate ECDH shared secrets derived from the old identity.
* Root cause: The local wrapper treats identity import as an isolated identity-store update and does not run the contact reload path.
* User impact: After restoring/importing identity from an official app, existing direct-message contacts can use stale shared secrets until reboot or manual reload. DMs may fail to decrypt or encrypt.
* Compatibility risk: High for identity restore/migration workflows.
* Detailed fix plan:
  1. Identify the local contact reset/reload primitives already used during boot in `mesh_wrapper.cpp`.
  2. Add a safe wrapper function, for example `reloadContactsAfterIdentityChange()`, that clears contact crypto/transient state and reloads persisted contacts.
  3. Call that function after `saveIdentity()` succeeds.
  4. If live reload is unsafe, return OK only after scheduling a controlled reboot with a short delay and clear app-visible documentation.
  5. Ensure any pending ACKs, pending requests, and companion offline queue are cleared or preserved deliberately.
* Pseudocode / implementation notes:
  ```cpp
  bool importPrivateKey(const uint8_t* key64) {
      if (!validate) return false;
      g_mesh->self_id.readFrom(key64, PRV_KEY_SIZE);
      saveIdentity(g_mesh->self_id);
      sigurdos::mesh::reloadContactsForNewIdentity();
      return true;
  }
  ```
* Tests to add: fake contact with shared-secret-valid state, import key, assert contacts reloaded/invalidated; import invalid key; persistence save failure path if mockable.
* Hardware validation needed: Export identity, import a different identity from official app, restart-free DM send/receive with an existing contact.
* Suggested PR: PR 3 - "Make companion private-key import reload contact crypto state"
* Estimated complexity: Medium

### Finding 6: Companion channel slots conflict with the local compact channel model

* Severity: Medium
* Status: **FIXED** (PR #731)
* Issue: [#730](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/730)
* PR: [#731](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/731)
* Confidence: High
* Our code: `WrapperCompanionHost::channelCount()`, `getChannel()`, and `setChannel()` in `src/mesh/companion_adapter.inc`; `SigurdMeshV2::getChannelCount()`, `getChannel()`, and `removeChannel()` in `src/mesh/sigurd_mesh_v2.cpp`
* Upstream reference: `CMD_GET_CHANNEL`, `CMD_SET_CHANNEL`, and `BaseChatMesh::getChannel()/setChannel()` in `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp` and `/tmp/meshcore-audit/src/helpers/BaseChatMesh.cpp`; `docs/companion_protocol.md`
* Current behaviour: Local UI/API compacts non-empty channels. `SigurdMeshV2::getChannel()` returns null for empty slots, `channelCount()` returns contiguous non-empty count, and companion `setChannel()` rejects empty names.
* Expected MeshCore behaviour: Companion clients address fixed channel indices `0..MAX_GROUP_CHANNELS-1`. Upstream `BaseChatMesh::getChannel()` returns the slot struct for any valid index. Setting an empty name with all-zero secret is how clients clear/delete a slot.
* Root cause: The local standalone UI wants a compact channel list, but the companion protocol is slot-based. The adapter currently exposes the compact UI model directly to the app.
* User impact: Official apps can fail to delete channels, can see fewer slots than expected, and can have channel indices shift relative to app state after local deletes/compaction.
* Compatibility risk: Medium. Basic channel send works, but channel management can be wrong.
* Detailed fix plan:
  1. Keep the local UI compact model if desired, but add companion-specific fixed-slot accessors.
  2. Implement `WrapperCompanionHost::getChannel(index)` using `BaseChatMesh::getChannel(index, cd)` directly and return empty names/secrets for empty slots.
  3. Implement empty-name/all-zero-secret handling in `setChannel()` as a slot clear.
  4. Decide whether clearing a middle slot should preserve upstream fixed holes for the companion or compact immediately for local UI. Recommended default: preserve fixed slots for companion storage and have UI filter empty slots.
  5. Update channel persistence to preserve fixed slot positions.
* Pseudocode / implementation notes:
  ```cpp
  bool setChannel(int index, const CompanionChannel& ch) {
      if (!valid index) return false;
      ChannelDetails cd{};
      copy name and first 16 secret bytes;
      BaseChatMesh::setChannel(index, cd);
      saveChannelsPreservingSlots();
      return true;
  }
  ```
* Tests to add: `GET_CHANNEL` for empty slot returns `RESP_CODE_CHANNEL_INFO`; `SET_CHANNEL` empty name clears; middle slot clear does not shift companion indices; local UI still hides empty slots.
* Hardware validation needed: Add, edit, and delete channels from official app and local T-Deck UI; send on remaining channels.
* Suggested PR: PR 4 - "Separate companion fixed channel slots from local compact channel UI"
* Estimated complexity: Medium

### Finding 7: Companion telemetry, custom variables, and "other params" are incomplete

* Severity: Medium
* Status: **FIXED** (PR #733)
* Issue: [#732](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/732)
* PR: [#733](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/733)
* Confidence: High
* Our code: `WrapperCompanionHost::selfTelemetry()` and `setOtherParams()` in `src/mesh/companion_adapter.inc`; `CMD_GET_CUSTOM_VARS` in `src/comms/companion_bridge.cpp`; `CompanionCommand` in `src/comms/companion_bridge.h`; `NodePrefs` GPS/location fields in `src/hal/prefs.h`
* Upstream reference: `CMD_SEND_TELEMETRY_REQ`, `CMD_GET_CUSTOM_VARS`, `CMD_SET_CUSTOM_VAR`, `CMD_SET_OTHER_PARAMS`, and telemetry permission handling in `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp`
* Current behaviour: Self telemetry returns an empty blob. `GET_CUSTOM_VARS` returns an empty list. `SET_CUSTOM_VAR` is absent. `setOtherParams()` ignores manual-add and telemetry mode fields and applies only location policy and multi-ACKs.
* Expected MeshCore behaviour: Upstream returns battery/sensor telemetry for self requests, exposes sensor settings as `name:value` pairs, supports `SET_CUSTOM_VAR`, and stores telemetry/manual-add settings.
* Root cause: SigurdOS has local battery/GPS data and prefs but no companion telemetry blob builder or remote setting map.
* User impact: Official app telemetry/settings panels appear empty or ineffective. Users cannot configure GPS/custom variables from companion clients.
* Compatibility risk: Medium.
* Detailed fix plan:
  1. Implement `selfTelemetry()` using the same CayenneLPP-style format already used by `SigurdMeshV2::onContactRequest()` for telemetry responses.
  2. Add a small custom-var registry backed by `NodePrefs`, starting with `gps` and `gps_interval` if product-approved.
  3. Add `CMD_SET_CUSTOM_VAR` parsing in `CompanionBridge::handleFrame()`.
  4. Add prefs fields for telemetry modes/manual-add if official app UI should round-trip those values; otherwise document and return stable defaults.
  5. Update `selfInfo()` so telemetry/manual-add bytes reflect actual stored behavior.
* Pseudocode / implementation notes:
  ```cpp
  // "gps:1,gps_interval:30"
  bool setCustomVar(name, value) {
      if (strcmp(name, "gps") == 0) prefs.gps_enabled = value[0] == '1';
      else if (strcmp(name, "gps_interval") == 0) prefs.gps_interval = clamp(atoi(value), 0, 86400);
      else return false;
  }
  ```
* Tests to add: self telemetry contains battery; optional GPS with fix; `GET_CUSTOM_VARS` exact string; `SET_CUSTOM_VAR` valid/invalid; `SET_OTHER_PARAMS` persistence.
* Hardware validation needed: Official app telemetry screen and GPS/custom settings against T-Deck with and without GPS fix.
* Suggested PR: PR 5 - "Add companion telemetry and supported custom variables"
* Estimated complexity: Medium

### Finding 8: Advert path, path discovery, and app-triggered self advert semantics diverge

* Severity: Medium
* Status: **PARTIAL** (PR #735 — advert path storage, GET_ADVERT_PATH, path discovery, zero-hop advert)
* Issue: [#734](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/734)
* PR: [#735](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/735)
* Confidence: High
* Our code: `CMD_GET_ADVERT_PATH` and `CMD_SEND_SELF_ADVERT` handling in `src/comms/companion_bridge.cpp`; `WrapperCompanionHost::sendAdvert()` in `src/mesh/companion_adapter.inc`; `SigurdMeshV2::_advert_paths`, `storeAdvertPath()`, `getAdvertPathLen()`, `sendPathDiscovery()`, and `broadcastAdvert()` in `src/mesh/sigurd_mesh_v2.cpp` and `.h`
* Upstream reference: `AdvertPath`, `onDiscoveredContact()`, `CMD_GET_ADVERT_PATH`, `CMD_SEND_PATH_DISCOVERY_REQ`, `PUSH_CODE_PATH_DISCOVERY_RESPONSE`, and `CMD_SEND_SELF_ADVERT` in `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp`
* Current behaviour: Local code stores advert path length and a 7-byte pubkey prefix but discards path bytes. Companion `CMD_GET_ADVERT_PATH` always returns not found. `CMD_SEND_PATH_DISCOVERY_REQ` is absent. `sendAdvert(bool flood)` ignores the `flood` argument and always calls a broadcast path that sends flood. Upstream zero-hop self advert behavior is not exposed.
* Expected MeshCore behaviour: Upstream stores advert path bytes and returns timestamp/path for `GET_ADVERT_PATH`. Path discovery sends a special flood telemetry request and later pushes in/out path bytes. Self advert command sends zero-hop unless the app requests flood-scoped send.
* Root cause: Local advert path tracking was built for UI hop display, not companion path export. Local advert send wrapper predates companion's zero-hop/flood distinction.
* User impact: Official apps cannot inspect advert paths or perform path discovery through SigurdOS. App-triggered adverts can create more network traffic than expected and do not match upstream command semantics.
* Compatibility risk: Medium for route-management features; low for basic chat.
* Detailed fix plan:
  1. Extend `AdvertPathEntry` to store path bytes up to `MAX_PATH_SIZE`, not just path length.
  2. Change `storeAdvertPath()` to copy inbound path bytes.
  3. Add a host method and bridge handler for `GET_ADVERT_PATH` that returns timestamp, path length, and encoded path bytes by pubkey prefix.
  4. Add `CMD_SEND_PATH_DISCOVERY_REQ` support by using or adapting `SigurdMeshV2::sendPathDiscovery()` and emitting `PUSH_CODE_PATH_DISCOVERY_RESPONSE` from `onContactPathRecv()`.
  5. Split `sendAdvert(bool flood)` into zero-hop and flood-scoped implementations. Use `createSelfAdvert()` plus `sendZeroHop()` for false and default flood scope for true.
* Pseudocode / implementation notes:
  ```cpp
  struct AdvertPathEntry {
      uint8_t pubkey_prefix[7];
      uint8_t path_len;
      uint8_t path[MAX_PATH_SIZE];
      char name[32];
      uint32_t recv_timestamp;
  };
  ```
* Tests to add: advert path stores bytes; `GET_ADVERT_PATH` found/not-found; path discovery command response and later push; self advert flood flag selects zero-hop vs flood.
* Hardware validation needed: Two-node path discovery with official app; packet log or serial diagnostics proving zero-hop/flood behavior.
* Suggested PR: PR 6 - "Implement companion advert path and path discovery semantics"
* Estimated complexity: Medium to High

### Finding 9: Remote-management async response frames are incomplete

* Severity: Medium
* Status: **FIXED** (PR #733)
* Issue: [#732](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/732)
* PR: [#733](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/733)
* Confidence: Medium to High
* Our code: `CompanionBridge::pushLoginResult()`, `pushStatusResponse()`, `pushTelemetryResponse()`, `pushTraceData()` in `src/comms/companion_bridge.cpp`; `SigurdMeshV2::onContactResponse()` and `mesh_v2_companion_login_push()` in `src/mesh/sigurd_mesh_v2.cpp` and `src/mesh/companion_adapter.inc`
* Upstream reference: `MyMesh::onContactResponse()`, `onContactPathRecv()`, and push frame construction in `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp`
* Current behaviour: Login success pushes include code, permission/admin byte, and pubkey prefix. Upstream newer login success includes pubkey prefix plus server timestamp/tag, ACL permission, and firmware version level. Binary response and path discovery pushes are missing because the commands are missing. Status/telemetry and trace pushes are partially implemented.
* Expected MeshCore behaviour: Official clients receive richer login success frames and generic binary/path-discovery response pushes to correlate remote-management flows.
* Root cause: Local remote-management support was implemented around existing UI status/telemetry/trace helpers, not copied byte-for-byte from upstream `MyMesh`.
* User impact: Official app remote-management UI can miss permission/ACL/firmware details and cannot correlate unsupported binary/path-discovery flows.
* Compatibility risk: Medium. Basic login may appear to work while advanced admin controls fail or show incomplete state.
* Detailed fix plan:
  1. Update `pushLoginResult()` signature to include optional tag/server timestamp, ACL permissions, and firmware level.
  2. Parse and forward those fields from `SigurdMeshV2::onContactResponse()`.
  3. Add `PUSH_CODE_BINARY_RESPONSE` and path-discovery response as part of generic command work.
  4. Add tests that match upstream login legacy and new-frame layouts.
* Pseudocode / implementation notes:
  ```cpp
  pushLoginResult(prefix, success, permission, tag, acl, fw_level, has_extended);
  ```
* Tests to add: new login OK frame with tag/ACL/fw; legacy login OK frame; login fail; binary response correlation by tag.
* Hardware validation needed: Official app remote login to a repeater/room server, verify permissions/ACL display and command availability.
* Suggested PR: PR 5 - "Complete companion remote-management response frames"
* Estimated complexity: Medium

### Finding 10: Radio, PIN, repeat-frequency, and time validation differ from upstream

* Severity: Medium
* Status: **FIXED** (PR #733)
* Issue: [#732](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/732)
* PR: [#733](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/733)
* Confidence: High
* Our code: `WrapperCompanionHost::setRadioParams()`, `setRadioTxPower()`, `setBlePin()`, `allowedRepeatFreqRanges()`, and `setCurrentTime()` in `src/mesh/companion_adapter.inc`; `sigurdos::mesh::setSystemTime()` in `src/mesh/mesh_wrapper.cpp`
* Upstream reference: `CMD_SET_RADIO_PARAMS`, `CMD_SET_RADIO_TX_POWER`, `CMD_GET_ALLOWED_REPEAT_FREQ`, `CMD_SET_DEVICE_PIN`, and `CMD_SET_DEVICE_TIME` in `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp`
* Current behaviour: Local radio params accept 400000-1000000 kHz, bandwidth 7800-500000 Hz, SF 6-12, TX power 2-22 dBm, client repeat 0/1 without allowed-frequency gating. Allowed repeat ranges returns none. Nonzero device PINs can be 4-6 digits. System time accepts backwards changes.
* Expected MeshCore behaviour: Upstream accepts 150000-2500000 kHz, bandwidth 7000-500000 Hz, SF 5-12, TX power down to -9 up to board max, validates client-repeat frequency against allowed ranges, returns allowed ranges, requires PIN 0 or six digits, and rejects time rollback.
* Root cause: Local validation is T-Deck/SX1262-specific and privacy/safety-oriented, but the companion protocol does not communicate those narrower constraints clearly. Some constraints are simply mismatched.
* User impact: Official app may reject or hide repeat configuration, valid upstream values can be refused, invalid repeat modes can be accepted, PIN UX differs, and clock rollback can affect replay/timestamp logic.
* Compatibility risk: Medium.
* Detailed fix plan:
  1. Product decision: choose strict T-Deck hardware/regulatory bounds or upstream generic bounds. Recommended default is strict hardware/regulatory bounds but app-visible via returned ranges.
  2. Implement `allowedRepeatFreqRanges()` with explicit local ranges or disable client repeat in device info.
  3. Enforce six-digit nonzero PINs to match upstream.
  4. Reject backwards `setSystemTime()` unless an explicit recovery override is added.
  5. Ensure companion app errors are `ERR_CODE_ILLEGAL_ARG`, not misleading not-found/table-full errors.
* Pseudocode / implementation notes:
  ```cpp
  if (pin != 0 && (pin < 100000 || pin > 999999)) return false;
  if (epoch < getCurrentTime()) return false;
  ```
* Tests to add: all boundary values; allowed-repeat response shape; client-repeat frequency outside/inside range; backwards time rejection.
* Hardware validation needed: Official app radio settings save, PIN change/pairing, repeat toggle on allowed and disallowed frequencies.
* Suggested PR: PR 5 - "Align companion settings validation and app-visible ranges"
* Estimated complexity: Medium

### Finding 11: Private flood-scope default keys are not persisted through companion commands

* Severity: Medium
* Status: **FIXED** (PR #733)
* Issue: [#732](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/732)
* PR: [#733](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/733)
* Confidence: High
* Our code: `WrapperCompanionHost::getDefaultFloodScope()`, `setDefaultFloodScope()`, and `setFloodScopeOverride()` in `src/mesh/companion_adapter.inc`; `src/mesh/regions.cpp`; `src/hal/prefs.h`
* Upstream reference: `CMD_SET_DEFAULT_FLOOD_SCOPE`, `CMD_GET_DEFAULT_FLOOD_SCOPE`, `CMD_SET_FLOOD_SCOPE_KEY`, and `NodePrefs.default_scope_name/default_scope_key` in `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp` and companion `NodePrefs.h`
* Current behaviour: Local regions and active scoped sends exist. `setDefaultFloodScope()` can create/select a region name, but the code comment says private `$` keys would need `TransportKeyStore::putKey` and currently relies on the companion app re-sending the key. `NodePrefs` stores only `active_region`, not the default companion key.
* Expected MeshCore behaviour: Upstream stores both default scope name and 16-byte default scope key and uses them for flood-scoped sends after reboot.
* Root cause: SigurdOS adopted `RegionMap` persistence but did not add companion-style persistence for private default scope keys.
* User impact: An official app can set a private default flood scope and receive OK, but after reboot the key may not be available. Flood-scoped messages/adverts can be sent with the wrong scope or no matching private key.
* Compatibility risk: Medium for users of private regions/flood scopes.
* Detailed fix plan:
  1. Add a persisted default companion flood-scope record: name[31] plus key[16], either in `NodePrefs` or region/key storage.
  2. For public `#` names, derive/store the key consistently with `TransportKeyStore::getAutoKeyFor()`.
  3. For private `$` names, store the app-provided 16-byte key and restore it at boot before active-scope setup.
  4. Make `GET_DEFAULT_FLOOD_SCOPE` return the exact persisted name/key.
  5. Decide how this relates to UI active region: recommended default is app default scope and UI active region should be the same persisted concept unless product wants separate states.
* Pseudocode / implementation notes:
  ```cpp
  struct CompanionDefaultScope {
      char name[31];
      uint8_t key[16];
  };
  ```
* Tests to add: set `$private` default, reload prefs/regions, get same key; clear default; public hashtag derivation; active send scope restored after reboot.
* Hardware validation needed: Private flood scope set from official app, reboot, send scoped message through a region-aware repeater.
* Suggested PR: PR 5 - "Persist companion default flood-scope keys"
* Estimated complexity: Medium

### Finding 12: QR share URI names are not URL-encoded

* Severity: Low
* Status: **FIXED** (PR #739)
* Issue: [#738](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/738)
* PR: [#739](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/739)
* Confidence: High
* Our code: `src/ui/screens/screen_contacts.cpp` contact QR construction; `src/ui/screens/screen_channels.cpp` channel QR construction; import parsing in `src/mesh/mesh_wrapper.cpp`
* Upstream reference: `/tmp/meshcore-audit/docs/qr_codes.md`
* Current behaviour: QR export inserts raw contact/channel names into query parameters. Import code URL-decodes names.
* Expected MeshCore behaviour: QR `name` parameters are URL-encoded when needed.
* Root cause: Export path skipped query-component encoding even though import path supports decoding.
* User impact: Names containing spaces, `+`, `&`, `#`, `=`, `%`, or non-ASCII bytes can produce QR URIs that official clients parse incorrectly.
* Compatibility risk: Low but easy to fix.
* Detailed fix plan:
  1. Add a small URL query-component encoder in a local utility file or QR helper.
  2. Encode contact and channel names before `snprintf()` into the URI.
  3. Keep import decoding unchanged.
  4. Avoid logging or exposing channel secrets beyond the existing QR display.
* Pseudocode / implementation notes:
  ```cpp
  static bool urlEncodeQueryComponent(const char* in, char* out, size_t out_sz);
  ```
* Tests to add: names with space, plus, ampersand, hash, equals, percent, and UTF-8 bytes; round-trip import tests.
* Hardware validation needed: Scan generated QR with official app.
* Suggested PR: PR 8 - "URL-encode MeshCore QR share names"
* Estimated complexity: Low

### Finding 13: Official app and BLE hardware compatibility remains unproven

* Severity: Medium
* Status: **BLOCKED** — requires physical T-Deck with Raspberry Pi bridge (192.168.4.1 unreachable) + Android phone with official MeshCore companion app (not available in current environment)
* Confidence: High that validation is missing; unknown on runtime behavior
* Our code: `src/comms/observed_ble_interface.*`, `src/ui/screens/screen_bluetooth.cpp`, `scripts/validation/companion_ble_smoke.py` if present, `SigurdOS_TDeck_ble_validation` environment in `platformio.ini`
* Upstream reference: `/tmp/meshcore-audit/docs/companion_protocol.md`, `/tmp/meshcore-audit/src/helpers/esp32/SerialBLEInterface.cpp`, official clients listed in upstream `README.md`
* Current behaviour: Native protocol tests pass for selected frame handling, but this audit did not pair a physical T-Deck with official Android/iOS/web clients. BLE metrics exist, but no current hardware log was produced.
* Expected MeshCore behaviour: Official clients can scan, pair, negotiate MTU, send `DEVICE_QUERY`/`APP_START`, sync contacts/channels/messages, send/receive, reconnect, and survive idle/disconnect cycles.
* Root cause: Compatibility was developed largely through native tests and code inspection. BLE stacks and official clients have runtime behavior that native tests cannot prove.
* User impact: A release can pass native tests yet fail in pairing, MTU, notification pacing, reconnect, or app command sequencing.
* Compatibility risk: Medium until physical validation is done.
* Detailed fix plan:
  1. Create a hardware validation checklist with exact official app flows.
  2. Use `SigurdOS_TDeck_ble_validation` to collect BLE counters for pairing, MTU, RX/TX frame counts, drops, auth success/failure, and reconnect.
  3. Validate against Android and at least one non-mobile tool if USB/TCP is added.
  4. Add a release note stating which client versions and transports were validated.
* Pseudocode / implementation notes: None; this is validation work.
* Tests to add: host-side BLE smoke script that checks device query, app start, contacts, sync, and message send against a real device.
* Hardware validation needed: Yes, required before claiming official-app compatibility.
* Suggested PR: PR 8 - "Add official companion validation checklist and BLE smoke artifacts"
* Estimated complexity: Medium

## Missing Companion Features

| Feature | Upstream evidence | Affected local files/modules | Implementation approach | Priority |
| --- | --- | --- | --- | --- |
| USB serial companion transport | `ArduinoSerialInterface.cpp`, `examples/companion_radio/main.cpp`, upstream README | `platformio.ini`, `src/comms/`, `src/mesh/mesh_wrapper.cpp`, debug serial policy | Add serial interface/mode using `<`/`>` framing; isolate logs | High |
| Wi-Fi TCP companion transport | `SerialWifiInterface.cpp`, upstream README | `src/hal/wifi_ota.cpp`, Wi-Fi settings, `src/comms/` | Add optional TCP server sharing bridge state | Medium |
| Protocol code 13 support | `FIRMWARE_VER_CODE 13` and command table in `MyMesh.cpp` | `companion_bridge.h/cpp`, tests | Add coverage table and implement missing commands before bump | High |
| `CMD_SEND_RAW_DATA` / `PUSH_CODE_RAW_DATA` | `MyMesh.cpp` raw data handlers | `CompanionBridgeHost`, `SigurdMeshV2` raw callbacks | Add sendDirect raw and inbound raw push | Medium |
| `CMD_SET_CUSTOM_VAR` | `MyMesh.cpp` sensor settings | `companion_bridge.cpp`, `prefs.h/cpp` | Add setting registry and parser | Medium |
| `CMD_SEND_BINARY_REQ` / `PUSH_CODE_BINARY_RESPONSE` | `MyMesh.cpp` generic request path | `SigurdMeshV2::onContactResponse`, bridge push enum | Add pending tag tracking and push response | Medium |
| `CMD_SEND_PATH_DISCOVERY_REQ` | `MyMesh.cpp` path discovery | `SigurdMeshV2::sendPathDiscovery`, path receive hook | Wire existing discovery to companion command/push | Medium |
| `CMD_SEND_CONTROL_DATA` / `PUSH_CODE_CONTROL_DATA` | `MyMesh.cpp` control data send/recv | `SigurdMeshV2::onControlDataRecv`, bridge | Add zero-hop control send and inbound push | Medium |
| `CMD_SEND_ANON_REQ` | `MyMesh.cpp` v13 non-contact anon request | Existing `SigurdMeshV2::sendAnonMessage` | Generalize to raw anon request payload and tag response | Medium |
| `CMD_SEND_RAW_PACKET` | `MyMesh.cpp` parse/enqueue packet | Mesh packet parser/send APIs | Add strict parse and priority handling | Low to Medium |
| `PUSH_CODE_LOG_RX_DATA` | Upstream push enum | Diagnostics/logging path | Decide whether official clients use it; implement only if needed | Low |
| Self telemetry blob | `MyMesh.cpp` self telemetry response | `selfTelemetry()`, battery/GPS | Build CayenneLPP battery/GPS payload | Medium |
| Fixed companion channel slots | Upstream `BaseChatMesh::getChannel()` fixed slots | `WrapperCompanionHost`, channel persistence/UI | Preserve slots for companion, filter in UI | Medium |
| Per-message companion delivery ACK | Upstream RAM queue drain behavior, local persistent need | `message_store`, `CompanionBridge` | Add message IDs and exact marking | High |
| URL-encoded QR names | `docs/qr_codes.md` | QR screens | Add encoder | Low |

## Protocol and Transport Gaps

BLE:

* Local BLE reuses upstream UUIDs and queue behavior through `SerialBLEInterface`.
* Needs confirmation on real T-Deck for pairing, MTU negotiation, notification pacing, disconnect/reconnect, and app command sequencing.
* Local `ObservedSerialBLEInterface` records useful metrics but does not itself prove compatibility.

USB serial:

* Upstream USB companion frames are `<`/`>` plus little-endian `uint16_t` length.
* Local USB CDC currently carries logs, serial monitor, debug/test controller, and terminal interactions.
* Fix requires a product/build-mode decision: companion serial cannot share a stream with unframed debug output.

TCP/Wi-Fi:

* Upstream `SerialWifiInterface` accepts one TCP client and uses the same frame headers.
* Local Wi-Fi is OTA/scanning/STA credential management. There is no companion TCP server.
* Fix must coordinate Wi-Fi power state, OTA server, STA credentials, and app-visible transport status.

Packet framing:

* BLE has no extra `<`/`>` header; one write/notify is one companion frame.
* Serial/TCP require the 3-byte header.
* Local `CompanionBridge` consumes raw companion frames and can be reused behind multiple framed transports.

Command IDs and schemas:

* Local IDs 1-24, 26-40, 42-43, 51, 54, 56, 58-64 are mostly present.
* Missing IDs: 25, 41, 50, 52, 55, 57, 65.
* Missing push IDs: 0x84, 0x88, 0x8C, 0x8D, 0x8E.
* Existing login push schema is shorter than upstream's newer login success frame.

ACKs and state handling:

* RF ACK push exists through `processAck()` and `notifySendConfirmed()`.
* Companion delivery ACK does not exist per persistent record; current global marking is unsafe.
* Outbound CLI timestamps should use local unique RTC time to avoid replay issues.

Schemas/data model:

* `StoredMessage` needs protocol metadata, not just UI text.
* Channels need fixed slot representation for companion while UI can remain compact.
* Private flood-scope default needs persisted name plus key.

## Recommended PR Plan

### PR 1: Fix Companion Message Sync and Metadata ✅

* title: `fix: preserve companion message metadata and mark sync per message`
* **Status: MERGED/SUBMITTED** — Issue [#726](https://github.com/hermes-gadget/SigurdOS-tdeck/issues/726), PR [#727](https://github.com/hermes-gadget/SigurdOS-tdeck/pull/727)
* goal: eliminate companion message loss and preserve sender/type/extra metadata.
* findings fixed: 3, 4
* files likely changed: `src/mesh/message_store.h`, `src/mesh/message_store.cpp`, `src/comms/companion_bridge.cpp`, `src/mesh/companion_adapter.inc`, `src/mesh/mesh_wrapper.cpp`, `src/mesh/sigurd_mesh_v2.cpp`, `test/test_message_store/`, `test/test_companion_protocol/`
* test plan: native tests for message IDs, per-record marking, CLI/signed frames, duplicate names, migration/reset.
* risk level: High
* dependency on earlier PRs: none

### PR 2: Align Outbound CLI Send Semantics

* title: `fix: use local unique timestamps for companion CLI data`
* goal: match upstream CLI replay-protection behavior.
* findings fixed: part of 4
* files likely changed: `src/mesh/companion_adapter.inc`, tests
* test plan: `CMD_SEND_TXT_MSG` with `COMPANION_TXT_CLI_DATA` ignores stale app timestamp.
* risk level: Low
* dependency on earlier PRs: can be included in PR 1

### PR 3: Fix Identity Import and Contact Crypto Reload

* title: `fix: reload contact crypto state after companion private-key import`
* goal: make identity restore safe without stale ECDH/shared-secret state.
* findings fixed: 5
* files likely changed: `src/mesh/companion_adapter.inc`, `src/mesh/mesh_wrapper.cpp`, tests/mocks
* test plan: identity import invalidates/reloads contacts; invalid key rejected.
* risk level: Medium
* dependency on earlier PRs: none

### PR 4: Add Companion Fixed Channel Slot Semantics

* title: `fix: support fixed companion channel slots and slot clearing`
* goal: make official app channel add/edit/delete behavior match upstream.
* findings fixed: 6
* files likely changed: `src/mesh/sigurd_mesh_v2.cpp`, `src/mesh/companion_adapter.inc`, channel persistence, `screen_channels.cpp`, tests
* test plan: get empty slots, clear slots, preserve indices, UI filters empty.
* risk level: Medium
* dependency on earlier PRs: none

### PR 5: Complete Companion Settings, Telemetry, Flood Scope, and Remote Login Frames

* title: `feat: complete companion settings and telemetry frame support`
* goal: make app settings, self telemetry, private flood scopes, and login metadata round-trip.
* findings fixed: 7, 9, 10, 11
* files likely changed: `src/comms/companion_bridge.*`, `src/mesh/companion_adapter.inc`, `src/hal/prefs.*`, `src/mesh/regions.*`, tests
* test plan: telemetry/custom vars, PIN/time/radio bounds, default scope persistence, login push shapes.
* risk level: Medium
* dependency on earlier PRs: preferably after PR 1 for store schema stability, but not required.

### PR 6: Implement Missing Current Upstream Command Families

* title: `feat: add companion raw, control, binary, anon, and path-discovery commands`
* goal: close protocol-code 13 feature gaps.
* findings fixed: 1, 8
* files likely changed: `src/comms/companion_bridge.*`, `src/mesh/companion_adapter.inc`, `src/mesh/sigurd_mesh_v2.*`, tests
* test plan: command coverage table; byte-level responses/pushes; malformed payloads; upstream interop.
* risk level: High
* dependency on earlier PRs: PR 1 recommended first; protocol-code bump only after this PR.

### PR 7: Add USB Serial and Wi-Fi TCP Companion Transports

* title: `feat: add serial and TCP companion transports`
* goal: support official web/desktop/tooling clients.
* findings fixed: 2
* files likely changed: `platformio.ini`, `src/comms/`, `src/mesh/mesh_wrapper.cpp`, `src/hal/wifi_ota.*`, settings UI
* test plan: serial/TCP frame tests, debug-output isolation, web client smoke, TCP reconnect.
* risk level: High
* dependency on earlier PRs: after PR 1 so transport expansion does not multiply sync bugs.

### PR 8: QR Encoding and Official App Validation

* title: `test: add official companion validation checklist and QR encoding`
* goal: close low-risk QR bug and document/prove real client compatibility.
* findings fixed: 12, 13
* files likely changed: `src/ui/screens/screen_contacts.cpp`, `src/ui/screens/screen_channels.cpp`, QR helper/tests, `test/README.md` or validation docs/scripts
* test plan: QR unit tests; official app hardware checklist; BLE validation logs.
* risk level: Low to Medium
* dependency on earlier PRs: QR can land anytime; full validation after PRs 1-7.

## Test and Validation Plan

Static tests:

* Add a protocol coverage table test that lists every upstream `CMD_*`, `RESP_CODE_*`, and `PUSH_CODE_*` used by companion clients.
* Add compile-time/static assertions for frame payload size limits where possible.
* Add grep-based CI check or unit test preventing accidental version-code bump without command coverage updates.

Unit tests:

* `test_companion_protocol`: command parsing, malformed lengths, exact response bytes, push bytes, app target version branches.
* `test_message_store`: per-message IDs, companion delivery marking, migration/version handling, exact metadata persistence.
* `test_regions`: private default-scope key persistence and active scope restore.
* `test_channel_validation` or new channel tests: fixed slots, empty-slot clear, UI filtering.
* QR tests: URL encoding and import round-trip.

Integration tests:

* Native fake serial interface for serial/TCP framing.
* Fake host exercising full `CompanionBridgeHost` remote-management commands.
* Simulated reconnect/reboot: store messages, rebuild bridge, drain app sync, verify no drops/duplicates.
* Simulated duplicate contact names and changed names.

Simulator/emulator checks:

* Remote test mode can validate UI navigation and non-radio command paths, but cannot validate BLE pairing, physical radio, or RF path behavior.
* Do not use `SigurdOS_TDeck_remote_test` without explicit user consent.

On-device T-Deck validation steps:

* Flash a debug or BLE validation build.
* Pair official Android app; verify PIN, auth, MTU, device query, app start.
* Sync contacts/channels/messages.
* Send/receive DM and channel messages with another MeshCore node.
* Disconnect phone, receive more than 16 messages, reboot, reconnect, verify all sync once.
* Import/export identity, then DM an existing contact without reboot if live reload is implemented.
* Add/edit/delete channels from the app and local UI.
* Set radio params, PIN, GPS/custom variables, telemetry, and default flood scope.
* Run remote-management login/status/telemetry/trace against repeater or room server.
* Validate zero-hop vs flood self advert with packet logs or a second node.

Compatibility checks against MeshCore tools/clients:

* Android official app.
* iOS official app if available.
* Web client over USB once serial transport exists.
* `meshcore.js` or Python CLI over serial/TCP.
* Upstream companion radio as a behavioral reference for byte-level frame comparisons.

## Open Questions / Decisions Needed

* question: Should SigurdOS claim full companion-radio compatibility or BLE basic compatibility?
  * why it matters: Full compatibility requires serial/TCP transports and current command families; BLE basic can defer them.
  * possible options: Full upstream parity; BLE mobile parity only; explicit subset with version code 12.
  * recommended default: Claim partial/BLE basic until PRs 1-7 land and hardware validation passes.

* question: Should local T-Deck-originated messages be mirrored into official companion apps?
  * why it matters: Upstream companion radios have no local UI, so there is no clean upstream "device-originated sent message" frame.
  * possible options: Do not mirror; add a SigurdOS extension; echo as incoming and accept wrong semantics.
  * recommended default: Do not mirror through upstream frames; document this or design an extension only for SigurdOS clients.

* question: How should channel slots be stored long-term?
  * why it matters: Official app expects fixed slots; local UI expects compact list.
  * possible options: Preserve fixed slots internally and filter UI; keep compact and translate indices; migrate channels to a new fixed-slot store.
  * recommended default: Preserve fixed slots internally and filter UI.

* question: Should radio validation use upstream generic bounds or T-Deck/regional constraints?
  * why it matters: Official app compatibility and legal RF behavior can conflict.
  * possible options: Upstream broad bounds; strict local bounds; strict local bounds plus app-visible allowed ranges.
  * recommended default: Strict local bounds plus app-visible ranges and clear errors.

* question: How should private default flood-scope keys be stored?
  * why it matters: Private region compatibility requires reboot persistence.
  * possible options: Add fields to `NodePrefs`; store in `/regions2`/RegionMap; add a small companion scope blob.
  * recommended default: Add explicit NVS/blob record with name and key; keep RegionMap for region tree.

* question: Is private-key export always allowed?
  * why it matters: Upstream gates export behind `ENABLE_PRIVATE_KEY_EXPORT`; local export always succeeds when mesh exists.
  * possible options: Keep always exportable; add a build/prefs gate; require user confirmation on-device.
  * recommended default: Product/security decision needed; if full upstream parity is the goal, add a gate or explicit setting.

## Appendix: Evidence

Repository sync evidence:

* `git status --short --branch` after sync: `## ci/release-asset-upload-order...origin/dev` plus untracked `audit.md` and `prompt.md`.
* `git rev-list --left-right --count HEAD...origin/dev`: `0 0`.
* `git submodule status lib/meshcore`: `60ea4a91bf14363e837037a79ce1bff7fa37483f lib/meshcore`.
* Upstream MeshCore clone reference: `e8d3c53ba1ea863937081cd0caad759b832f3028`.

Local call paths:

* BLE init: `mesh_wrapper.cpp` -> `g_ble_serial.begin("MeshCore-", ...)` -> `CompanionBridge::begin(&g_ble_serial, &g_companion_host)`.
* Companion app frame: BLE write -> `SerialBLEInterface::checkRecvFrame()` -> `CompanionBridge::loop()` -> `CompanionBridge::handleFrame()` -> `WrapperCompanionHost`.
* Incoming RF DM: `SigurdMeshV2::onMessageRecv()` -> `mesh_v2_queue_push()` -> local UI queue + `storeIncomingMessageForCompanion()` -> `messageStoreAppend()` + `CompanionBridge::enqueueMessage()`.
* App sync: `CMD_SYNC_NEXT_MESSAGE` -> `CompanionBridge::getFromOfflineQueue()` -> `serial->writeFrame()`.
* ACK: `SigurdMeshV2::processAck()` -> `registerAckedMessage()` + `mesh_v2_notify_send_confirmed()` -> `PUSH_CODE_SEND_CONFIRMED`.

Selected grep evidence from current local tree:

* `src/comms/companion_bridge.h:15` has `SIGURDOS_COMPANION_FIRMWARE_VER_CODE = 12`.
* `src/comms/companion_bridge.h` has no local enum entries for `CMD_SEND_RAW_DATA`, `CMD_SET_CUSTOM_VAR`, `CMD_SEND_BINARY_REQ`, `CMD_SEND_PATH_DISCOVERY_REQ`, `CMD_SEND_CONTROL_DATA`, `CMD_SEND_ANON_REQ`, or `CMD_SEND_RAW_PACKET`.
* `src/comms/companion_bridge.cpp:275` and `:293` call `messageStoreMarkAllCompanionSent()`.
* `src/mesh/message_store.cpp:463` implements `messageStoreMarkAllCompanionSent()` by marking every unsent record.
* `src/comms/companion_bridge.cpp:235` copies `msg.sender_prefix`, and `src/mesh/companion_adapter.inc:117` fills it by sender name lookup.
* `src/comms/companion_bridge.cpp` writes `COMPANION_TXT_PLAIN` in message frames regardless of inbound CLI/signed type.
* `src/mesh/companion_adapter.inc:416` imports private keys without contact reset/reload.
* `src/mesh/companion_adapter.inc:227` rejects empty channel names in `setChannel()`.
* `src/comms/companion_bridge.cpp:1038` returns not found for `CMD_GET_ADVERT_PATH`.
* `src/mesh/companion_adapter.inc:651` returns empty self telemetry.
* `src/ui/screens/screen_contacts.cpp` and `src/ui/screens/screen_channels.cpp` build `meshcore://...name=%s...` URIs without URL encoding.

Selected upstream evidence:

* `/tmp/meshcore-audit/examples/companion_radio/MyMesh.h` defines `FIRMWARE_VER_CODE 13`.
* `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp` defines missing commands 25, 41, 50, 52, 55, 57, 65 and push codes 0x84, 0x88, 0x8C, 0x8D, 0x8E.
* `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp::queueMessage()` preserves text type and optional signed extra.
* `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp::onCommandDataRecv()` queues `TXT_TYPE_CLI_DATA`.
* `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp::onSignedMessageRecv()` queues `TXT_TYPE_SIGNED_PLAIN` with 4-byte extra.
* `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp::CMD_IMPORT_PRIVATE_KEY` resets and reloads contacts after identity import.
* `/tmp/meshcore-audit/examples/companion_radio/MyMesh.cpp::CMD_SEND_SELF_ADVERT` chooses flood-scoped send only when command byte 1 is `1`; otherwise it sends zero-hop.
* `/tmp/meshcore-audit/src/helpers/ArduinoSerialInterface.cpp` and `SerialWifiInterface.cpp` implement `<`/`>` framed serial/TCP companion transports.
* `/tmp/meshcore-audit/docs/qr_codes.md` says contact/channel `name` parameters are URL-encoded if needed.
