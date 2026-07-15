import importlib.util
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/audit_launcher_artifact.py"
SPEC = importlib.util.spec_from_file_location("audit_launcher_artifact", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def partition_entry(partition_type, subtype, offset, size, label):
    return struct.pack(
        "<HBBII16sI",
        MODULE.ENTRY_MAGIC,
        partition_type,
        subtype,
        offset,
        size,
        label.encode().ljust(16, b"\0"),
        0,
    )


def app_image(chip_id=MODULE.ESP32S3_CHIP_ID, segment=b"test", append_digest=True):
    common = struct.pack("<BBBBI", MODULE.ESP_IMAGE_MAGIC, 1, 2, 0, 0x40370000)
    extended = struct.pack(
        "<BBBBHBHHBBBBB", 0xEE, 0, 0, 0, chip_id, 0, 0, 0, 0, 0, 0, 0,
        int(append_digest),
    )
    image = common + extended + struct.pack("<II", 0x3FC80000, len(segment)) + segment
    image += b"\0" * (MODULE.align_up(len(image) + 1, 16) - len(image))
    if append_digest:
        image += b"\0" * 32
    return image


def merged_fixture(*, chip_id=MODULE.ESP32S3_CHIP_ID, app_partition_size=0x20000,
                   malformed_size=False, truncate_image=False):
    image = app_image(chip_id=chip_id)
    entries = [
        partition_entry(MODULE.DATA_TYPE, MODULE.DATA_SUBTYPE_OTA, 0xE000, 0x2000, "otadata"),
        partition_entry(
            MODULE.APP_TYPE,
            0x10,
            0x10000,
            0 if malformed_size else app_partition_size,
            "app0",
        ),
        partition_entry(MODULE.DATA_TYPE, MODULE.DATA_SUBTYPE_SPIFFS, 0x30000, 0x10000, "spiffs"),
    ]
    data = bytearray(b"\xFF" * (0x10000 + len(image)))
    table = b"".join(entries) + b"\xFF" * MODULE.PARTITION_ENTRY_SIZE
    data[MODULE.PARTITION_TABLE_OFFSET : MODULE.PARTITION_TABLE_OFFSET + len(table)] = table
    data[0x10000 : 0x10000 + len(image)] = image
    return bytes(data[:-20] if truncate_image else data)


class LauncherArtifactAuditTests(unittest.TestCase):
    def write_fixture(self, data):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name) / "firmware-merged.bin"
        path.write_bytes(data)
        return path

    def test_valid_image_reports_esp32s3_metadata(self):
        result = MODULE.audit(self.write_fixture(merged_fixture()))
        self.assertTrue(result.ok, result.errors)
        self.assertEqual(result.image.chip_id, MODULE.ESP32S3_CHIP_ID)
        self.assertEqual(result.image.segment_count, 1)
        self.assertEqual(result.image.launcher_allocation, MODULE.LAUNCHER_APP_ALIGNMENT)

    def test_missing_table_and_truncated_file_fail(self):
        for data in (b"not an image", b"\xFF" * 0x9000):
            with self.subTest(size=len(data)):
                result = MODULE.audit(self.write_fixture(data))
                self.assertFalse(result.ok)
                self.assertTrue(result.errors)

    def test_wrong_chip_and_truncated_image_fail(self):
        wrong_chip = MODULE.audit(self.write_fixture(merged_fixture(chip_id=0)))
        self.assertFalse(wrong_chip.ok)
        self.assertIn("expected ESP32-S3", wrong_chip.errors[0])

        truncated = MODULE.audit(self.write_fixture(merged_fixture(truncate_image=True)))
        self.assertFalse(truncated.ok)
        self.assertIn("truncated", truncated.errors[0])

    def test_oversized_app_and_malformed_entry_fail(self):
        oversized = MODULE.audit(
            self.write_fixture(merged_fixture(app_partition_size=32))
        )
        self.assertFalse(oversized.ok)
        self.assertTrue(any("exceeds partition" in error for error in oversized.errors))

        malformed = MODULE.audit(self.write_fixture(merged_fixture(malformed_size=True)))
        self.assertFalse(malformed.ok)
        self.assertIn("zero size", malformed.errors[0])

    def test_cli_returns_nonzero_for_invalid_input(self):
        invalid = self.write_fixture(b"not an image")
        completed = subprocess.run(
            [sys.executable, str(SCRIPT), str(invalid)], capture_output=True, text=True
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("FAIL", completed.stdout)


if __name__ == "__main__":
    unittest.main()
