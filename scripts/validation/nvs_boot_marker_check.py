#!/usr/bin/env python3
"""Check GPS validation NVS boot-marker evidence from an ESP32-S3 flash dump.

The parser mirrors the ESP-IDF NVS v2 page layout.  It deliberately accepts
only live, CRC-valid records from usable pages and resolves duplicate keys in
page sequence/entry order, which is the order used by the NVS page manager.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path


PAGE_SIZE = 4096
PAGE_HEADER_SIZE = 32
ENTRY_STATE_TABLE_OFFSET = PAGE_HEADER_SIZE
ENTRY_STATE_TABLE_SIZE = 32
ENTRY_DATA_OFFSET = ENTRY_STATE_TABLE_OFFSET + ENTRY_STATE_TABLE_SIZE
ENTRY_TABLE_OFFSET = ENTRY_DATA_OFFSET  # Compatibility alias for older callers.
ENTRY_SIZE = 32
ENTRY_COUNT = (PAGE_SIZE - ENTRY_DATA_OFFSET) // ENTRY_SIZE

NVS_VERSION = 0xFE

NVS_TYPE_U8 = 0x01
NVS_TYPE_U16 = 0x02
NVS_TYPE_U32 = 0x04
NVS_TYPE_U64 = 0x08
NVS_TYPE_I8 = 0x11
NVS_TYPE_I16 = 0x12
NVS_TYPE_I32 = 0x14
NVS_TYPE_I64 = 0x18
NVS_TYPE_STR = 0x21
NVS_TYPE_BLOB_IDX = 0x41
NVS_TYPE_BLOB = 0x42
NVS_TYPE_BLOB_DATA = 0x48

NVS_KNOWN_TYPES = {
    NVS_TYPE_U8,
    NVS_TYPE_U16,
    NVS_TYPE_U32,
    NVS_TYPE_U64,
    NVS_TYPE_I8,
    NVS_TYPE_I16,
    NVS_TYPE_I32,
    NVS_TYPE_I64,
    NVS_TYPE_STR,
    NVS_TYPE_BLOB_IDX,
    NVS_TYPE_BLOB,
    NVS_TYPE_BLOB_DATA,
}
NVS_SCALAR_SIZES = {
    NVS_TYPE_U8: 1,
    NVS_TYPE_U16: 2,
    NVS_TYPE_U32: 4,
    NVS_TYPE_U64: 8,
    NVS_TYPE_I8: 1,
    NVS_TYPE_I16: 2,
    NVS_TYPE_I32: 4,
    NVS_TYPE_I64: 8,
}
NVS_SIGNED_TYPES = {NVS_TYPE_I8, NVS_TYPE_I16, NVS_TYPE_I32, NVS_TYPE_I64}

# Page states from esp-idf/components/nvs_flash/src/nvs_page.hpp.
PAGE_UNINITIALIZED = 0xFFFFFFFF
PAGE_ACTIVE = 0xFFFFFFFE
PAGE_FULL = 0xFFFFFFFC
PAGE_FREEING = 0xFFFFFFF8
USABLE_PAGE_STATES = {PAGE_ACTIVE, PAGE_FULL, PAGE_FREEING}

# Two-bit entry states from the ESP-IDF compressed entry-state table.
ENTRY_EMPTY = 0x3
ENTRY_WRITTEN = 0x2
ENTRY_ERASED = 0x0
ENTRY_ILLEGAL = 0x1

DEFAULT_NAMESPACE = "gpsval"
DEFAULT_KEYS = ("boot_count", "marker")
DEFAULT_MARKER_VALUE = "gps-validation"


class NvsFormatError(ValueError):
    """The image cannot be safely interpreted as an NVS partition."""


@dataclass(frozen=True)
class NvsPage:
    number: int
    sequence: int
    state: int
    version: int
    raw: bytes


@dataclass(frozen=True)
class NvsEntry:
    namespace_index: int
    entry_type: int
    span: int
    chunk_index: int
    key: str
    data: bytes
    page: int
    slot: int
    sequence: int


@dataclass(frozen=True)
class NvsSummary:
    path: Path
    namespaces: dict[str, int]
    keys_by_namespace: dict[int, set[str]]
    values_by_namespace: dict[int, dict[str, int]]
    strings_by_namespace: dict[int, dict[str, str]]
    pages: int
    valid_pages: int

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

    def has_marker_value(
        self,
        marker_value: str,
        namespace: str = DEFAULT_NAMESPACE,
        key: str = "marker",
    ) -> bool:
        namespace_index = self.namespace_index(namespace)
        if namespace_index is None:
            return False
        return (
            self.strings_by_namespace.get(namespace_index, {}).get(key)
            == marker_value
        )


def _read_u16(data: bytes) -> int:
    return int.from_bytes(data, byteorder="little", signed=False)


def _read_u32(data: bytes) -> int:
    return int.from_bytes(data, byteorder="little", signed=False)


def crc32_le(data: bytes, crc: int = 0xFFFFFFFF) -> int:
    """Match ESP-IDF's esp_rom_crc32_le(initial_crc, data, length)."""

    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc & 0xFFFFFFFF


