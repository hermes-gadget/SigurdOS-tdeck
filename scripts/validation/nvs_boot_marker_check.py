#!/usr/bin/env python3
"""Check GPS validation NVS boot-marker evidence from an ESP32-S3 flash dump."""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path


PAGE_SIZE = 4096
ENTRY_TABLE_OFFSET = 64
ENTRY_SIZE = 32
ENTRY_COUNT = 126

NVS_TYPE_U8 = 0x01
NVS_KNOWN_TYPES = {
    0x01,  # u8
    0x02,  # u16
    0x04,  # u32
    0x08,  # u64
    0x11,  # i8
    0x12,  # i16
    0x14,  # i32
    0x18,  # i64
    0x21,  # string
    0x41,  # blob index
    0x42,  # blob
    0x48,  # blob data
}

DEFAULT_NAMESPACE = "gpsval"
DEFAULT_KEYS = ("boot_count", "marker")
DEFAULT_MARKER_VALUE = "gps-validation"


@dataclass(frozen=True)
class NvsEntry:
    namespace_index: int
    entry_type: int
    span: int
    key: str
    data: bytes
    page: int
    slot: int


@dataclass(frozen=True)
class NvsSummary:
    path: Path
    namespaces: dict[str, int]
    keys_by_namespace: dict[int, set[str]]
    values_by_namespace: dict[int, dict[str, int]]
    raw: bytes

    def namespace_index(self, namespace: str) -> int | None:
        return self.namespaces.get(namespace)

    def has_key(self, namespace: str, key: str) -> bool:
        namespace_index = self.namespace_index(namespace)
        if namespace_index is None:
            return False
        return key in self.keys_by_namespace.get(namespace_index, set())

    def value(self, namespace: str, key: str) -> int | None:
        namespace_index = self.namespace_index(namespace)
        if namespace_index is None:
            return None
        return self.values_by_namespace.get(namespace_index, {}).get(key)

    def has_marker_value(self, marker_value: str) -> bool:
        return marker_value.encode("utf-8") in self.raw


def decode_key(raw_key: bytes) -> str | None:
    raw_key = raw_key.split(b"\x00", 1)[0]
    if not raw_key:
        return None
    if len(raw_key) > 15:
        return None
    if any(byte < 0x20 or byte > 0x7e for byte in raw_key):
        return None
    try:
        return raw_key.decode("ascii")
    except UnicodeDecodeError:
        return None


def parse_entries(raw: bytes) -> list[NvsEntry]:
    entries: list[NvsEntry] = []
    for page_number, page_start in enumerate(range(0, len(raw), PAGE_SIZE)):
        page = raw[page_start:page_start + PAGE_SIZE]
        if len(page) < PAGE_SIZE or page.count(0xff) == len(page):
            continue

        slot = 0
        while slot < ENTRY_COUNT:
            entry_start = ENTRY_TABLE_OFFSET + (slot * ENTRY_SIZE)
            entry = page[entry_start:entry_start + ENTRY_SIZE]
            if len(entry) < ENTRY_SIZE:
                break
            if entry.count(0xff) == ENTRY_SIZE or entry.count(0x00) == ENTRY_SIZE:
                slot += 1
                continue

            namespace_index = entry[0]
            entry_type = entry[1]
            span = entry[2]
            key = decode_key(entry[8:24])

            if (
                key is None
                or entry_type not in NVS_KNOWN_TYPES
                or span < 1
                or span > ENTRY_COUNT - slot
                or namespace_index == 0xff
            ):
                slot += 1
                continue

            entries.append(
                NvsEntry(
                    namespace_index=namespace_index,
                    entry_type=entry_type,
                    span=span,
                    key=key,
                    data=entry[24:32],
                    page=page_number,
                    slot=slot,
                ),
            )
            slot += span

    return entries


