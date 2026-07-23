#!/usr/bin/env python3
"""Stamp and verify the OTA anti-rollback epoch in an ESP application image."""

from __future__ import annotations

import hashlib
import os
import struct
from pathlib import Path


ESP_IMAGE_MAGIC = 0xE9
ESP_APP_DESC_MAGIC = 0xABCD5432
ESP_IMAGE_HEADER_SIZE = 24
ESP_SEGMENT_HEADER_SIZE = 8
ESP_APP_DESC_OFFSET = 32
ESP_SECURE_VERSION_OFFSET = ESP_APP_DESC_OFFSET + 4


class SecurityEpochError(ValueError):
    """Raised when an image cannot be safely stamped."""


def _image_layout(image: bytes | bytearray) -> tuple[int, int]:
    if len(image) < ESP_SECURE_VERSION_OFFSET + 4 or image[0] != ESP_IMAGE_MAGIC:
        raise SecurityEpochError("invalid or truncated ESP application image")
    if struct.unpack_from("<I", image, ESP_APP_DESC_OFFSET)[0] != ESP_APP_DESC_MAGIC:
        raise SecurityEpochError("ESP application descriptor is missing")

    cursor = ESP_IMAGE_HEADER_SIZE
    checksum = 0xEF
    for _ in range(image[1]):
        if cursor + ESP_SEGMENT_HEADER_SIZE > len(image):
            raise SecurityEpochError("truncated ESP segment header")
        segment_size = struct.unpack_from("<I", image, cursor + 4)[0]
        cursor += ESP_SEGMENT_HEADER_SIZE
        segment_end = cursor + segment_size
        if segment_end > len(image):
            raise SecurityEpochError("truncated ESP segment data")
        for value in image[cursor:segment_end]:
            checksum ^= value
        cursor = segment_end

    # The checksum is the last byte of the 16-byte block that follows the
    # segment stream. When the stream already ends on a block boundary, ESP
    # images contain 15 padding bytes before the checksum; rounding up with
    # integer division would incorrectly select the byte before the stream.
    checksum_offset = cursor | 0x0F
    hash_size = 32 if image[23] == 1 else 0
    expected_size = checksum_offset + 1 + hash_size
    if checksum_offset < cursor or len(image) != expected_size:
        raise SecurityEpochError("unexpected ESP image checksum or hash layout")
    return checksum_offset, checksum


def stamp_security_epoch(image: bytes, epoch: int) -> bytes:
    if not 0 <= epoch <= 0xFFFFFFFF:
        raise SecurityEpochError("security epoch must fit in uint32")

    stamped = bytearray(image)
    _image_layout(stamped)
    struct.pack_into("<I", stamped, ESP_SECURE_VERSION_OFFSET, epoch)
    checksum_offset, checksum = _image_layout(stamped)
    stamped[checksum_offset] = checksum
    if stamped[23] == 1:
        stamped[-32:] = hashlib.sha256(stamped[:-32]).digest()

    verified_offset, verified_checksum = _image_layout(stamped)
    if stamped[verified_offset] != verified_checksum:
        raise SecurityEpochError("failed to verify stamped ESP checksum")
    if stamped[23] == 1 and stamped[-32:] != hashlib.sha256(stamped[:-32]).digest():
        raise SecurityEpochError("failed to verify stamped ESP image hash")
    return bytes(stamped)


def _configured_epoch(build_env) -> int:
    for define in build_env.get("CPPDEFINES", []):
        if isinstance(define, (tuple, list)) and len(define) == 2:
            name, value = define
            if str(name) == "SIGURDOS_SECURITY_EPOCH":
                return int(str(value).rstrip("ULul"), 0)
        elif isinstance(define, str) and define.startswith("SIGURDOS_SECURITY_EPOCH="):
            return int(define.split("=", 1)[1].rstrip("ULul"), 0)
    raise SecurityEpochError("SIGURDOS_SECURITY_EPOCH is not configured")


def stamp_action(target, source, env) -> None:
    firmware = Path(env.subst("$BUILD_DIR/${PROGNAME}.bin"))
    epoch = _configured_epoch(env)
    original = firmware.read_bytes()
    stamped = stamp_security_epoch(original, epoch)
    temporary = firmware.with_suffix(firmware.suffix + ".epoch.tmp")
    try:
        temporary.write_bytes(stamped)
        os.replace(temporary, firmware)
    finally:
        if temporary.exists():
            temporary.unlink()
    print(f"SigurdOS: stamped OTA security epoch {epoch} into {firmware.name}")


try:
    Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.
except NameError:
    env = None

if env is not None:
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", stamp_action)
