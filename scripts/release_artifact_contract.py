"""Validation contract for the complete SigurdOS release artifact directory."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from audit_launcher_artifact import audit


COMPONENT_OFFSETS = {
    "sigurdos-tdeck-bootloader.bin": 0x0000,
    "sigurdos-tdeck-partitions.bin": 0x8000,
    "sigurdos-tdeck-boot_app0.bin": 0xE000,
    "sigurdos-tdeck-firmware.bin": 0x10000,
}
GENERATED_WEB_OFFSETS = {
    **COMPONENT_OFFSETS,
    "sigurdos-tdeck-full.bin": 0,
    "sigurdos-tdeck-launcher.bin": 0,
}
FULL_IMAGE_NAMES = (
    "firmware-merged.bin",
    "SigurdOS-tdeck-launcher.bin",
    "sigurdos-tdeck-full.bin",
    "sigurdos-tdeck-launcher.bin",
)
REQUIRED_RELEASE_ARTIFACTS = frozenset(
    {
        "firmware.bin",
        "firmware-debug.bin",
        "manifest.json",
        "build-metadata.json",
        *COMPONENT_OFFSETS,
        *FULL_IMAGE_NAMES,
    }
)


class ReleaseArtifactError(ValueError):
    """Raised when a release directory does not close over its manifest."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _load_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ReleaseArtifactError(f"{path.name}: invalid JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise ReleaseArtifactError(f"{path.name}: top-level value must be an object")
    return value


def validate_manifest(path: Path, expected_version: str | None = None) -> dict:
    manifest = _load_json(path)
    if set(manifest) != {"name", "version", "new_install_prompt_erase", "builds"}:
        raise ReleaseArtifactError("manifest does not match the ESP Web Tools install schema")
    if manifest.get("name") != "SigurdOS T-Deck":
        raise ReleaseArtifactError("manifest name must be SigurdOS T-Deck")
    version = manifest.get("version")
    if not isinstance(version, str) or not version:
        raise ReleaseArtifactError("manifest version must be non-empty")
    if expected_version is not None and version != expected_version:
        raise ReleaseArtifactError(
            f"manifest version {version!r} does not match {expected_version!r}"
        )
    if manifest.get("new_install_prompt_erase") is not True:
        raise ReleaseArtifactError("manifest must let users confirm new-install erase")
    builds = manifest.get("builds")
    if not isinstance(builds, list) or len(builds) != 1:
        raise ReleaseArtifactError("manifest must contain exactly one build")
    build = builds[0]
    if not isinstance(build, dict) or build.get("chipFamily") != "ESP32-S3":
        raise ReleaseArtifactError("manifest build must target ESP32-S3")
    parts = build.get("parts")
    if not isinstance(parts, list) or len(parts) != len(COMPONENT_OFFSETS):
        raise ReleaseArtifactError("manifest must contain the four canonical flash parts")
    actual = {}
    for part in parts:
        if not isinstance(part, dict) or set(part) != {"path", "offset"}:
            raise ReleaseArtifactError("each manifest part needs only path and offset")
        name = part.get("path")
        offset = part.get("offset")
        if not isinstance(name, str) or Path(name).name != name:
            raise ReleaseArtifactError("manifest part paths must be local filenames")
        if isinstance(offset, bool) or not isinstance(offset, int):
            raise ReleaseArtifactError("manifest part offsets must be numeric integers")
        if name in actual:
            raise ReleaseArtifactError(f"manifest repeats part {name}")
        actual[name] = offset
    if actual != COMPONENT_OFFSETS:
        raise ReleaseArtifactError("manifest paths or offsets do not match canonical layout")
    return manifest


