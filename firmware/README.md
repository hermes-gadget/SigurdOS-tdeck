# SigurdOS T-Deck Firmware Binaries

> **Legacy tracked artifacts:** `sigurdos-tdeck.bin` and
> `sigurdos-tdeck-merged.bin` in this directory identify as
> `SlopOS beta-0.1.30`. They are retained only for historical recovery and are
> not current release outputs. Install a versioned artifact from GitHub
> Releases or rebuild from the desired commit.

Pre-built firmware for the LilyGo T-Deck (ESP32-S3, 16 MB flash).

| File | Use |
|------|-----|
| `sigurdos-tdeck-merged.bin` | **Full flash at 0x0** — recommended for standalone first install (bootloader + partitions + boot_app0 + firmware) |
| `SigurdOS-tdeck-launcher.bin` | Same bytes as merged — feed to [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) for install as a Launcher app. Published as a [release asset](https://github.com/hermes-gadget/SigurdOS-tdeck/releases/latest); local builds emit it as `webflasher/sigurdos-tdeck-launcher.bin` |
| `sigurdos-tdeck.bin` | App update only — flash at 0x10000 (preserves bootloader/partitions) |

## Which file should I flash?

| Scenario | File | Offset |
|----------|------|--------|
| **First install / fresh device** | `sigurdos-tdeck-merged.bin` | 0x0 |
| **Update firmware only** (keep settings + bootloader) | `sigurdos-tdeck.bin` | 0x10000 |
| **Web flasher** (e.g. flasher.sigurdos.dev) | `sigurdos-tdeck-full.bin` from `webflasher/` | auto |

**Use `sigurdos-tdeck-merged.bin` for ESP32 flash tools (esptool, ESP Flash Download Tool, etc.) — it contains everything needed to boot in a single image.**

## How the merged binary is built — the `merge_bin` process

The merged binary is produced automatically by `scripts/merge_bin.py` as a PlatformIO
post-build action (configured in `platformio.ini` for the `SigurdOS_TDeck` environment).

After a successful firmware build, `merge_bin.py`:

1.  Uses `esptool.py merge_bin` to combine **bootloader**, **partition table**,
    **boot_app0**, and the **firmware app** into one binary named `sigurdos-tdeck-merged.bin`.
2.  Copies each component to `webflasher/` as separate files (for web-based flashers
    that download individual binaries):
    - `sigurdos-tdeck-bootloader.bin`
    - `sigurdos-tdeck-partitions.bin`
    - `sigurdos-tdeck-boot_app0.bin`
    - `sigurdos-tdeck-firmware.bin`
    - `sigurdos-tdeck-full.bin` (identical to `sigurdos-tdeck-merged.bin`)
    - `sigurdos-tdeck-launcher.bin` (identical to `sigurdos-tdeck-merged.bin` — the Launcher install artifact)
3.  Generates `webflasher/manifest.json` containing firmware version, Git SHA,
    artifact SHA-256 checksums, flash offsets, and build metadata.

The merge uses the ESP32-S3 flash configuration (QIO mode, 80 MHz, 16 MB) read
from the board definition in `platformio.ini`.

## Where to find the files

| Path | Contents |
|------|----------|
| `firmware/sigurdos-tdeck-merged.bin` | **Single image** — flash at 0x0 with esptool |
| `firmware/sigurdos-tdeck.bin` | App-only update — flash at 0x10000 |
| `webflasher/sigurdos-tdeck-full.bin` | Same as `sigurdos-tdeck-merged.bin` — for web flashers |
| `webflasher/sigurdos-tdeck-*.bin` | Individual components — for web-based or custom flashing |
| `webflasher/manifest.json` | Build manifest (version, hashes, offsets, metadata) |

## Flash with esptool (no PlatformIO needed)

```bash
# Install esptool if you don't have it
pip install esptool

# Full flash (first install)
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode qio --flash_freq 80m --flash_size 16MB \
  0x0 sigurdos-tdeck-merged.bin

# App update only (keep settings)
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x10000 sigurdos-tdeck.bin
```

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
- The file `SigurdOS-tdeck-launcher.bin` from the [latest release](https://github.com/hermes-gadget/SigurdOS-tdeck/releases/latest)

### Installation

Feed `SigurdOS-tdeck-launcher.bin` to Launcher via:

- **SD card** — copy the file to FAT32 SD, insert, use Launcher's SD install
- **WebUI** — upload through Launcher's browser interface
- **Direct URL** (OTA) — enter the release asset URL in Launcher's online installer:
  `https://github.com/hermes-gadget/SigurdOS-tdeck/releases/latest/download/SigurdOS-tdeck-launcher.bin`

Launcher detects the embedded partition table, creates a SPIFFS partition for persistence, and boots SigurdOS.

### Important warnings

| Issue | What happens | Mitigation |
|-------|-------------|------------|
| **App-only (`firmware.bin`) loses persistence** | If you feed `firmware.bin` (app-only, no partition table) to Launcher, SPIFFS is not created — mesh identity regenerates on every boot, contacts/channels never persist | Always use `SigurdOS-tdeck-launcher.bin` (or `sigurdos-tdeck-merged.bin`) for Launcher installs |
| **Merged image overwrites Launcher** | Flashing `sigurdos-tdeck-merged.bin` at 0x0 with esptool replaces Launcher's bootloader and partition table — reflash Launcher to recover | Only feed the merged image **to Launcher's installer**, not esptool, if you want to keep Launcher |
| **Self-OTA disabled under Launcher** | WiFi AP OTA and GitHub OTA are automatically detected and refuse to start when running under Launcher (preventing flash corruption of co-installed apps) | Update SigurdOS through Launcher's own update mechanism |
| **Settings reset on mode switch** | Switching between standalone and Launcher-installed modes resets NVS preferences and SPIFFS identity — onboarding re-runs, radio TX stays safely gated | Back up your identity/contacts before switching install methods |

### How it works

`SigurdOS-tdeck-launcher.bin` is byte-identical to `sigurdos-tdeck-merged.bin` (bootloader + partition table + app). Launcher uses the embedded partition table as a manifest to create the app partition and a 1 MB SPIFFS partition at runtime. The bootloader inside our merged image is never flashed — Launcher uses its own custom bootloader to return to Launcher at power-on.

The firmware detects it is running under Launcher by probing for Launcher's resident `test`-subtype app partition, which never exists in the standard standalone partition layout. When detected:
- Self-OTA features are disabled with an on-screen explanation
- Boot-time diagnostics provide targeted advice if SPIFFS mount fails

### Switching back to standalone

To return to standalone operation:
```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 sigurdos-tdeck-merged.bin
```

This replaces Launcher entirely with standalone SigurdOS. Your mesh identity will regenerate (it's stored in a differently-located SPIFFS), and NVS settings will reset. Re-onboard via the setup wizard and reconfigure your radio preferences.
