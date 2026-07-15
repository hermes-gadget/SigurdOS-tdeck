#!/usr/bin/env python3
"""Validate a merged ESP32-S3 image as a Launcher installation source."""

from __future__ import annotations

import argparse
import json
import struct
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path

PARTITION_TABLE_OFFSET = 0x8000
PARTITION_ENTRY_SIZE = 0x20
PARTITION_TABLE_SIZE = 0x1000
ENTRY_MAGIC = 0x50AA
MD5_MAGIC = 0xEBEB

APP_TYPE = 0x00
DATA_TYPE = 0x01
APP_SOURCE_SUBTYPES = {0x00, 0x10, 0x20}
DATA_SUBTYPE_OTA = 0x00
DATA_SUBTYPE_SPIFFS = 0x82

ESP_IMAGE_MAGIC = 0xE9
ESP32S3_CHIP_ID = 9
MAX_IMAGE_SEGMENTS = 16
LAUNCHER_APP_ALIGNMENT = 0x10000
LAUNCHER_DEFAULT_SPIFFS_SIZE = 0x100000

TYPE_NAMES = {APP_TYPE: "app", DATA_TYPE: "data"}
APP_SUBTYPE_NAMES = {0x00: "factory", 0x10: "ota_0", 0x11: "ota_1", 0x20: "test"}
DATA_SUBTYPE_NAMES = {0x00: "ota", 0x81: "fat", 0x82: "spiffs"}


class ArtifactError(ValueError):
    """Raised when bytes cannot represent a valid audited structure."""


@dataclass
class ImageMetadata:
    offset: int
    image_size: int
    segment_count: int
    chip_id: int
    append_digest: bool
    launcher_allocation: int


@dataclass
class AuditResult:
    path: str
    file_size: int
    entries: list[dict] = field(default_factory=list)
    image: ImageMetadata | None = None
    flash_capacity: int = 0
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.errors

    def to_dict(self) -> dict:
        data = asdict(self)
        data["ok"] = self.ok
        return data


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def type_name(partition_type: int) -> str:
    return TYPE_NAMES.get(partition_type, f"0x{partition_type:02x}")


def subtype_name(partition_type: int, subtype: int) -> str:
    names = APP_SUBTYPE_NAMES if partition_type == APP_TYPE else DATA_SUBTYPE_NAMES
    return names.get(subtype, f"0x{subtype:02x}")


def parse_partition_entry(data: bytes, offset: int) -> dict | None:
    if offset + PARTITION_ENTRY_SIZE > len(data):
        raise ArtifactError(f"truncated partition entry at file offset 0x{offset:x}")
    entry = data[offset : offset + PARTITION_ENTRY_SIZE]
    magic = struct.unpack_from("<H", entry)[0]
    if magic in (0xFFFF, MD5_MAGIC):
        return None
    if magic != ENTRY_MAGIC:
        raise ArtifactError(f"invalid partition magic 0x{magic:04x} at file offset 0x{offset:x}")

    _, partition_type, subtype, partition_offset, size, raw_label, flags = struct.unpack(
        "<HBBII16sI", entry
    )
    if size == 0:
        raise ArtifactError(f"partition at file offset 0x{offset:x} has zero size")
    try:
        label = raw_label.split(b"\0", 1)[0].decode("ascii")
    except UnicodeDecodeError as exc:
        raise ArtifactError(f"partition at file offset 0x{offset:x} has a non-ASCII label") from exc
    if not label:
        raise ArtifactError(f"partition at file offset 0x{offset:x} has an empty label")
    return {
        "type": partition_type,
        "type_name": type_name(partition_type),
        "subtype": subtype,
        "subtype_name": subtype_name(partition_type, subtype),
        "offset": partition_offset,
        "size": size,
        "label": label,
        "flags": flags,
    }


def parse_partition_table(data: bytes) -> list[dict]:
    if len(data) < PARTITION_TABLE_OFFSET + 2:
        raise ArtifactError("file is truncated before the partition table")
    entries = []
    table_end = min(len(data), PARTITION_TABLE_OFFSET + PARTITION_TABLE_SIZE)
    position = PARTITION_TABLE_OFFSET
    while position + PARTITION_ENTRY_SIZE <= table_end:
        entry = parse_partition_entry(data, position)
        if entry is None:
            break
        entries.append(entry)
        position += PARTITION_ENTRY_SIZE
    if not entries:
        raise ArtifactError(f"no partition table at file offset 0x{PARTITION_TABLE_OFFSET:x}")

    ordered = sorted(entries, key=lambda item: item["offset"])
    for previous, current in zip(ordered, ordered[1:]):
        previous_end = previous["offset"] + previous["size"]
        if current["offset"] < previous_end:
            raise ArtifactError(
                f"partitions {previous['label']} and {current['label']} overlap"
            )
    return entries


