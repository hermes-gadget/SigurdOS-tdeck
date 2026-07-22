from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

PACKAGE_PARENT = Path(__file__).resolve().parents[2]
if str(PACKAGE_PARENT) not in sys.path:
    sys.path.insert(0, str(PACKAGE_PARENT))

import audit_launcher_artifact as artifact_format

from hw_test.hw_constants import SCREENSHOT_TIMEOUT_S, CommandProtocol, boot_wait_for
from hw_test.hw_flash import (
    FlashError,
    HardwareFlasher,
    find_pi_host,
    validate_merged_firmware,
)
from hw_test.hw_report import HardwareReport, TestResult, TestStatus, utc_now, write_report_bundle
from hw_test.hw_serial import (
    DeviceInfo,
    ScreenshotError,
    decode_capture_text,
    infer_radio_availability,
    parse_stat_line,
)
from hw_test.hw_test_runner import PhaseSelection, _check_variant, _merge_report_metadata
from hw_test.hw_soak import SoakConfig


def _partition_entry(partition_type, subtype, offset, size, label):
    return struct.pack(
        "<HBBII16sI",
        artifact_format.ENTRY_MAGIC,
        partition_type,
        subtype,
        offset,
        size,
        label.encode().ljust(16, b"\0"),
        0,
    )


def _esp_image(segment: bytes) -> bytes:
    common = struct.pack(
        "<BBBBI", artifact_format.ESP_IMAGE_MAGIC, 1, 2, 0, 0x40370000
    )
    extended = struct.pack(
        "<BBBBHBHHBBBBB",
        0xEE, 0, 0, 0, artifact_format.ESP32S3_CHIP_ID, 0, 0, 0, 0, 0, 0, 0, 1,
    )
    image = common + extended + struct.pack("<II", 0x3FC80000, len(segment)) + segment
    checksum = 0xEF
    for value in segment:
        checksum ^= value
    checksum_end = artifact_format.align_up(len(image) + 1, 16)
    image += b"\0" * (checksum_end - len(image) - 1)
    image += bytes([checksum])
    return image + hashlib.sha256(image).digest()


def _valid_merged_fixture() -> bytes:
    app = _esp_image(b"application!")
    bootloader = _esp_image(b"boot" * 3)
    entries = b"".join(
        [
            _partition_entry(artifact_format.DATA_TYPE, 0x02, 0x9000, 0x5000, "nvs"),
            _partition_entry(
                artifact_format.DATA_TYPE,
                artifact_format.DATA_SUBTYPE_OTA,
                artifact_format.BOOT_APP0_OFFSET,
                artifact_format.BOOT_APP0_SIZE,
                "otadata",
            ),
            _partition_entry(artifact_format.APP_TYPE, 0x10, 0x10000, 0x20000, "app0"),
            _partition_entry(
                artifact_format.DATA_TYPE,
                artifact_format.DATA_SUBTYPE_SPIFFS,
                0x30000,
                0x10000,
                "spiffs",
            ),
        ]
    )
    table = (
        entries
        + struct.pack("<H", artifact_format.MD5_MAGIC)
        + b"\xFF" * 14
        + hashlib.md5(entries).digest()  # nosec: ESP partition format
    )
    data = bytearray(b"\xFF" * (0x10000 + len(app)))
    data[:len(bootloader)] = bootloader
    start = artifact_format.PARTITION_TABLE_OFFSET
    data[start:start + len(table)] = table
    start = artifact_format.BOOT_APP0_OFFSET
    data[start:start + 32] = (
        struct.pack("<I", 1) + b"\xFF" * 24 + struct.pack("<I", 0x4743989A)
    )
    data[start + 0x1000:start + 0x1004] = b"\x00" * 4
    data[0x10000:0x10000 + len(app)] = app
    return bytes(data)


class SerialParsingTests(unittest.TestCase):
    def test_stat_line(self) -> None:
        sample = parse_stat_line(
            "[stat] t=1599 heap=168864/163372 psram=7949451 batt=87% flush=0",
            elapsed_s=2.5,
        )
        self.assertIsNotNone(sample)
        assert sample is not None
        self.assertEqual(sample.heap_free, 168864)
        self.assertEqual(sample.heap_min_free, 163372)
        self.assertEqual(sample.psram_free, 7949451)
        self.assertEqual(sample.battery_percent, 87)

    def test_capture_filters_noise_and_writes_png(self) -> None:
        raw = struct.pack("<HH", 0xF800, 0x07E0)
        transcript = (
            "[capture] W=2 H=1 S=4\n"
            f"[cdata] {raw.hex().upper()} NOT-HEX [stat] heap=ABC\n"
            "[capture] END\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "capture.png"
            artifact = decode_capture_text(transcript, output, screen="test")
            self.assertEqual(artifact.raw_bytes, 4)
            self.assertEqual(output.read_bytes()[:8], b"\x89PNG\r\n\x1a\n")

    def test_short_capture_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ScreenshotError):
                decode_capture_text(
                    "[capture] W=2 H=1 S=4\n[cdata] FF\n[capture] END\n",
                    Path(directory) / "bad.png",
                )

    def test_capture_recovers_hex_after_interleaved_log_line(self) -> None:
        raw = bytes(range(32))
        encoded = raw.hex().upper()
        transcript = (
            "[capture] W=16 H=1 S=32\n"
            f"[cdata] {encoded[:20]}[D] mesh cafe=DEADBEEF\n{encoded[20:]}\n"
            "[capture] END\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            artifact = decode_capture_text(
                transcript,
                Path(directory) / "interleaved.png",
            )
            self.assertEqual(artifact.raw_bytes, len(raw))

    def test_remote_radio_capability_uses_profile_or_boot_log(self) -> None:
        self.assertFalse(infer_radio_availability("[test] getrf: profile=remote_no_radio"))
        self.assertTrue(infer_radio_availability("[test] getrf: profile=remote_radio"))
        self.assertTrue(infer_radio_availability("not configured", "[mesh] Radio: 868 MHz"))


