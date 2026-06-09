# Known Issues

This document tracks currently open known issues, bugs, and missing features in SigurdOS T-Deck firmware. All historically tracked issues that have been resolved are maintained in the Git history — check merged PRs and commit logs for the full record.

---

## Launcher Compatibility

### SigurdOS Launcher compatibility

[Launcher](https://github.com/bmorcelli/Launcher) is an ESP32 app launcher with explicit T-Deck support. SigurdOS can be installed and run under Launcher by flashing the `SigurdOS-tdeck-launcher.bin` merged artifact from the latest release.

**Current status:**
- ✅ **Phase 1/4 (PR #573):** Runtime Launcher detection, self-OTA gates, partition layout compatibility, release artifacts, documentation — **code complete, awaiting hardware test**
- 🔄 **Phase 2:** Boot/flash compatibility testing — blocked, needs hardware (see below)
- 🔄 **Phase 5:** Full regression matrix (T1–T14) — blocked on hardware
- ⏳ **Phase 3:** Warm-handoff keyboard timing hardening — scoping deferred
- ⏳ **Phase 6:** LauncherHub catalog listing — post-hardware verification

**Blocking issue:** Firmware testing requires a stable USB power supply on the test bench. The Pi-powered T-Deck is currently unreachable due to undervoltage (`throttled=0x50000`). A new power adapter is on order.

Standalone (non-Launcher) firmware is unaffected by any of these changes.

---

## How to Help

Pick any item from the list above and open a PR against the `dev` branch. See [`CONTRIBUTING.md`](./CONTRIBUTING.md) for the full contribution workflow.
