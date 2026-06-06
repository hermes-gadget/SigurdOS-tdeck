#!/usr/bin/env python3
"""Post-build: merge bootloader + partitions + firmware into a single flashable image.
Patterned after MeshCore merge-bin.py — reads flash config from board JSON."""

Import("env")

def merge_bin_action(target, source, env):
    board_config = env.BoardConfig()
    build_dir = env.subst("$BUILD_DIR")
    merged_bin = env.subst("$BUILD_DIR/${PROGNAME}-merged.bin")
    firmware_bin = env.subst("$BUILD_DIR/${PROGNAME}.bin")

    import os as _os
    bootloader = f"{build_dir}/bootloader.bin"
    partitions = f"{build_dir}/partitions.bin"

    for f in [bootloader, partitions, firmware_bin]:
        if not _os.path.isfile(f):
            print(f"SigurdOS: skipping merge - missing: {f}")
            return

    flash_images = [
        *env.Flatten(env.get("FLASH_EXTRA_IMAGES", [])),
        "$ESP32_APP_OFFSET",
        firmware_bin,
    ]

    merge_cmd = " ".join([
        '"$PYTHONEXE"',
        '"$OBJCOPY"',
        "--chip",
        board_config.get("build.mcu", "esp32s3"),
        "merge_bin",
        "-o", merged_bin,
        "--flash_mode",
        "keep",  # Preserve bootloader's DIO mode — ROM requires DIO at boot
        "--flash_freq",
        "${__get_board_f_flash(__env__)}",
        "--flash_size",
        board_config.get("upload.flash_size", "16MB"),
        *flash_images,
    ])

    print(f"SigurdOS: merging firmware ({board_config.get('build.mcu', 'esp32s3')})...")
    env.Execute(merge_cmd)

    if _os.path.isfile(merged_bin):
        print(f"SigurdOS: merged -> firmware-merged.bin ({_os.path.getsize(merged_bin):,} bytes)")

    # ── Web flasher manifest ──────────────────────────────
    import hashlib as _hashlib
    import json as _json
    import subprocess as _subprocess
    from datetime import datetime, timezone as _timezone

    proj_dir = env.subst("$PROJECT_DIR")
    web_dir = _os.path.join(proj_dir, "webflasher")
    _os.makedirs(web_dir, exist_ok=True)

    mcu = board_config.get("build.mcu", "esp32s3")
    flash_mode = "keep"  # Don't let esptool.js rewrite bootloader header — ESP32-S3 ROM boots DIO
    flash_size = board_config.get("upload.flash_size", "16MB")

    offsets = {
        "bootloader": "0x0000",
        "partitions": "0x8000",
        "boot_app0": "0xe000",
        "firmware":  "0x10000",
    }

    boot_app0_src = _os.path.join(_os.path.dirname(build_dir),
        ".pio/build/SigurdOS_TDeck/boot_app0.bin")
    # boot_app0 comes from framework, find it
    for root, dirs, files in _os.walk(_os.path.join(env.subst("$PROJECT_PACKAGES_DIR"), "framework-arduinoespressif32")):
        if "boot_app0.bin" in files:
            boot_app0_src = _os.path.join(root, "boot_app0.bin")
            break

    artifacts = {
        "bootloader": bootloader,
        "partitions": partitions,
        "boot_app0": boot_app0_src,
        "firmware":  firmware_bin,
        "full":      merged_bin,
    }

    def _git(args, cwd):
        try:
            completed = _subprocess.run(
                ["git", *args],
                cwd=cwd,
                check=True,
                stdout=_subprocess.PIPE,
                stderr=_subprocess.DEVNULL,
                text=True,
            )
        except (OSError, _subprocess.CalledProcessError):
            return "unknown"
        value = completed.stdout.strip()
        return value if value else "unknown"

    def _git_dirty(cwd):
        try:
            completed = _subprocess.run(
                ["git", "status", "--porcelain", "--untracked-files=no"],
                cwd=cwd,
                check=True,
                stdout=_subprocess.PIPE,
                stderr=_subprocess.DEVNULL,
                text=True,
            )
        except (OSError, _subprocess.CalledProcessError):
            return True
        return bool(completed.stdout.strip())

    def _sha256(path):
        digest = _hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    # Read version from tdeck_pins.h
    pins_h = _os.path.join(proj_dir, "src", "hal", "tdeck_pins.h")
    version = "unknown"
    try:
        with open(pins_h, encoding="utf-8", errors="replace") as f:
            for line in f:
                if "SIGURDOS_VERSION" in line and '"' in line:
                    version = line.split('"')[1]
                    break
    except: pass

    git_sha = _git(["rev-parse", "--short=12", "HEAD"], proj_dir)
    meshcore_sha = _git(["rev-parse", "--short=12", "HEAD"], _os.path.join(proj_dir, "lib", "meshcore"))

    manifest = {
        "name": "SigurdOS T-Deck",
        "board": "LilyGo T-Deck",
        "platformio_board": env.subst("$BOARD"),
        "mcu": mcu,
        "firmware_version": version,
        "git_sha": git_sha,
        "git_dirty": _git_dirty(proj_dir),
        "meshcore_sha": meshcore_sha,
        "build_environment": env.subst("$PIOENV"),
        "partition_table": env.GetProjectOption("board_build.partitions", "unknown"),
        "built_at_utc": datetime.now(_timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "chip_family": "ESP32-S3",
        "flash_mode": flash_mode,
        "flash_size": flash_size,
        "artifacts": {},
        "flash_offsets": offsets,
    }

    for name, src in artifacts.items():
        if _os.path.isfile(src):
            dst_name = f"sigurdos-tdeck-{name}.bin"
            dst = _os.path.join(web_dir, dst_name)
            with open(src, "rb") as fsrc:
                with open(dst, "wb") as fdst:
                    fdst.write(fsrc.read())
            size = _os.path.getsize(dst)
            manifest["artifacts"][name] = {
                "file": dst_name, "size": size,
                "sha256": _sha256(dst),
                "offset": offsets.get(name, "0x0")
            }
            print(f"SigurdOS webflasher: {dst_name} ({size:,} bytes)")
        else:
            print(f"SigurdOS webflasher: SKIP {name} - not found")

    manifest_path = _os.path.join(web_dir, "manifest.json")
    with open(manifest_path, "w") as f:
        _json.dump(manifest, f, indent=2)
    print(f"SigurdOS webflasher: manifest written")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin_action)