class ReportTests(unittest.TestCase):
    def test_critical_failure_exit_code_and_bundle(self) -> None:
        now = utc_now()
        report = HardwareReport(mode="smoke", transport="local")
        report.results.append(
            TestResult(
                name="serial.connect",
                status=TestStatus.FAIL,
                started_at=now,
                finished_at=now,
                duration_s=0.1,
                detail="offline",
                critical=True,
            )
        )
        self.assertEqual(report.exit_code, 2)
        with tempfile.TemporaryDirectory() as directory:
            paths = write_report_bundle(report, Path(directory))
            payload = json.loads(paths["json"].read_text(encoding="utf-8"))
            self.assertEqual(payload["outcome"], "CRITICAL")
            self.assertIn("serial.connect", paths["markdown"].read_text(encoding="utf-8"))

    def test_pi_worker_merge_reports_pi_transport(self) -> None:
        report = HardwareReport(mode="smoke", transport="local")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            write_report_bundle(report, output)
            _merge_report_metadata(output, {"pi_host": "hermes-pi"}, transport="pi")
            payload = json.loads((output / "results.json").read_text(encoding="utf-8"))
            self.assertEqual(payload["transport"], "pi")
            self.assertEqual(payload["metadata"]["pi_host"], "hermes-pi")

    def test_expected_radio_variant_warns_for_no_radio_device(self) -> None:
        report = HardwareReport(mode="smoke", transport="local")
        info = DeviceInfo(
            protocol=CommandProtocol.REMOTE_TEST,
            radio_available=False,
            test_controller=True,
        )
        self.assertTrue(
            _check_variant(
                report,
                info,
                "SigurdOS_TDeck_remote_test_radio",
                PhaseSelection(smoke=True),
            )
        )
        self.assertEqual(report.results[-1].status, TestStatus.WARN)


class ConstantsAndFlashTests(unittest.TestCase):
    def test_boot_wait(self) -> None:
        self.assertEqual(boot_wait_for("SigurdOS_TDeck_remote_test_radio"), 10.0)
        self.assertEqual(boot_wait_for("SigurdOS_TDeck"), 4.0)
        self.assertEqual(CommandProtocol.REMOTE_TEST.value, "remote_test")
        self.assertEqual(SoakConfig().capture_timeout_s, SCREENSHOT_TIMEOUT_S)

    def test_app_only_firmware_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.bin"
            path.write_bytes(b"x" * 600_000)
            with self.assertRaises(FlashError):
                validate_merged_firmware(path)

    def test_renamed_random_data_is_refused_before_flashing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware-merged.bin"
            path.write_bytes(b"x" * 600_000)
            with self.assertRaisesRegex(FlashError, "invalid merged ESP32-S3 image"):
                validate_merged_firmware(path)

    def test_external_firmware_requires_and_matches_reviewed_digest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware-merged.bin"
            path.write_bytes(_valid_merged_fixture())
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            with self.assertRaisesRegex(FlashError, "requires --sha256 or --metadata"):
                validate_merged_firmware(path, require_provenance=True)
            self.assertEqual(
                validate_merged_firmware(
                    path, expected_sha256=digest, require_provenance=True
                ),
                path.resolve(),
            )
            with self.assertRaisesRegex(FlashError, "SHA-256 mismatch"):
                validate_merged_firmware(
                    path, expected_sha256="0" * 64, require_provenance=True
                )

    def test_release_evidence_metadata_binds_external_firmware(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            path = directory / "firmware-merged.bin"
            path.write_bytes(_valid_merged_fixture())
            metadata = directory / "release-evidence.json"
            metadata.write_text(
                json.dumps(
                    {"artifacts": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()}}
                ),
                encoding="utf-8",
            )
            self.assertEqual(
                validate_merged_firmware(
                    path, metadata=metadata, require_provenance=True
                ),
                path.resolve(),
            )

    def test_runner_no_longer_accepts_untrusted_pr_build_mode(self) -> None:
        runner = PACKAGE_PARENT / "hw_test" / "hw_test_runner.py"
        result = subprocess.run(
            [sys.executable, str(runner), "--pr", "123", "--local"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("unrecognized arguments: --pr 123", result.stderr)

    def test_pi_host_discovery_falls_back_after_timeout(self) -> None:
        reachable = mock.Mock(returncode=0, stdout="", stderr="")
        with mock.patch(
            "hw_test.hw_flash._run",
            side_effect=[FlashError("mDNS timed out"), reachable],
        ):
            self.assertEqual(find_pi_host(), "hermes-pi")

    def test_boot_log_filters_known_noise_but_rejects_bad_image(self) -> None:
        clean = (
            "i2cRead returned Error 263\n"
            "SPIFFS Already Mounted!\n"
            "SigurdOS Remote Test Controller\n"
        )
        self.assertTrue(HardwareFlasher.assess_boot_log(clean)[0])
        self.assertFalse(
            HardwareFlasher.assess_boot_log("Invalid image block\nSigurdOS T-Deck ready")[0]
        )


if __name__ == "__main__":
    unittest.main()
