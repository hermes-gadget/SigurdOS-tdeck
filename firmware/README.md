# SigurdOS T-Deck Firmware Binaries

Pre-built firmware is published only as immutable, versioned assets on the
[GitHub Releases page](https://github.com/hermes-gadget/SigurdOS-tdeck/releases).
The unversioned binaries formerly tracked in this directory were removed because
they were a mismatched pair from the
[beta-0.1.30](https://github.com/hermes-gadget/SigurdOS-tdeck/releases/tag/beta-0.1.30)
and [beta-0.1.31](https://github.com/hermes-gadget/SigurdOS-tdeck/releases/tag/beta-0.1.31)
eras. Do not combine artifacts from different releases.

Pre-built firmware for the LilyGo T-Deck (ESP32-S3, 16 MB flash).

| File | Use |
|------|-----|
| `firmware-merged.bin` | **Full flash at 0x0** — recommended for standalone first install (bootloader + partitions + boot_app0 + firmware) |
| `SigurdOS-tdeck-launcher.bin` | Same bytes as merged — feed to [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) for install as a Launcher app. Published with each [tagged release](https://github.com/hermes-gadget/SigurdOS-tdeck/releases); local builds emit it as `webflasher/sigurdos-tdeck-launcher.bin` |
| `firmware.bin` | App update only — flash at 0x10000 (preserves bootloader/partitions) |

## Which file should I flash?

| Scenario | File | Offset |
|----------|------|--------|
| **First install / fresh device** | `firmware-merged.bin` from one tagged release | 0x0 |
| **Update firmware only** (keep settings + bootloader) | `firmware.bin` from the same tagged release | 0x10000 |
| **Web flasher** (e.g. flasher.sigurdos.dev) | `sigurdos-tdeck-full.bin` from `webflasher/` | auto |

**Use `firmware-merged.bin` for ESP32 flash tools (esptool, ESP Flash Download Tool, etc.) — it contains everything needed to boot in a single image.**

## How the merged binary is built — the `merge_bin` process

The merged binary is produced automatically by `scripts/merge_bin.py` as a PlatformIO
post-build action (configured in `platformio.ini` for the `SigurdOS_TDeck` environment).

After a successful firmware build, `merge_bin.py`:

1.  Uses `esptool.py merge_bin` to combine **bootloader**, **partition table**,
    **boot_app0**, and the **firmware app** into `${PROGNAME}-merged.bin`. Missing
    inputs, overlapping parts, merge failures, stale output, or an app-slice
    mismatch fail the PlatformIO build.
2.  Copies each component to `webflasher/` as separate files (for web-based flashers
    that download individual binaries):
    - `sigurdos-tdeck-bootloader.bin`
    - `sigurdos-tdeck-partitions.bin`
    - `sigurdos-tdeck-boot_app0.bin`
    - `sigurdos-tdeck-firmware.bin`
    - `sigurdos-tdeck-full.bin` (identical to release `firmware-merged.bin`)
    - `sigurdos-tdeck-launcher.bin` (identical to release `firmware-merged.bin` — the Launcher install artifact)
3.  Generates a standard ESP Web Tools `webflasher/manifest.json` with the
    `ESP32-S3` component paths and numeric flash offsets.
4.  Writes provenance, deterministic source timestamp, component sizes, and
    SHA-256 checksums separately to `webflasher/build-metadata.json`.

The canonical PlatformIO environment builds the ESP32-S3 bootloader for DIO
flash mode at 80 MHz with 16 MB flash. The merge preserves that header instead
of applying the board JSON's QIO default.

## Where to find the files

| Path | Contents |
|------|----------|
| Tagged release: `firmware-merged.bin` | **Single image** — flash at 0x0 with esptool |
| Tagged release: `firmware.bin` | App-only update — flash at 0x10000 |
| Local build: `.pio/build/SigurdOS_TDeck/firmware-merged.bin` | Locally built single image |
| `webflasher/sigurdos-tdeck-full.bin` | Same as `firmware-merged.bin` — for web flashers |
| `webflasher/sigurdos-tdeck-*.bin` | Individual components — for web-based or custom flashing |
| `webflasher/manifest.json` | ESP Web Tools install manifest |
| `webflasher/build-metadata.json` | Build provenance, component sizes, offsets, and SHA-256 hashes |

## Flash with esptool (no PlatformIO needed)

```bash
# Install esptool if you don't have it
pip install esptool

# Full flash (first install)
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  --before default_reset --after hard_reset write_flash \
  0x0 firmware-merged.bin

# App update only (keep settings)
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x10000 firmware.bin
```

Do not pass `--flash_mode qio` when flashing the merged image. Its bootloader
header is DIO; forcing QIO can make affected T-Deck units boot-loop.

## Flash with PlatformIO

```bash
pio run -e SigurdOS_TDeck -t upload
```

## Flash with web flasher (flasher.sigurdos.dev)

Use the **Custom Firmware** option and upload `webflasher/sigurdos-tdeck-full.bin`.  
The flasher will flash the merged binary at offset 0x0.

## What's in the merged binary

| Offset | Component |
|--------|-----------|
| 0x0000 | Bootloader |
| 0x8000 | Partition table |
| 0xe000 | Boot app0 |
| 0x10000 | SigurdOS firmware |

Flash the merged binary at offset 0x0 — it contains everything needed to boot.

---

## Install via bmorcelli/Launcher

SigurdOS can be installed as an app under [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) (v2.7.2+).

### Prerequisites

- A T-Deck already running Launcher (v2.7.2 or newer)
- The file `SigurdOS-tdeck-launcher.bin` from a specific [tagged release](https://github.com/hermes-gadget/SigurdOS-tdeck/releases). Use the tag shown on that release; prereleases are not resolved by GitHub's stable-release alias.

### Installation

Feed `SigurdOS-tdeck-launcher.bin` to Launcher via:

- **SD card** — copy the file to FAT32 SD, insert, use Launcher's SD install
- **WebUI** — upload through Launcher's browser interface
- **Direct URL** (OTA) — enter the versioned release asset URL in Launcher's online installer, replacing `<tag>` with the exact release tag:
  `https://github.com/hermes-gadget/SigurdOS-tdeck/releases/download/<tag>/SigurdOS-tdeck-launcher.bin`

Launcher detects the embedded partition table, creates a SPIFFS partition for persistence, and boots SigurdOS.

### Important warnings

| Issue | What happens | Mitigation |
|-------|-------------|------------|
| **App-only (`firmware.bin`) loses persistence** | If you feed `firmware.bin` (app-only, no partition table) to Launcher, SPIFFS is not created — mesh identity regenerates on every boot, contacts/channels never persist | Always use `SigurdOS-tdeck-launcher.bin` (or `firmware-merged.bin`) for Launcher installs |
| **Merged image overwrites Launcher** | Flashing `firmware-merged.bin` at 0x0 with esptool replaces Launcher's bootloader and partition table — reflash Launcher to recover | Only feed the merged image **to Launcher's installer**, not esptool, if you want to keep Launcher |
| **Self-OTA disabled under Launcher** | WiFi AP OTA and GitHub OTA are automatically detected and refuse to start when running under Launcher (preventing flash corruption of co-installed apps) | Update SigurdOS through Launcher's own update mechanism |
| **Settings reset on mode switch** | Switching between standalone and Launcher-installed modes resets NVS preferences and SPIFFS identity — onboarding re-runs, radio TX stays safely gated | Back up your identity/contacts before switching install methods |

### How it works

`SigurdOS-tdeck-launcher.bin` is byte-identical to `firmware-merged.bin` (bootloader + partition table + app). Launcher uses the embedded partition table as a manifest to create the app partition and a 1 MB SPIFFS partition at runtime. The bootloader inside our merged image is never flashed — Launcher uses its own custom bootloader to return to Launcher at power-on.

The firmware detects it is running under Launcher by probing for Launcher's resident `test`-subtype app partition, which never exists in the standard standalone partition layout. When detected:
- Self-OTA features are disabled with an on-screen explanation
- Boot-time diagnostics provide targeted advice if SPIFFS mount fails

### Switching back to standalone

To return to standalone operation:
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 firmware-merged.bin
```

This replaces Launcher entirely with standalone SigurdOS. Your mesh identity will regenerate (it's stored in a differently-located SPIFFS), and NVS settings will reset. Re-onboard via the setup wizard and reconfigure your radio preferences.
