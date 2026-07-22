#!/usr/bin/env python3
"""Create fail-closed merged images and ESP Web Tools release assets."""

Import("env")

import hashlib as _hashlib
import json as _json
import os as _os
import shutil as _shutil
import subprocess as _subprocess
from datetime import datetime as _datetime, timezone as _timezone


_FLASH_MODE_HEADER_VALUES = {
    "qio": 0x00,
    "qout": 0x01,
    "dio": 0x02,
    "dout": 0x03,
}
_FLASH_CAPACITY = 16 * 1024 * 1024
_COMPONENT_NAMES = {
    0x0000: "bootloader",
    0x8000: "partitions",
    0xE000: "boot_app0",
}


def _sha256(path):
    digest = _hashlib.sha256()
    with open(path, "rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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
            ["git", "status", "--porcelain", "--untracked-files=normal"],
            cwd=cwd,
            check=True,
            stdout=_subprocess.PIPE,
            stderr=_subprocess.DEVNULL,
            text=True,
        )
    except (OSError, _subprocess.CalledProcessError):
        return True
    return bool(completed.stdout.strip())


def _source_timestamp(project_dir):
    raw = _os.environ.get("SOURCE_DATE_EPOCH")
    if raw is None:
        raw = _git(["show", "-s", "--format=%ct", "HEAD"], project_dir)
    try:
        epoch = int(raw)
    except (TypeError, ValueError) as exc:
        raise RuntimeError("SOURCE_DATE_EPOCH or the Git commit timestamp is required") from exc
    if epoch < 0:
        raise RuntimeError("SOURCE_DATE_EPOCH cannot be negative")
    return _datetime.fromtimestamp(epoch, _timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _assert_image_flash_mode(path, expected_mode, label):
    expected = _FLASH_MODE_HEADER_VALUES.get(expected_mode)
    if expected is None:
        raise RuntimeError(f"Unsupported configured flash mode: {expected_mode}")
    with open(path, "rb") as image:
        header = image.read(3)
    if len(header) != 3 or header[0] != 0xE9:
        raise RuntimeError(f"{label} has an invalid ESP image header: {path}")
    if header[2] != expected:
        raise RuntimeError(
            f"{label} flash mode mismatch: header=0x{header[2]:02x}, "
            f"expected {expected_mode} (0x{expected:02x})"
        )


def _flash_parts(build_env, firmware_bin):
    values = [
        build_env.subst(str(value))
        for value in build_env.Flatten(build_env.get("FLASH_EXTRA_IMAGES", []))
    ]
    if len(values) % 2:
        raise RuntimeError("FLASH_EXTRA_IMAGES must contain offset/path pairs")
    values.extend([build_env.subst("$ESP32_APP_OFFSET"), firmware_bin])
    parts = []
    for index in range(0, len(values), 2):
        raw_offset, path = values[index : index + 2]
        try:
            offset = int(raw_offset, 0)
        except ValueError as exc:
            raise RuntimeError(f"invalid flash offset: {raw_offset}") from exc
        if not _os.path.isfile(path):
            raise RuntimeError(f"missing mandatory merge input: {path}")
        parts.append((offset, _os.path.abspath(path)))

    parts.sort(key=lambda item: item[0])
    seen = set()
    for index, (offset, path) in enumerate(parts):
        if offset in seen:
            raise RuntimeError(f"duplicate flash offset 0x{offset:x}")
        seen.add(offset)
        end = offset + _os.path.getsize(path)
        if offset < 0 or end > _FLASH_CAPACITY:
            raise RuntimeError(f"flash part exceeds 16 MB capacity: {path}")
        if index and offset < parts[index - 1][0] + _os.path.getsize(parts[index - 1][1]):
            raise RuntimeError(f"flash parts overlap at 0x{offset:x}: {path}")
    return parts


def _component_parts(parts, app_offset):
    expected = dict(_COMPONENT_NAMES)
    expected[app_offset] = "firmware"
    by_offset = {offset: path for offset, path in parts}
    if set(by_offset) != set(expected):
        found = ", ".join(f"0x{value:x}" for value in sorted(by_offset))
        wanted = ", ".join(f"0x{value:x}" for value in sorted(expected))
        raise RuntimeError(f"unexpected flash layout ({found}); expected {wanted}")
    return [(expected[offset], offset, by_offset[offset]) for offset in sorted(expected)]


def _assert_embedded_app(merged_bin, firmware_bin, app_offset):
    with open(firmware_bin, "rb") as app:
        expected = app.read()
    with open(merged_bin, "rb") as merged:
        merged.seek(app_offset)
        actual = merged.read(len(expected))
    if actual != expected:
        raise RuntimeError("merged image does not contain the newly built app at its app offset")


def merge_bin_action(target, source, env):
    board_config = env.BoardConfig()
    build_dir = env.subst("$BUILD_DIR")
    merged_bin = env.subst("$BUILD_DIR/${PROGNAME}-merged.bin")
    firmware_bin = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    project_dir = env.subst("$PROJECT_DIR")

    # Capture source state before generated, tracked web-flasher assets are replaced.
    source_dirty = _git_dirty(project_dir)
    git_sha = _git(["rev-parse", "HEAD"], project_dir)
    meshcore_sha = _git(
        ["rev-parse", "HEAD"], _os.path.join(project_dir, "lib", "meshcore")
    )
    version = _git(["describe", "--tags", "--dirty", "--always"], project_dir)
    if _os.path.lexists(merged_bin):
        _os.remove(merged_bin)

    parts = _flash_parts(env, firmware_bin)
    app_offset = int(env.subst("$ESP32_APP_OFFSET"), 0)
    components = _component_parts(parts, app_offset)
    bootloader = next(path for name, _, path in components if name == "bootloader")

    expected_flash_mode = env.GetProjectOption(
        "board_build.flash_mode",
        board_config.get("build.flash_mode", "dio"),
    ).lower()
    _assert_image_flash_mode(bootloader, expected_flash_mode, "bootloader")

    python_exe = env.subst("$PYTHONEXE")
    esptool = env.subst("$OBJCOPY")
    flash_frequency = env.subst("${__get_board_f_flash(__env__)}")
    flash_size = board_config.get("upload.flash_size", "16MB")
    command = [
        python_exe,
        esptool,
        "--chip",
        board_config.get("build.mcu", "esp32s3"),
        "merge_bin",
        "-o",
        merged_bin,
        "--flash_mode",
        "keep",
        "--flash_freq",
        flash_frequency,
        "--flash_size",
        flash_size,
    ]
    for offset, path in parts:
        command.extend([hex(offset), path])

    print(f"SigurdOS: merging firmware ({board_config.get('build.mcu', 'esp32s3')})...")
    try:
        _subprocess.run(command, check=True)
    except (OSError, _subprocess.CalledProcessError) as exc:
        raise RuntimeError("esptool merge_bin failed") from exc
    if not _os.path.isfile(merged_bin):
        raise RuntimeError("esptool reported success without producing the merged image")
    _assert_image_flash_mode(merged_bin, expected_flash_mode, "merged image")
    _assert_embedded_app(merged_bin, firmware_bin, app_offset)
    print(f"SigurdOS: merged -> firmware-merged.bin ({_os.path.getsize(merged_bin):,} bytes)")

    launcher_bin = _os.path.join(build_dir, "SigurdOS-tdeck-launcher.bin")
    _shutil.copy2(merged_bin, launcher_bin)
    if _sha256(launcher_bin) != _sha256(merged_bin):
        raise RuntimeError("Launcher copy does not match the merged image")
    print(
        "SigurdOS: launcher -> SigurdOS-tdeck-launcher.bin "
        f"({_os.path.getsize(launcher_bin):,} bytes)"
    )

    web_dir = _os.path.join(project_dir, "webflasher")
    _os.makedirs(web_dir, exist_ok=True)
    artifact_sources = {
        **{name: (path, offset) for name, offset, path in components},
        "full": (merged_bin, 0),
        "launcher": (launcher_bin, 0),
    }
    artifact_metadata = {}
    for name, (source_path, offset) in artifact_sources.items():
        destination_name = f"sigurdos-tdeck-{name}.bin"
        destination = _os.path.join(web_dir, destination_name)
        _shutil.copy2(source_path, destination)
        if not _os.path.isfile(destination):
            raise RuntimeError(f"failed to create mandatory web artifact: {destination}")
        artifact_metadata[name] = {
            "file": destination_name,
            "size": _os.path.getsize(destination),
            "sha256": _sha256(destination),
            "offset": offset,
        }
        print(f"SigurdOS webflasher: {destination_name} ({_os.path.getsize(destination):,} bytes)")

    manifest = {
        "name": "SigurdOS T-Deck",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": [
                    {"path": artifact_metadata[name]["file"], "offset": offset}
                    for name, offset, _ in components
                ],
            }
        ],
    }
    metadata = {
        "schema_version": 1,
        "name": "SigurdOS T-Deck",
        "version": version,
        "board": "LilyGo T-Deck",
        "platformio_board": env.subst("$BOARD"),
        "mcu": board_config.get("build.mcu", "esp32s3"),
        "chip_family": "ESP32-S3",
        "git_sha": git_sha,
        "git_dirty": source_dirty,
        "meshcore_sha": meshcore_sha,
        "build_environment": env.subst("$PIOENV"),
        "partition_table": env.GetProjectOption(
            "board_build.partitions", "unknown"
        ),
        "source_timestamp_utc": _source_timestamp(project_dir),
        "flash_mode": "keep",
        "flash_size": flash_size,
        "artifacts": artifact_metadata,
    }
    for filename, payload in (
        ("manifest.json", manifest),
        ("build-metadata.json", metadata),
    ):
        path = _os.path.join(web_dir, filename)
        with open(path, "w", encoding="utf-8", newline="\n") as output:
            _json.dump(payload, output, indent=2, sort_keys=True)
            output.write("\n")
        if not _os.path.isfile(path):
            raise RuntimeError(f"failed to create mandatory release metadata: {path}")
    print("SigurdOS webflasher: ESP Web Tools manifest and build metadata written")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin_action)
