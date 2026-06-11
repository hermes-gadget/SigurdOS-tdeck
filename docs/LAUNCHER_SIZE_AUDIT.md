# Launcher Firmware Size Audit

Measured on 2026-06-11 from `dev` commit `104eb29` with:

```bash
pio run -e SigurdOS_TDeck
python3 scripts/audit_launcher_artifact.py .pio/build/SigurdOS_TDeck/firmware-merged.bin
```

This completes O3 from `docs/LAUNCHER_ROADMAP.md`: "Shrink-audit of the app image".

## Current Size

| Artifact | Size | Notes |
|---|---:|---|
| `.pio/build/SigurdOS_TDeck/firmware.bin` | 2,570,992 bytes / 2.452 MiB | ESP32-S3 app image that Launcher extracts from the merged file. |
| `.pio/build/SigurdOS_TDeck/firmware-merged.bin` | 2,636,528 bytes / 2.514 MiB | Standalone full-flash image. |
| `.pio/build/SigurdOS_TDeck/SigurdOS-tdeck-launcher.bin` | 2,636,528 bytes / 2.514 MiB | Byte-identical Launcher-named copy of the merged image. |
| Launcher app allocation | 2,621,440 bytes / 2.500 MiB | `firmware.bin` rounded up to Launcher's 64 KiB app-partition alignment. |

The release build reports:

```text
RAM:   [====      ]  40.7% (used 133504 bytes from 327680 bytes)
Flash: [====      ]  39.2% (used 2570581 bytes from 6553600 bytes)
```

## Headroom

| Constraint | Available Space | Current Headroom |
|---|---:|---:|
| Standalone `app0` partition (`0x640000`) | 6,553,600 bytes / 6.250 MiB | 3,982,608 bytes / 3.798 MiB |
| Hypothetical 3 MiB Launcher app slot | 3,145,728 bytes / 3.000 MiB | 574,736 bytes / 0.548 MiB |
| Hypothetical 4 MiB Launcher app slot | 4,194,304 bytes / 4.000 MiB | 1,623,312 bytes / 1.548 MiB |

`scripts/audit_launcher_artifact.py` confirms that the merged artifact remains a valid Launcher install source. The app image fits comfortably, and the embedded SPIFFS entry causes Launcher to create the 1 MiB runtime SPIFFS partition documented in the roadmap.

## Linked-Size Observations

The largest linked symbols are normal feature payloads rather than Launcher-specific overhead:

- Font glyph bitmaps in flash rodata.
- LVGL software draw/blend routines.
- TLS/HTTP/printing support used by GitHub OTA, WiFi OTA, WebServer, and diagnostics.
- Mesh crypto routines from MeshCore/Crypto.
- BLE/WiFi support for the companion and networking features.

Large archive files such as `liblvgl.a`, `libRadioLib.a`, `libBLE.a`, and `libFrameworkArduino.a` are useful signals for future investigation, but their archive sizes are not the same as final linked flash usage.

## Conclusion

No shrink work is required for Launcher compatibility right now.

The Launcher artifact is about 2.5 MiB, the aligned Launcher app allocation is 2.5 MiB, and even a conservative 3 MiB app slot has roughly 0.55 MiB of headroom. Shrinking the image would make installs faster, but the current size does not block Launcher install, SPIFFS creation, standalone OTA slots, or release artifact hosting.

Do not remove features, split build environments, or prune libraries solely for Launcher support until a measured threshold is crossed.

Revisit size optimization if any of these become true:

- `firmware.bin` exceeds 3.0 MiB.
- `SigurdOS-tdeck-launcher.bin` exceeds 4.0 MiB.
- Launcher layout selection cannot keep at least a 1 MiB SPIFFS partition on 16 MB T-Deck devices.
- A future required feature adds a large dependency such as a second graphics stack or a second BLE implementation.

Potential future shrink candidates, if needed, should be measured in dedicated PRs:

- LVGL feature/font configuration.
- LovyanGFX and RadioLib module reachability.
- BLE companion feature gating.
- WiFi/WebServer/HTTPS OTA feature gating.
- Emoji and extended Latin font coverage.
