from __future__ import annotations

import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

PACKAGE_PARENT = Path(__file__).resolve().parents[2]
if str(PACKAGE_PARENT) not in sys.path:
    sys.path.insert(0, str(PACKAGE_PARENT))

from hw_test.hw_constants import SCREENSHOT_TIMEOUT_S, CommandProtocol, boot_wait_for
from hw_test.hw_flash import FlashError, HardwareFlasher, find_pi_host, validate_merged_firmware
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
