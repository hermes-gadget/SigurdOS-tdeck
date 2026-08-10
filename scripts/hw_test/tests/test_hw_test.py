from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from pathlib import Path
from unittest import mock

PACKAGE_PARENT = Path(__file__).resolve().parents[2]
if str(PACKAGE_PARENT) not in sys.path:
    sys.path.insert(0, str(PACKAGE_PARENT))

import audit_launcher_artifact as artifact_format

from hw_test.hw_constants import (
    SCREENSHOT_TIMEOUT_S,
    CommandProtocol,
    boot_wait_for,
    nav_silence_grace_for,
    nav_timeout_for,
)
from hw_test.hw_flash import (
    FlashError,
    HardwareFlasher,
    find_pi_host,
    validate_merged_firmware,
)
from hw_test.hw_report import HardwareReport, TestResult, TestStatus, utc_now, write_report_bundle
from hw_test.hw_serial import (
    DeviceInfo,
    PersistentSerial,
    ScreenshotError,
    ScreenshotCrashError,
    decode_capture_text,
    infer_radio_availability,
    parse_stat_line,
)
from hw_test.hw_test_runner import (
    PhaseSelection,
    _check_variant,
    _merge_report_metadata,
    _parse_radio_profile,
    _restore_radio_profile,
    _run_pi_worker,
    _wait_for_mesh_ready,
    build_parser,
    run_crash_recovery,
    run_radio,
    validate_args,
)
from hw_test.hw_soak import SoakConfig, SoakExit, SoakRunner


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

    def test_capture_transcript_with_panic_and_rom_reset_is_rejected(self) -> None:
        transcript = (
            "[capture] W=2 H=1 S=4\n"
            "[cdata] 00FF\n"
            "[D] panic text follows: Guru Meditation\n"
            "ESP-ROM:esp32s3\n"
            "rst:0x3 (SW_RESET)\n"
            "[capture] END\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ScreenshotCrashError) as raised:
                decode_capture_text(transcript, Path(directory) / "crash.png")

        self.assertEqual(raised.exception.marker, "Guru Meditation")
        self.assertIn("ESP-ROM:esp32s3", raised.exception.evidence)
        self.assertLessEqual(len(raised.exception.evidence), 1000)

    def test_capture_screenshot_scans_stream_before_decoding(self) -> None:
        transcript = (
            "[capture] W=2 H=1 S=4\n"
            "[cdata] 00FF\n"
            "ESP-ROM:esp32s3\n"
            "rst:0x3 (SW_RESET)\n"
            + "x" * 300
        ).encode()

        class FakeSerial:
            is_open = True

            def __init__(self) -> None:
                self.pending = bytearray()

            @property
            def in_waiting(self) -> int:
                return len(self.pending)

            def read(self, size: int = 1) -> bytes:
                chunk = bytes(self.pending[:size])
                del self.pending[:size]
                return chunk

            def write(self, _data: bytes) -> None:
                self.pending.extend(transcript)

            def flush(self) -> None:
                return None

            def close(self) -> None:
                self.is_open = False

        connection = PersistentSerial("fake")
        connection.protocol = CommandProtocol.REMOTE_TEST
        connection._serial = FakeSerial()
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(connection, "drain"):
                with self.assertRaises(ScreenshotCrashError) as raised:
                    connection.capture_screenshot(Path(directory) / "crash.png")

        self.assertEqual(raised.exception.marker, "ESP-ROM:esp32s3")
        self.assertIn("ESP-ROM:esp32s3", raised.exception.evidence)


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

    def test_radio_profile_parser_includes_rx_boost(self) -> None:
        self.assertEqual(
            _parse_radio_profile(
                "[test] getrf: profile=remote_radio freq=869.525 SF=10 "
                "BW=250.0 CR=5 TX=7 dBm RX_BOOST=1 channels=2"
            ),
            (869.525, 10, 250.0, 5, 7, True),
        )


class SoakRunnerTests(unittest.TestCase):
    class _FakeRemoteConnection:
        protocol = CommandProtocol.REMOTE_TEST

        def drain(self, _duration_s: float) -> None:
            return None

        def read(self, _size: int = 1) -> bytes:
            time.sleep(0.001)
            return b""

        def capture_screenshot(self, *_args, **_kwargs):
            raise ScreenshotCrashError(
                "Guru Meditation",
                "[cdata] 00FF\nGuru Meditation\nESP-ROM:esp32s3\nrst:0x3",
            )

    class _FakeReleaseConnection:
        protocol = CommandProtocol.RELEASE

        def __init__(self, response: str) -> None:
            self.response = response
            self.probe_kwargs: dict[str, object] | None = None

        def drain(self, _duration_s: float) -> None:
            return None

        def read(self, _size: int = 1) -> bytes:
            time.sleep(0.001)
            return b""

        def send_command(self, _command: str, **kwargs):
            self.probe_kwargs = kwargs
            return SimpleNamespace(output=self.response, wire_command="NAV home")

    def _run_soak(
        self,
        connection,
        *,
        duration_s: float = 0.02,
        screenshot_interval_s: float = 0.0,
    ):
        with tempfile.TemporaryDirectory() as directory:
            result = SoakRunner(
                connection,
                Path(directory),
                SoakConfig(
                    duration_s=duration_s,
                    screenshot_interval_s=screenshot_interval_s,
                    progress_interval_s=60.0,
                ),
                protocol=connection.protocol,
            ).run()
            return result.to_dict()

    def test_capture_crash_becomes_soak_failure(self) -> None:
        result = self._run_soak(
            self._FakeRemoteConnection(),
            screenshot_interval_s=0.001,
        )

        self.assertEqual(result["exit_code"], int(SoakExit.CRASH))
        self.assertEqual(result["status"], "CRASH")
        self.assertEqual(result["crashes"][0]["marker"], "Guru Meditation")

    def test_silent_release_requires_final_liveness_probe(self) -> None:
        connection = self._FakeReleaseConnection("")
        result = self._run_soak(connection)

        self.assertEqual(result["exit_code"], int(SoakExit.CRASH))
        self.assertFalse(result["liveness_probe"]["ok"])
        self.assertEqual(result["crashes"][0]["marker"], "release liveness probe failed")
        self.assertEqual(connection.probe_kwargs["recover_on_silence"], False)

    def test_release_final_liveness_probe_records_acknowledgement(self) -> None:
        connection = self._FakeReleaseConnection("[serial] NAV home\n")
        result = self._run_soak(connection)

        self.assertEqual(result["exit_code"], int(SoakExit.PASS))
        self.assertTrue(result["liveness_probe"]["ok"])
        self.assertEqual(result["liveness_probe"]["command"], "NAV home")
        self.assertTrue(result["liveness_probe"]["response_timestamp"])
        self.assertEqual(connection.probe_kwargs["expected"], ("[serial] NAV",))


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

    def test_pi_worker_removes_staging_after_deploy_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            args = SimpleNamespace(
                pi_host="hermes-pi",
                port="/dev/ttyACM0",
                outdir=Path(directory),
                keep_remote=False,
            )
            created = mock.Mock(returncode=0, stdout="", stderr="")
            failed_copy = mock.Mock(returncode=1, stdout="", stderr="copy failed")
            with (
                mock.patch("hw_test.hw_test_runner.find_pi_host", return_value="hermes-pi"),
                mock.patch(
                    "hw_test.hw_test_runner._run",
                    side_effect=[created, failed_copy],
                ),
                mock.patch("hw_test.hw_test_runner.cleanup_remote_stage") as cleanup,
            ):
                with self.assertRaisesRegex(RuntimeError, "cannot deploy Pi worker"):
                    _run_pi_worker(args, {})

            cleanup.assert_called_once()
            self.assertEqual(cleanup.call_args.args[0], "hermes-pi")
            self.assertRegex(
                cleanup.call_args.args[1], r"^/tmp/sigurdos-hw-test-runner-\d+$"
            )

    def test_expected_radio_variant_fails_for_no_radio_device(self) -> None:
        report = HardwareReport(mode="smoke", transport="local")
        info = DeviceInfo(
            protocol=CommandProtocol.REMOTE_TEST,
            radio_available=False,
            test_controller=True,
        )
        self.assertFalse(
            _check_variant(
                report,
                info,
                "SigurdOS_TDeck_remote_test_radio",
                PhaseSelection(smoke=True),
            )
        )
        self.assertEqual(report.results[-1].status, TestStatus.FAIL)
        self.assertTrue(report.results[-1].critical)

    def test_complete_provenance_passes_variant_gate(self) -> None:
        report = HardwareReport(mode="smoke", transport="local")
        info = DeviceInfo(
            protocol=CommandProtocol.REMOTE_TEST,
            build_environment="SigurdOS_TDeck_remote_test_radio",
            git_sha="a" * 12,
            git_dirty=False,
            meshcore_sha="b" * 12,
            radio_available=True,
            test_controller=True,
        )
        provenance = {
            "git_sha": "a" * 40,
            "git_dirty": False,
            "meshcore_sha": "b" * 40,
            "firmware_sha256": "c" * 64,
            "device_firmware_sha256": "c" * 64,
        }
        self.assertTrue(
            _check_variant(
                report,
                info,
                "SigurdOS_TDeck_remote_test_radio",
                PhaseSelection(smoke=True),
                provenance,
            )
        )
        self.assertFalse(any(result.status == TestStatus.FAIL for result in report.results))

    def test_unknown_provenance_requires_explicit_exploratory_override(self) -> None:
        info = DeviceInfo(
            protocol=CommandProtocol.REMOTE_TEST,
            radio_available=True,
            test_controller=True,
        )
        strict = HardwareReport(mode="smoke", transport="local")
        self.assertFalse(
            _check_variant(
                strict,
                info,
                "SigurdOS_TDeck_remote_test_radio",
                PhaseSelection(smoke=True),
            )
        )
        exploratory = HardwareReport(mode="smoke", transport="local")
        self.assertTrue(
            _check_variant(
                exploratory,
                info,
                "SigurdOS_TDeck_remote_test_radio",
                PhaseSelection(smoke=True),
                allow_unknown_provenance=True,
            )
        )
        self.assertEqual(exploratory.results[-1].status, TestStatus.WARN)

    def test_radio_phase_restores_original_profile_after_early_return(self) -> None:
        class FakeConnection:
            protocol = CommandProtocol.REMOTE_TEST
            device_info = DeviceInfo(
                protocol=CommandProtocol.REMOTE_TEST,
                radio_available=True,
            )

            def __init__(self) -> None:
                self.commands: list[str] = []
                self.getrf_count = 0

            def send_command(self, command, **_kwargs):
                self.commands.append(command)
                def response(output):
                    return SimpleNamespace(
                        output=output,
                        attempts=1,
                        recovered=False,
                        wire_command=command,
                    )
                if command == "getrf":
                    self.getrf_count += 1
                    profiles = (
                        "freq=869.525 SF=10 BW=250.0 CR=5 TX=7 dBm RX_BOOST=1",
                        "freq=868.100 SF=10 BW=250.0 CR=5 TX=2 dBm RX_BOOST=0",
                        "freq=869.525 SF=10 BW=250.0 CR=5 TX=7 dBm RX_BOOST=1",
                    )
                    return response("[test] getrf: " + profiles[self.getrf_count - 1])
                if command.startswith("setrf "):
                    return response("radio params saved to NVS")
                if command == "reboot":
                    return response("device restarting")
                if command == "status":
                    return response(
                        "[test] heap=100 psram=100 lvmem_used_pct=10 lvmem_free=10 "
                        "lvmem_total=100 lvmem_frag=0 stack_hwm_words=10 "
                        "stress_lvgl_min_free=10 stress_lvgl_max_used_pct=1 mesh=1"
                    )
                if command.startswith("addchannel "):
                    return response("addchannel ERROR")
                return response("OK")

            def close(self):
                return None

            def connect(self):
                return self

            def detect_firmware(self):
                return self.device_info

        connection = FakeConnection()
        report = HardwareReport(mode="radio", transport="local")
        with mock.patch("hw_test.hw_test_runner.time.sleep"):
            run_radio(
                connection,
                report,
                frequency_mhz=868.1,
                spreading_factor=10,
                bandwidth_khz=250.0,
                coding_rate=5,
                tx_power_dbm=2,
            )
        self.assertIn("setrf 869.525 10 250 5 7 1", connection.commands)
        self.assertEqual(connection.getrf_count, 3)
        self.assertEqual(
            next(item for item in report.results if item.name == "radio.verify_restore").status,
            TestStatus.PASS,
        )

    def test_nav_timeout_map_gets_extended_budget(self) -> None:
        self.assertEqual(nav_timeout_for("map"), 15.0)
        self.assertEqual(nav_timeout_for("home"), 5.0)
        self.assertEqual(nav_timeout_for("s-system"), 5.0)
        self.assertEqual(nav_silence_grace_for("map"), 3.0)
        self.assertEqual(nav_silence_grace_for("home"), 0.45)

    def test_silence_grace_cuts_late_marker_with_default_grace(self) -> None:
        class RenderThenSilence:
            def __init__(self) -> None:
                self.chunks = [
                    b"[test] > nav map\n[map] render: zoom=10 center=51.5,-0.1\n"
                ]

            def read_available(self, **_kwargs):
                if self.chunks:
                    return self.chunks.pop(0)
                return b""

            def drain(self, _seconds):
                return None

            def write(self, _data):
                return 1

        connection = PersistentSerial("/dev/null")
        connection.read_available = RenderThenSilence().read_available
        connection.drain = lambda _seconds: None
        connection.write = lambda _data: 1
        response = connection.send_command(
            "nav map",
            timeout_s=15.0,
            expected=("nav -> map",),
            recover_on_silence=False,
        )
        self.assertNotIn("nav -> map", response.output)

    def test_extended_silence_grace_captures_late_marker(self) -> None:
        class RenderThenLateMarker:
            def __init__(self) -> None:
                self.chunks = [
                    b"[test] > nav map\n[map] render: zoom=10 center=51.5,-0.1\n",
                    b"",
                    b"",
                    b"[test] nav -> map\n",
                ]

            def read_available(self, **_kwargs):
                if self.chunks:
                    return self.chunks.pop(0)
                return b""

            def drain(self, _seconds):
                return None

            def write(self, _data):
                return 1

        connection = PersistentSerial("/dev/null")
        connection.read_available = RenderThenLateMarker().read_available
        connection.drain = lambda _seconds: None
        connection.write = lambda _data: 1
        response = connection.send_command(
            "nav map",
            timeout_s=15.0,
            expected=("nav -> map",),
            recover_on_silence=False,
            silence_grace_s=3.0,
        )
        self.assertIn("nav -> map", response.output)

    def test_wait_for_mesh_ready_polls_until_ready(self) -> None:
        class FakeConnection:
            def __init__(self) -> None:
                self.status_calls = 0

            def send_command(self, command, **_kwargs):
                self.status_calls += 1
                mesh = "1" if self.status_calls >= 2 else "0"
                return SimpleNamespace(
                    output=(
                        "[test] heap=100 psram=100 lvmem_used_pct=10 "
                        f"lvmem_free=10 lvmem_total=100 lvmem_frag=0 "
                        f"stack_hwm_words=10 stress_lvgl_min_free=10 "
                        f"stress_lvgl_max_used_pct=1 mesh={mesh}"
                    ),
                    attempts=1,
                    recovered=False,
                    wire_command=command,
                )

        connection = FakeConnection()
        report = HardwareReport(mode="radio", transport="local")
        with mock.patch("hw_test.hw_test_runner.time.sleep"):
            self.assertTrue(_wait_for_mesh_ready(connection, report, timeout_s=45.0))
        self.assertEqual(connection.status_calls, 2)
        result = next(item for item in report.results if item.name == "radio.mesh_ready")
        self.assertEqual(result.status, TestStatus.PASS)

    def test_wait_for_mesh_ready_times_out_as_critical(self) -> None:
        class FakeConnection:
            def send_command(self, command, **_kwargs):
                return SimpleNamespace(
                    output=(
                        "[test] heap=100 psram=100 lvmem_used_pct=10 "
                        "lvmem_free=10 lvmem_total=100 lvmem_frag=0 "
                        "stack_hwm_words=10 stress_lvgl_min_free=10 "
                        "stress_lvgl_max_used_pct=1 mesh=0"
                    ),
                    attempts=1,
                    recovered=False,
                    wire_command=command,
                )

        report = HardwareReport(mode="radio", transport="local")
        with mock.patch("hw_test.hw_test_runner.time.sleep"):
            self.assertFalse(_wait_for_mesh_ready(FakeConnection(), report, timeout_s=0.1))
        result = next(item for item in report.results if item.name == "radio.mesh_ready")
        self.assertEqual(result.status, TestStatus.FAIL)
        self.assertTrue(result.critical)

    def test_radio_restore_retries_setrf_until_marker_seen(self) -> None:
        class FakeRestoreConnection:
            def __init__(self) -> None:
                self.setrf_attempts = 0

            def send_command(self, command, **_kwargs):
                def response(output):
                    return SimpleNamespace(
                        output=output,
                        attempts=1,
                        recovered=False,
                        wire_command=command,
                    )

                if command.startswith("setrf "):
                    self.setrf_attempts += 1
                    if self.setrf_attempts < 3:
                        return response("")  # marker lost in the serial flood
                    return response("radio params saved to NVS")
                if command == "getrf":
                    return response(
                        "[test] getrf: freq=869.525 SF=10 BW=250.0 CR=5 TX=7 dBm RX_BOOST=1"
                    )
                if command == "reboot":
                    return response("device restarting")
                return response("OK")

            def close(self):
                return None

            def connect(self):
                return self

            def detect_firmware(self):
                return DeviceInfo(protocol=CommandProtocol.REMOTE_TEST, radio_available=True)

        connection = FakeRestoreConnection()
        report = HardwareReport(mode="radio", transport="local")
        with mock.patch("hw_test.hw_test_runner.time.sleep"):
            _restore_radio_profile(connection, report, (869.525, 10, 250.0, 5, 7, True))
        self.assertEqual(connection.setrf_attempts, 3)
        restore = next(item for item in report.results if item.name == "radio.restore")
        self.assertEqual(restore.status, TestStatus.PASS)
        self.assertFalse(restore.critical)
        verify = next(item for item in report.results if item.name == "radio.verify_restore")
        self.assertEqual(verify.status, TestStatus.PASS)

    def test_radio_channel_retries_addchannel_until_ack(self) -> None:
        class FakeChannelConnection:
            protocol = CommandProtocol.REMOTE_TEST
            device_info = DeviceInfo(
                protocol=CommandProtocol.REMOTE_TEST,
                radio_available=True,
            )

            def __init__(self) -> None:
                self.addchannel_attempts = 0

            def send_command(self, command, **_kwargs):
                def response(output):
                    return SimpleNamespace(
                        output=output,
                        attempts=1,
                        recovered=False,
                        wire_command=command,
                    )

                if command.startswith("addchannel "):
                    self.addchannel_attempts += 1
                    if self.addchannel_attempts < 3:
                        # First store commit blocks (SPIFFS GC): echo only.
                        return response(f"[test] > {command}")
                    return response(
                        f"[test] > {command}\n[test] addchannel OK: hashtag #sigurd-hwtest-12345"
                    )
                if command.startswith("setrf "):
                    return response("radio params saved to NVS")
                if command == "getrf":
                    return response(
                        "[test] getrf: freq=868.100 SF=10 BW=250.0 CR=5 TX=2 dBm RX_BOOST=0"
                    )
                if command == "reboot":
                    return response("device restarting")
                if command == "status":
                    return response(
                        "[test] heap=100 psram=100 lvmem_used_pct=10 lvmem_free=10 "
                        "lvmem_total=100 lvmem_frag=0 stack_hwm_words=10 "
                        "stress_lvgl_min_free=10 stress_lvgl_max_used_pct=1 mesh=1"
                    )
                if command.startswith("removechannel "):
                    return response("OK")
                return response("OK")

            def close(self):
                return None

            def connect(self):
                return self

            def detect_firmware(self):
                return DeviceInfo(protocol=CommandProtocol.REMOTE_TEST, radio_available=True)

        connection = FakeChannelConnection()
        report = HardwareReport(mode="radio", transport="local")
        with mock.patch("hw_test.hw_test_runner.time.sleep"):
            run_radio(
                connection,
                report,
                frequency_mhz=868.1,
                spreading_factor=10,
                bandwidth_khz=250.0,
                coding_rate=5,
                tx_power_dbm=2,
            )
        self.assertEqual(connection.addchannel_attempts, 3)
        channel = next(item for item in report.results if item.name == "radio.channel")
        self.assertEqual(channel.status, TestStatus.PASS)
        self.assertEqual(channel.data.get("attempts"), 3)

    def test_crash_recovery_refuses_non_telemetry_firmware(self) -> None:
        report = HardwareReport(mode="crash_recovery", transport="local")
        info = DeviceInfo(
            protocol=CommandProtocol.REMOTE_TEST,
            build_environment="SigurdOS_TDeck_remote_test",
            test_controller=True,
        )
        self.assertFalse(
            _check_variant(
                report,
                info,
                "SigurdOS_TDeck_telemetry",
                PhaseSelection(crash_recovery=True),
            )
        )
        self.assertEqual(report.results[-1].name, "crash.capability")
        self.assertTrue(report.results[-1].critical)

    def test_crash_recovery_requires_flash_summary_and_retention(self) -> None:
        class FakeConnection:
            protocol = CommandProtocol.REMOTE_TEST

            def __init__(self) -> None:
                self.commands: list[str] = []
                self.close_count = 0
                self.connect_count = 0

            def send_command(self, command: str, **_: object) -> SimpleNamespace:
                self.commands.append(command)
                output = {
                    "crash clear": "@ok|cmd=crash clear\n",
                    "crash test": "[telemetry] crash test triggered\n",
                    "crash report": "@crash|reason=4|core_dump=1|pc=1107362629\n",
                }[command]
                return SimpleNamespace(
                    output=output,
                    attempts=1,
                    recovered=False,
                    wire_command=command,
                )

            def close(self) -> None:
                self.close_count += 1

            def connect(self) -> None:
                self.connect_count += 1

            def detect_firmware(self) -> DeviceInfo:
                return DeviceInfo(
                    protocol=CommandProtocol.REMOTE_TEST,
                    build_environment="SigurdOS_TDeck_telemetry",
                    test_controller=True,
                    evidence="telemetry ready",
                )

        connection = FakeConnection()
        report = HardwareReport(mode="crash_recovery", transport="local")
        run_crash_recovery(connection, report, reboot_pause_s=0)

        self.assertEqual(
            connection.commands,
            ["crash clear", "crash test", "crash report", "crash report"],
        )
        self.assertEqual(connection.close_count, 1)
        self.assertEqual(connection.connect_count, 1)
        self.assertEqual(
            [result.name for result in report.results],
            [
                "crash.clear_stale",
                "crash.trigger",
                "crash.reboot",
                "crash.flash_summary",
                "crash.retention",
            ],
        )
        self.assertTrue(all(result.status == TestStatus.PASS for result in report.results))

    def test_crash_recovery_cli_requires_explicit_confirmation(self) -> None:
        parser = build_parser()
        args = parser.parse_args(
            ["--crash-recovery", "--local", "--port", "/dev/ttyACM0"]
        )
        with mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            validate_args(parser, args)


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

    def test_runner_rejects_digest_only_firmware_provenance(self) -> None:
        runner = PACKAGE_PARENT / "hw_test" / "hw_test_runner.py"
        result = subprocess.run(
            [
                sys.executable,
                str(runner),
                "--smoke",
                "--local",
                "--port",
                "/dev/null",
                "--flash",
                "--firmware",
                "/tmp/firmware-merged.bin",
                "--sha256",
                "a" * 64,
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("--sha256 alone cannot identify", result.stderr)

    def test_remote_controller_reports_provenance_without_telemetry(self) -> None:
        controller = (PACKAGE_PARENT.parent / "src/test/test_controller.cpp").read_text()
        self.assertIn('strcmp(cmd, "buildinfo")', controller)
        self.assertIn("build.git_sha", controller)
        self.assertIn("build.meshcore_sha", controller)
        self.assertIn("build.build_env", controller)

    def test_pi_host_discovery_falls_back_after_timeout(self) -> None:
        reachable = mock.Mock(returncode=0, stdout="", stderr="")
        with mock.patch(
            "hw_test.hw_flash._run",
            side_effect=[FlashError("mDNS timed out"), reachable],
        ):
            # PI_HOSTS order: hermes-portable, hermes-pi.local, hermes-pi, ...
            # First candidate times out; second is reachable.
            self.assertEqual(find_pi_host(), "hermes-pi.local")

    def test_pi_flash_removes_staging_after_transfer_failure(self) -> None:
        flasher = HardwareFlasher(
            Path("/tmp/repo"),
            pi_mode=True,
            port="/dev/ttyACM0",
            pi_host="hermes-pi",
        )
        mkdir = mock.Mock(returncode=0, stdout="", stderr="")
        failed_copy = mock.Mock(returncode=1, stdout="", stderr="copy failed")
        cleanup = mock.Mock(returncode=0, stdout="", stderr="")
        with (
            mock.patch("hw_test.hw_flash.find_pi_host", return_value="hermes-pi"),
            mock.patch(
                "hw_test.hw_flash._run",
                side_effect=[mkdir, failed_copy, cleanup],
            ) as run,
        ):
            with self.assertRaisesRegex(FlashError, "firmware transfer failed"):
                flasher._flash_pi(Path("/tmp/firmware-merged.bin"))

        cleanup_command = run.call_args_list[-1].args[0]
        self.assertEqual(cleanup_command[:5], ["ssh", "hermes-pi", "rm", "-rf", "--"])
        self.assertRegex(cleanup_command[5], r"^/tmp/sigurdos-hw-flash-\d+$")

    def test_pi_flash_keep_remote_opt_in_skips_cleanup(self) -> None:
        flasher = HardwareFlasher(
            Path("/tmp/repo"),
            pi_mode=True,
            port="/dev/ttyACM0",
            pi_host="hermes-pi",
            keep_remote=True,
        )
        ok = mock.Mock(returncode=0, stdout="Hash of data verified", stderr="")
        with (
            mock.patch("hw_test.hw_flash.find_pi_host", return_value="hermes-pi"),
            mock.patch("hw_test.hw_flash._run", side_effect=[ok, ok, ok]) as run,
        ):
            self.assertEqual(
                flasher._flash_pi(Path("/tmp/firmware-merged.bin")),
                ("hermes-pi", "Hash of data verified"),
            )

        self.assertEqual(run.call_count, 3)

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