def decode_scalar_value(entry: NvsEntry) -> int | None:
    sizes = {
        0x01: 1,  # u8
        0x02: 2,  # u16
        0x04: 4,  # u32
        0x08: 8,  # u64
        0x11: 1,  # i8
        0x12: 2,  # i16
        0x14: 4,  # i32
        0x18: 8,  # i64
    }
    signed_types = {0x11, 0x12, 0x14, 0x18}
    size = sizes.get(entry.entry_type)
    if size is None:
        return None
    return int.from_bytes(
        entry.data[:size],
        byteorder="little",
        signed=entry.entry_type in signed_types,
    )


def summarize(path: Path) -> NvsSummary:
    raw = path.read_bytes()
    namespaces: dict[str, int] = {}
    keys_by_namespace: dict[int, set[str]] = {}
    values_by_namespace: dict[int, dict[str, int]] = {}

    for entry in parse_entries(raw):
        if entry.namespace_index == 0 and entry.entry_type == NVS_TYPE_U8:
            namespace_index = entry.data[0]
            if namespace_index not in (0x00, 0xff):
                namespaces[entry.key] = namespace_index
            continue

        keys_by_namespace.setdefault(entry.namespace_index, set()).add(entry.key)
        value = decode_scalar_value(entry)
        if value is not None:
            values_by_namespace.setdefault(entry.namespace_index, {})[entry.key] = value

    return NvsSummary(
        path=path,
        namespaces=namespaces,
        keys_by_namespace=keys_by_namespace,
        values_by_namespace=values_by_namespace,
        raw=raw,
    )


def presence(value: bool) -> str:
    return "present" if value else "absent"


def print_summary(summary: NvsSummary, args: argparse.Namespace) -> bool:
    namespace_present = summary.namespace_index(args.namespace) is not None
    key_results = {key: summary.has_key(args.namespace, key) for key in args.key}
    marker_present = summary.has_marker_value(args.marker_value)
    boot_count = summary.value(args.namespace, "boot_count")

    key_text = " ".join(f"{key}={presence(found)}" for key, found in key_results.items())
    boot_count_text = f"boot_count_value={boot_count} " if boot_count is not None else ""
    print(
        f"{summary.path}: "
        f"namespace {args.namespace}={presence(namespace_present)} "
        f"{key_text} "
        f"marker_value {args.marker_value}={presence(marker_present)} "
        f"{boot_count_text}"
        f"known_namespace sigurdos={presence(summary.namespace_index('sigurdos') is not None)}",
    )

    if args.verbose:
        print("  namespaces:")
        for namespace, namespace_index in sorted(summary.namespaces.items(), key=lambda item: item[1]):
            keys = sorted(summary.keys_by_namespace.get(namespace_index, set()))
            key_list = ", ".join(keys) if keys else "-"
            print(f"    {namespace_index:3d} {namespace}: {key_list}")

    return namespace_present and all(key_results.values()) and marker_present


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Inspect ESP32 NVS readback images for the SigurdOS GPS validation "
            "boot marker namespace, keys, and marker value."
        ),
    )
    parser.add_argument("image", nargs="+", type=Path, help="NVS partition image read back from flash")
    parser.add_argument("--namespace", default=DEFAULT_NAMESPACE, help="required NVS namespace")
    parser.add_argument(
        "--key",
        action="append",
        default=[],
        help="required NVS key inside the namespace; defaults to boot_count and marker",
    )
    parser.add_argument("--marker-value", default=DEFAULT_MARKER_VALUE, help="required marker string value")
    parser.add_argument("--require", action="store_true", help="exit non-zero unless every marker is present")
    parser.add_argument("--verbose", action="store_true", help="print parsed namespaces and keys")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if not args.key:
        args.key = list(DEFAULT_KEYS)

    all_ok = True
    for image in args.image:
        if not image.is_file():
            print(f"{image}: not found", file=sys.stderr)
            all_ok = False
            continue
        summary = summarize(image)
        all_ok = print_summary(summary, args) and all_ok

    return 1 if args.require and not all_ok else 0


if __name__ == "__main__":
    raise SystemExit(main())