def validate_release_directory(
    directory: Path,
    *,
    expected_commit: str | None = None,
    expected_version: str | None = None,
) -> dict[str, str]:
    missing = sorted(name for name in REQUIRED_RELEASE_ARTIFACTS if not (directory / name).is_file())
    if missing:
        raise ReleaseArtifactError(f"missing release artifacts: {', '.join(missing)}")

    manifest = validate_manifest(directory / "manifest.json", expected_version)
    metadata = _load_json(directory / "build-metadata.json")
    if metadata.get("schema_version") != 1:
        raise ReleaseArtifactError("unsupported build metadata schema")
    if metadata.get("build_environment") != "SigurdOS_TDeck":
        raise ReleaseArtifactError("release artifacts were not built by SigurdOS_TDeck")
    if metadata.get("platformio_board") != "t-deck":
        raise ReleaseArtifactError("build metadata does not identify the T-Deck board")
    if metadata.get("chip_family") != "ESP32-S3" or metadata.get("mcu") != "esp32s3":
        raise ReleaseArtifactError("build metadata does not identify ESP32-S3")
    if metadata.get("partition_table") != "default_16MB.csv":
        raise ReleaseArtifactError("release build did not use the canonical 16 MB partition table")
    if metadata.get("flash_size") != "16MB" or metadata.get("flash_mode") != "keep":
        raise ReleaseArtifactError("release build has an unexpected flash size or mode")
    if metadata.get("git_dirty") is not False:
        raise ReleaseArtifactError("release artifacts were built from a dirty worktree")
    if metadata.get("version") != manifest["version"]:
        raise ReleaseArtifactError("manifest and build metadata versions differ")
    commit = metadata.get("git_sha")
    if not isinstance(commit, str) or len(commit) != 40:
        raise ReleaseArtifactError("build metadata needs a full Git commit SHA")
    if expected_commit is not None and commit != expected_commit:
        raise ReleaseArtifactError("build metadata commit does not match release commit")
    meshcore = metadata.get("meshcore_sha")
    if not isinstance(meshcore, str) or len(meshcore) != 40:
        raise ReleaseArtifactError("build metadata needs a full MeshCore commit SHA")
    timestamp = metadata.get("source_timestamp_utc")
    if not isinstance(timestamp, str) or not timestamp.endswith("Z"):
        raise ReleaseArtifactError("build metadata needs a deterministic UTC source timestamp")

    artifact_metadata = metadata.get("artifacts")
    if not isinstance(artifact_metadata, dict):
        raise ReleaseArtifactError("build metadata must describe generated artifacts")
    for name, expected_offset in GENERATED_WEB_OFFSETS.items():
        key = name.removeprefix("sigurdos-tdeck-").removesuffix(".bin")
        record = artifact_metadata.get(key)
        if not isinstance(record, dict):
            raise ReleaseArtifactError(f"build metadata is missing {key}")
        path = directory / name
        if record.get("file") != name or record.get("offset") != expected_offset:
            raise ReleaseArtifactError(f"build metadata has an invalid {key} path or offset")
        if record.get("size") != path.stat().st_size or record.get("sha256") != sha256_file(path):
            raise ReleaseArtifactError(f"build metadata digest/size mismatch for {name}")

    full_hash = sha256_file(directory / FULL_IMAGE_NAMES[0])
    for name in FULL_IMAGE_NAMES:
        path = directory / name
        if sha256_file(path) != full_hash:
            raise ReleaseArtifactError(f"{name} is not byte-identical to the merged image")
        result = audit(path)
        if not result.ok:
            raise ReleaseArtifactError(f"{name} is not a valid merged ESP32-S3 image: {result.errors[0]}")
        header = path.read_bytes()[:3]
        if len(header) != 3 or header[0] != 0xE9 or header[2] != 0x02:
            raise ReleaseArtifactError(f"{name} does not preserve the canonical DIO boot header")

    debug_result = audit(directory / "firmware-debug.bin")
    if not debug_result.ok:
        raise ReleaseArtifactError(
            "firmware-debug.bin is not a valid merged ESP32-S3 image: "
            f"{debug_result.errors[0]}"
        )

    if sha256_file(directory / "firmware.bin") != sha256_file(
        directory / "sigurdos-tdeck-firmware.bin"
    ):
        raise ReleaseArtifactError("firmware.bin does not match the web-flasher app")

    merged = (directory / "firmware-merged.bin").read_bytes()
    for name, offset in COMPONENT_OFFSETS.items():
        component = (directory / name).read_bytes()
        if merged[offset : offset + len(component)] != component:
            raise ReleaseArtifactError(f"merged image does not contain {name} at 0x{offset:x}")

    return {
        name: sha256_file(directory / name)
        for name in sorted(REQUIRED_RELEASE_ARTIFACTS)
    }