def parse_esp32s3_image(data: bytes, offset: int) -> ImageMetadata:
    header_size = 24
    if offset + header_size > len(data):
        raise ArtifactError(f"app image header at 0x{offset:x} is truncated")

    magic, segment_count, _, _, _ = struct.unpack_from("<BBBBI", data, offset)
    if magic != ESP_IMAGE_MAGIC:
        raise ArtifactError(f"app image at 0x{offset:x} has invalid magic 0x{magic:02x}")
    if not 1 <= segment_count <= MAX_IMAGE_SEGMENTS:
        raise ArtifactError(f"app image has invalid segment count {segment_count}")

    extended = struct.unpack_from("<BBBBHBHHBBBBB", data, offset + 8)
    chip_id = extended[4]
    append_digest = extended[-1]
    if chip_id != ESP32S3_CHIP_ID:
        raise ArtifactError(
            f"app image chip ID is {chip_id}; expected ESP32-S3 chip ID {ESP32S3_CHIP_ID}"
        )
    if append_digest not in (0, 1):
        raise ArtifactError(f"app image has invalid append-digest flag {append_digest}")

    position = offset + header_size
    for segment_index in range(segment_count):
        if position + 8 > len(data):
            raise ArtifactError(f"segment {segment_index} header is truncated")
        _, segment_size = struct.unpack_from("<II", data, position)
        if segment_size == 0 or segment_size % 4:
            raise ArtifactError(f"segment {segment_index} has invalid size {segment_size}")
        position += 8
        if position + segment_size > len(data):
            raise ArtifactError(f"segment {segment_index} data is truncated")
        position += segment_size

    checksum_end = align_up(position + 1, 16)
    image_end = checksum_end + (32 if append_digest else 0)
    if image_end > len(data):
        raise ArtifactError("app image checksum or digest is truncated")
    image_size = image_end - offset
    return ImageMetadata(
        offset=offset,
        image_size=image_size,
        segment_count=segment_count,
        chip_id=chip_id,
        append_digest=bool(append_digest),
        launcher_allocation=align_up(image_size, LAUNCHER_APP_ALIGNMENT),
    )


def audit(path: str | Path) -> AuditResult:
    artifact = Path(path)
    data = artifact.read_bytes()
    result = AuditResult(path=str(artifact), file_size=len(data))
    try:
        result.entries = parse_partition_table(data)
    except ArtifactError as exc:
        result.errors.append(str(exc))
        return result

    result.flash_capacity = max(entry["offset"] + entry["size"] for entry in result.entries)
    app_sources = [
        entry
        for entry in result.entries
        if entry["type"] == APP_TYPE and entry["subtype"] in APP_SOURCE_SUBTYPES
    ]
    spiffs_entries = [
        entry
        for entry in result.entries
        if entry["type"] == DATA_TYPE and entry["subtype"] == DATA_SUBTYPE_SPIFFS
    ]
    ota_entries = [
        entry
        for entry in result.entries
        if entry["type"] == DATA_TYPE and entry["subtype"] == DATA_SUBTYPE_OTA
    ]
    if len(app_sources) != 1:
        result.errors.append(f"expected exactly one Launcher app source, found {len(app_sources)}")
    if not spiffs_entries:
        result.errors.append("partition table has no SPIFFS entry for Launcher persistence")
    if not ota_entries:
        result.warnings.append("partition table has no OTA data entry")
    if not app_sources:
        return result

    app = app_sources[0]
    try:
        result.image = parse_esp32s3_image(data, app["offset"])
    except ArtifactError as exc:
        result.errors.append(str(exc))
        return result
    if result.image.image_size > app["size"]:
        result.errors.append(
            f"app image size {result.image.image_size} exceeds partition {app['label']} "
            f"capacity {app['size']}"
        )
    return result


def render(result: AuditResult) -> None:
    print(f"File: {result.path}")
    print(f"Size: {result.file_size:,} bytes")
    if result.entries:
        print("\nPartitions:")
        for entry in result.entries:
            print(
                f"  {entry['label']:<16} {entry['type_name']}/{entry['subtype_name']:<8} "
                f"offset=0x{entry['offset']:06x} size=0x{entry['size']:x}"
            )
    if result.image:
        image = result.image
        print("\nESP application image:")
        print(f"  chip_id={image.chip_id} (ESP32-S3)")
        print(f"  segment_count={image.segment_count}")
        print(f"  parsed_size={image.image_size:,} bytes")
        print(f"  Launcher dynamic app allocation={image.launcher_allocation:,} bytes")
        print(
            "  Launcher resident slot is separate; this artifact also requires "
            f"{LAUNCHER_DEFAULT_SPIFFS_SIZE:,} bytes for its default SPIFFS partition."
        )
    for warning in result.warnings:
        print(f"WARNING: {warning}")
    for error in result.errors:
        print(f"ERROR: {error}", file=sys.stderr)
    print(f"\n{'PASS' if result.ok else 'FAIL'}: Launcher artifact audit")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "path",
        nargs="?",
        type=Path,
        default=Path(".pio/build/SigurdOS_TDeck/firmware-merged.bin"),
    )
    parser.add_argument("--json", action="store_true", help="emit the structured result as JSON")
    args = parser.parse_args()
    try:
        result = audit(args.path)
    except OSError as exc:
        print(f"launcher artifact audit failed: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(result.to_dict(), indent=2))
    else:
        render(result)
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
