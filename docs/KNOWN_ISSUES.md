# Known Issues

This document tracks currently open known issues, bugs, and missing features in SigurdOS T-Deck firmware. All historically tracked issues that have been resolved are maintained in the Git history — check merged PRs and commit logs for the full record.

---

## Companion transport and interoperability limits

- **Transport availability:** BLE NUS is enabled in the normal firmware build.
  USB CDC is available in the mutually exclusive
  `SigurdOS_TDeck_companion_usb` build. Hardware-UART companion mode is absent
  because GPS occupies UART pins 43/44, and Wi-Fi TCP companion mode is absent
  because the protocol has no transport-level authentication policy suitable
  for a LAN listener.
- **Raw RX diagnostics:** `PUSH_CODE_LOG_RX_DATA` is intentionally not emitted.
  It would expose raw received RF diagnostics to every paired companion and is
  disabled as a privacy decision.
- **Best-effort pushes:** channel-data, raw, control, trace, status, telemetry,
  login, advert, and path-update pushes are not durable. Channel-data uses the
  bounded volatile page but cannot evict or overtake durable direct text.
- **Device-authored history:** the official protocol has no push code for a
  message composed on the T-Deck. Such messages transmit normally but do not
  appear as authored messages in the official phone app.
- **Protocol version:** the firmware continues to advertise protocol code 12.
  Code 13 will not be advertised until its path-discovery, scope, login, and
  contact behaviours have complete interoperability evidence against a current
  stock companion.
The MeshCore submodule remains pinned. It contains local anonymous-contact
fixes that are not a fast-forward match for current upstream; any future update
must reconcile contact allocation/persistence indices and revalidate room
connection keepalives.

---

## Launcher Compatibility

### bmorcelli/Launcher v2.7.2 compatibility status

SigurdOS can now be installed as a Launcher app. See [`firmware/README.md`](../firmware/README.md) for the full installation guide and caveats.

| Capability | Implemented | Hardware-verified | Remaining / externally blocked |
|---|---|---|---|
| Launcher-named merged install artifact | Yes | No current evidence recorded | Run SD/WebUI/direct-URL install matrix with a tagged artifact |
| Runtime Launcher detection | Yes; dual partition signals have native coverage | No current end-to-end evidence recorded | Revalidate when claiming a newer Launcher version |
| Self-OTA gate and app-only persistence diagnostic | Yes | No current end-to-end evidence recorded | T4–T13 in the Launcher validation matrix |
| Keyboard warm-handoff hardening | Yes; bounded probe retry and explicit `CMD_MODE_KEY` (`0x04`) ASCII mode | No | T4/T9 on physical hardware; production never sends raw-mode `0x03` |
| LauncherHub catalog listing | Outside this repository | No | Externally coordinated by issue #615 |
| Reboot-to-Launcher action | No | No | Hardware/API investigation in issue #616 |

Code-complete does not mean hardware-verified. Until the end-to-end handoff
matrix is recorded, describe Launcher support as implemented but experimental.

---

## How to Help

Pick any item from the list above and open a PR against the `dev` branch. See [`CONTRIBUTING.md`](../CONTRIBUTING.md) for the full contribution workflow.