def decode_key(raw_key: bytes) -> str | None:
    raw_key = raw_key.split(b"\x00", 1)[0]
    if not raw_key or len(raw_key) > 15:
        return None
    if any(byte < 0x20 or byte > 0x7E for byte in raw_key):
        return None
    try:
        return raw_key.decode("ascii")
    except UnicodeDecodeError:
        return None


def _entry_state(page: bytes, slot: int) -> int:
    state_byte = page[ENTRY_STATE_TABLE_OFFSET + (slot // 4)]
    return (state_byte >> ((slot % 4) * 2)) & 0x03


def _entry_crc(entry: bytes) -> int:
    # Item::calculateCrc32() covers the four-byte header prefix, key, and
    # eight-byte value union, excluding the stored crc32 field itself.
    return crc32_le(entry[:4] + entry[8:ENTRY_SIZE])


def _parse_page(raw: bytes, page_number: int) -> NvsPage | None:
    if len(raw) != PAGE_SIZE:
        raise NvsFormatError("page is truncated")
    if raw == b"\xff" * PAGE_SIZE:
        return None

    state = _read_u32(raw[0:4])
    if state == PAGE_UNINITIALIZED:
        # ESP-IDF only treats an all-0xff page as uninitialized.  Any other
        # bytes in this state are corruption, never historical live records.
        return None
    if state not in USABLE_PAGE_STATES:
        return None

    header_crc = _read_u32(raw[28:32])
    if header_crc != crc32_le(raw[4:28]):
        return None

    version = raw[8]
    if version < NVS_VERSION:
        return None

    return NvsPage(
        number=page_number,
        sequence=_read_u32(raw[4:8]),
        state=state,
        version=version,
        raw=raw,
    )


def parse_pages(raw: bytes) -> list[NvsPage]:
    if len(raw) % PAGE_SIZE:
        raise NvsFormatError(
            f"image length {len(raw)} is not a multiple of the {PAGE_SIZE}-byte page size"
        )
    pages: list[NvsPage] = []
    for page_number, page_start in enumerate(range(0, len(raw), PAGE_SIZE)):
        page = _parse_page(raw[page_start : page_start + PAGE_SIZE], page_number)
        if page is not None:
            pages.append(page)
    return pages


def _variable_data(page: NvsPage, entry: bytes, slot: int, span: int) -> bytes | None:
    data_size = _read_u16(entry[24:26])
    max_available = (ENTRY_COUNT - slot - 1) * ENTRY_SIZE
    span_from_size = (data_size + ENTRY_SIZE - 1) // ENTRY_SIZE + 1
    if data_size > max_available or span != span_from_size:
        return None

    chunks: list[bytes] = []
    for continuation_slot in range(slot + 1, slot + span):
        if _entry_state(page.raw, continuation_slot) != ENTRY_WRITTEN:
            return None
        start = ENTRY_DATA_OFFSET + continuation_slot * ENTRY_SIZE
        chunks.append(page.raw[start : start + ENTRY_SIZE])

    data = b"".join(chunks)[:data_size]
    if crc32_le(data) != _read_u32(entry[28:32]):
        return None
    return data


def _parse_entry(page: NvsPage, slot: int) -> NvsEntry | None:
    if _entry_state(page.raw, slot) != ENTRY_WRITTEN:
        return None

    start = ENTRY_DATA_OFFSET + slot * ENTRY_SIZE
    entry = page.raw[start : start + ENTRY_SIZE]
    if len(entry) != ENTRY_SIZE or entry == b"\xff" * ENTRY_SIZE:
        return None

    namespace_index = entry[0]
    entry_type = entry[1]
    span = entry[2]
    chunk_index = entry[3]
    key = decode_key(entry[8:24])
    if (
        namespace_index == 0xFF
        or key is None
        or entry_type not in NVS_KNOWN_TYPES
        or span < 1
        or span > ENTRY_COUNT - slot
        or _read_u32(entry[4:8]) != _entry_crc(entry)
    ):
        return None

    if entry_type in NVS_SCALAR_SIZES or entry_type == NVS_TYPE_BLOB_IDX:
        if span != 1:
            return None
        if entry_type == NVS_TYPE_BLOB_IDX:
            # The blob index is a one-entry metadata record and uses the
            # wildcard chunk index, just as ESP-IDF's Item consistency check.
            if chunk_index != 0xFF:
                return None
            max_blob_size = (0xFF // 2) * (ENTRY_COUNT - 1) * ENTRY_SIZE
            if _read_u32(entry[24:28]) > max_blob_size:
                return None
        data = entry[24:32]
    else:
        if entry_type == NVS_TYPE_BLOB_DATA and chunk_index == 0xFF:
            return None
        data = _variable_data(page, entry, slot, span)
        if data is None:
            return None

    return NvsEntry(
        namespace_index=namespace_index,
        entry_type=entry_type,
        span=span,
        chunk_index=chunk_index,
        key=key,
        data=data,
        page=page.number,
        slot=slot,
        sequence=page.sequence,
    )


def parse_entries(raw: bytes) -> list[NvsEntry]:
    entries: list[NvsEntry] = []
    pages = sorted(parse_pages(raw), key=lambda page: (page.sequence, page.number))
    for page in pages:
        slot = 0
        while slot < ENTRY_COUNT:
            entry = _parse_entry(page, slot)
            if entry is None:
                slot += 1
                continue
            entries.append(entry)
            slot += entry.span
    return entries


def decode_scalar_value(entry: NvsEntry) -> int | None:
    size = NVS_SCALAR_SIZES.get(entry.entry_type)
    if size is None:
        return None
    return int.from_bytes(
        entry.data[:size],
        byteorder="little",
        signed=entry.entry_type in NVS_SIGNED_TYPES,
    )


def _decode_nvs_string(data: bytes) -> str | None:
    if not data or data[-1] != 0 or b"\x00" in data[:-1]:
        return None
    try:
        return data[:-1].decode("utf-8")
    except UnicodeDecodeError:
        return None


def summarize(path: Path) -> NvsSummary:
    raw = path.read_bytes()
    pages = parse_pages(raw)
    entries = parse_entries(raw)

    namespaces: dict[str, int] = {}
    records_by_namespace: dict[int, dict[str, NvsEntry]] = {}
    for entry in entries:
        if entry.namespace_index == 0 and entry.entry_type == NVS_TYPE_U8:
            namespace_index = entry.data[0]
            if namespace_index not in (0x00, 0xFF):
                namespaces[entry.key] = namespace_index
            continue
        records_by_namespace.setdefault(entry.namespace_index, {})[entry.key] = entry

    keys_by_namespace = {
        namespace_index: set(records)
        for namespace_index, records in records_by_namespace.items()
    }
    values_by_namespace: dict[int, dict[str, int]] = {}
    strings_by_namespace: dict[int, dict[str, str]] = {}
    for namespace_index, records in records_by_namespace.items():
        for key, entry in records.items():
            value = decode_scalar_value(entry)
            if value is not None:
                values_by_namespace.setdefault(namespace_index, {})[key] = value
            if entry.entry_type == NVS_TYPE_STR:
                string_value = _decode_nvs_string(entry.data)
                if string_value is not None:
                    strings_by_namespace.setdefault(namespace_index, {})[key] = string_value

    return NvsSummary(
        path=path,
        namespaces=namespaces,
        keys_by_namespace=keys_by_namespace,
        values_by_namespace=values_by_namespace,
        strings_by_namespace=strings_by_namespace,
        pages=len(raw) // PAGE_SIZE,
        valid_pages=len(pages),
    )


def presence(value: bool) -> str:
    return "present" if value else "absent"


def print_summary(summary: NvsSummary, args: argparse.Namespace) -> bool:
    namespace_present = summary.namespace_index(args.namespace) is not None
    key_results = {key: summary.has_key(args.namespace, key) for key in args.key}
    marker_present = summary.has_marker_value(args.marker_value, args.namespace)
    boot_count = summary.value(args.namespace, "boot_count")

    key_text = " ".join(f"{key}={presence(found)}" for key, found in key_results.items())
    boot_count_text = f"boot_count_value={boot_count} " if boot_count is not None else ""
    print(
        f"{summary.path}: "
        f"pages={summary.valid_pages}/{summary.pages} "
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
    had_error = False
    for image in args.image:
        if not image.is_file():
            print(f"{image}: not found", file=sys.stderr)
            all_ok = False
            had_error = True
            continue
        try:
            summary = summarize(image)
        except (OSError, NvsFormatError) as exc:
            print(f"{image}: invalid NVS image: {exc}", file=sys.stderr)
            all_ok = False
            had_error = True
            continue
        all_ok = print_summary(summary, args) and all_ok

    if args.require and not all_ok:
        return 1
    return 1 if had_error else 0


if __name__ == "__main__":
    raise SystemExit(main())
