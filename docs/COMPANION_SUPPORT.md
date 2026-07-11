# Companion command support

SigurdOS T-Deck implements the companion commands used for normal setup,
contacts, channels, text messaging, offline sync, status, telemetry, tracing,
signing, and configuration. Recognition of a command identifier does not imply
that its operation is supported.

This matrix describes the pinned MeshCore protocol at submodule commit
`60ea4a91bf14363e837037a79ce1bff7fa37483f`.

| Command family | Status | Notes |
|---|---|---|
| App/device query and time | Supported | Includes app start, device query, connection, time, battery/storage, and stats |
| Contacts and adverts | Supported | Import/export/update/remove/share, advert path/name/location, self advert, and path reset |
| Channels and text messages | Supported | Channel configuration/data/text, direct text, login, path discovery, and offline message sync |
| Radio, tuning, flood scope, and custom variables | Supported | Subject to T-Deck radio-region and TX-safety gates |
| Status, telemetry, and trace | Supported | Request and response push paths are implemented |
| Identity import/export and signing | Supported | Factory reset remains guarded by the authenticated protocol contract |
| `CMD_SEND_RAW_DATA` | Unsupported | Returns `ERR_CODE_UNSUPPORTED_CMD` |
| `CMD_SEND_BINARY_REQ` | Unsupported | Returns `ERR_CODE_UNSUPPORTED_CMD` |
| `CMD_SEND_CONTROL_DATA` | Unsupported | Returns `ERR_CODE_UNSUPPORTED_CMD` |
| `CMD_SEND_ANON_REQ` | Unsupported | Returns `ERR_CODE_UNSUPPORTED_CMD` |
| `CMD_SEND_RAW_PACKET` | Unsupported | Returns `ERR_CODE_UNSUPPORTED_CMD`; arbitrary packet injection is not exposed |

The unsupported raw/control families require separate protocol semantics,
authorization, packet-bound validation, and interoperability tests before they
can be enabled safely. Clients must handle `ERR_CODE_UNSUPPORTED_CMD` and must
not infer full companion-radio parity from the advertised BLE service.

Release interoperability should cover the official Android and iOS apps plus a
stock MeshCore companion radio. Core text messaging compatibility is broader
than the optional command families listed as unsupported above.
