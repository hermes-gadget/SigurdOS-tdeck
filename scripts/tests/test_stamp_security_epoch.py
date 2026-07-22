import hashlib
import importlib.util
import struct
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "stamp_security_epoch.py"
SPEC = importlib.util.spec_from_file_location("stamp_security_epoch", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def make_image(epoch=0, hashed=True):
    header = bytearray(24)
    header[0] = MODULE.ESP_IMAGE_MAGIC
    header[1] = 1
    header[23] = int(hashed)
    payload = bytearray(40)
    struct.pack_into("<I", payload, 0, MODULE.ESP_APP_DESC_MAGIC)
    struct.pack_into("<I", payload, 4, epoch)
    segment = struct.pack("<II", 0x3C000020, len(payload)) + payload
    body = header + segment
    checksum = 0xEF
    for value in payload:
        checksum ^= value
    body.extend(bytes(((-len(body) - 1) % 16)))
    body.append(checksum)
    if hashed:
        body.extend(hashlib.sha256(body).digest())
    return bytes(body)


class StampSecurityEpochTests(unittest.TestCase):
    def test_stamps_epoch_and_repairs_checksum_and_hash(self):
        original = make_image(epoch=0, hashed=True)
        stamped = MODULE.stamp_security_epoch(original, 7)

        self.assertEqual(struct.unpack_from("<I", stamped, 36)[0], 7)
        checksum_offset, checksum = MODULE._image_layout(stamped)
        self.assertEqual(stamped[checksum_offset], checksum)
        self.assertEqual(stamped[-32:], hashlib.sha256(stamped[:-32]).digest())
        self.assertNotEqual(stamped, original)

    def test_supports_images_without_appended_hash(self):
        stamped = MODULE.stamp_security_epoch(make_image(hashed=False), 2)
        checksum_offset, checksum = MODULE._image_layout(stamped)
        self.assertEqual(stamped[checksum_offset], checksum)
        self.assertEqual(len(stamped), checksum_offset + 1)

    def test_rejects_malformed_or_out_of_range_input(self):
        with self.assertRaises(MODULE.SecurityEpochError):
            MODULE.stamp_security_epoch(b"not an image", 1)
        with self.assertRaises(MODULE.SecurityEpochError):
            MODULE.stamp_security_epoch(make_image(), 0x1_0000_0000)


if __name__ == "__main__":
    unittest.main()
