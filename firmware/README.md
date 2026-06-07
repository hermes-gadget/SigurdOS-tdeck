# SigurdOS T-Deck Firmware Binaries

Pre-built firmware for the LilyGo T-Deck (ESP32-S3, 16 MB flash).

| File | Use |
|------|-----|
| `firmware-merged.bin` | **Full flash at 0x0** — recommended for first install (bootloader + partitions + boot_app0 + firmware) |
| `sigurdos-tdeck-merged.bin` | Full flash at 0x0 — legacy merged build (older format) |
| `sigurdos-tdeck.bin` | App update only — flash at 0x10000 (preserves bootloader/partitions) |

## Which file should I flash?

| Scenario | File | Offset |
|----------|------|--------|
| **First install / fresh device** | `firmware-merged.bin` | 0x0 |
| **Update firmware only** (keep settings + bootloader) | `sigurdos-tdeck.bin` | 0x10000 |
| **Web flasher** (e.g. flasher.sigurdos.dev) | `sigurdos-tdeck-full.bin` from `webflasher/` | auto |

**Use `firmware-merged.bin` for ESP32 flash tools (esptool, ESP Flash Download Tool, etc.) — it contains everything needed to boot in a single image.**

## How the merged binary is built — the `merge_bin` process

The merged binary is produced automatically by `scripts/merge_bin.py` as a PlatformIO
post-build action (configured in `platformio.ini` for the `SigurdOS_TDeck` environment).

After a successful firmware build, `merge_bin.py`:

1.  Uses `esptool.py merge_bin` to combine **bootloader**, **partition table**,
    **boot_app0**, and the **firmware app** into one binary named `firmware-merged.bin`.
2.  Copies each component to `webflasher/` as separate files (for web-based flashers
    that download individual binaries):
    - `sigurdos-tdeck-bootloader.bin`
    - `sigurdos-tdeck-partitions.bin`
    - `sigurdos-tdeck-boot_app0.bin`
    - `sigurdos-tdeck-firmware.bin`
    - `sigurdos-tdeck-full.bin` (identical to `firmware-merged.bin`)
3.  Generates `webflasher/manifest.json` containing firmware version, Git SHA,
    artifact SHA-256 checksums, flash offsets, and build metadata.

The merge uses the ESP32-S3 flash configuration (QIO mode, 80 MHz, 16 MB) read
from the board definition in `platformio.ini`.

## Where to find the files

| Path | Contents |
|------|----------|
| `firmware/firmware-merged.bin` | **Single image** — flash at 0x0 with esptool |
| `firmware/sigurdos-tdeck.bin` | App-only update — flash at 0x10000 |
| `webflasher/sigurdos-tdeck-full.bin` | Same as `firmware-merged.bin` — for web flashers |
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
  0x0 firmware-merged.bin

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
