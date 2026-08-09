"""Regression tests for privacy-safe BLE and NVS evidence diagnostics."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
BLE_SCRIPT = ROOT / "scripts" / "meshcore_ble_companion_test.py"
NVS_SCRIPT = ROOT / "scripts" / "validation" / "nvs_boot_marker_check.py"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


BLE = load_module("meshcore_ble_companion_test_privacy", BLE_SCRIPT)
NVS = load_module("nvs_boot_marker_check_regression", NVS_SCRIPT)


def set_entry_state(page: bytearray, slot: int, state: int) -> None:
    offset = NVS.ENTRY_STATE_TABLE_OFFSET + slot // 4
    shift = (slot % 4) * 2
    page[offset] = (page[offset] & ~(0x03 << shift)) | (state << shift)


def make_page(sequence: int = 1) -> bytearray:
    page = bytearray(b"\xff" * NVS.PAGE_SIZE)
    struct.pack_into("<I", page, 0, NVS.PAGE_ACTIVE)
    struct.pack_into("<I", page, 4, sequence)
    page[8] = NVS.NVS_VERSION
    struct.pack_into("<I", page, 28, NVS.crc32_le(bytes(page[4:28])))
    return page


def write_entry(
    page: bytearray,
    slot: int,
    namespace_index: int,
    entry_type: int,
    key: str,
    data: bytes,
    *,
    span: int = 1,
    chunk_index: int = 0xFF,
) -> None:
    entry = bytearray(b"\xff" * NVS.ENTRY_SIZE)
    entry[0:4] = bytes((namespace_index, entry_type, span, chunk_index))
    entry[8:24] = key.encode("ascii").ljust(16, b"\x00")
    entry[24:32] = data[:8].ljust(8, b"\xff")
    struct.pack_into("<I", entry, 4, NVS.crc32_le(bytes(entry[:4] + entry[8:32])))
    start = NVS.ENTRY_DATA_OFFSET + slot * NVS.ENTRY_SIZE
    page[start : start + NVS.ENTRY_SIZE] = entry
    set_entry_state(page, slot, NVS.ENTRY_WRITTEN)


def write_string(page: bytearray, slot: int, namespace_index: int, key: str, value: bytes) -> int:
    data = value + b"\x00"
    span = (len(data) + NVS.ENTRY_SIZE - 1) // NVS.ENTRY_SIZE + 1
    metadata = struct.pack(
        "<HHI",
        len(data),
        0xFFFF,
        NVS.crc32_le(data),
    )
    write_entry(
        page,
        slot,
        namespace_index,
        NVS.NVS_TYPE_STR,
        key,
        metadata,
        span=span,
    )
    for index in range(1, span):
        chunk = data[(index - 1) * NVS.ENTRY_SIZE : index * NVS.ENTRY_SIZE]
        start = NVS.ENTRY_DATA_OFFSET + (slot + index) * NVS.ENTRY_SIZE
        page[start : start + NVS.ENTRY_SIZE] = chunk.ljust(NVS.ENTRY_SIZE, b"\xff")
        set_entry_state(page, slot + index, NVS.ENTRY_WRITTEN)
    return span


def write_standard_records(page: bytearray, marker: bytes = b"gps-validation") -> None:
    write_entry(page, 0, 0, NVS.NVS_TYPE_U8, "gpsval", bytes((1,)))
    write_entry(page, 1, 1, NVS.NVS_TYPE_U32, "boot_count", struct.pack("<I", 7))
    write_string(page, 2, 1, "marker", marker)


class BlePrivacyTests(unittest.TestCase):
    def test_redaction_hides_pin_and_stable_identifiers(self) -> None:
        raw = (
            "[ble] available=1 enabled=1 connected=0 pin=123456 "
            "name=Sentinel Node last_sync=7 address=AA:BB:CC:DD:EE:FF "
            f"uuids={BLE.NUS_UUID}"
        )

        redacted = BLE.redact_ble_text(raw)

        self.assertIn("pin=******", redacted)
        self.assertIn("name=******", redacted)
        self.assertIn("address=******", redacted)
        self.assertIn("uuids=******", redacted)
        for sentinel in ("123456", "Sentinel Node", "AA:BB:CC:DD:EE:FF", BLE.NUS_UUID):
            self.assertNotIn(sentinel, redacted)

    def test_ensure_ble_ready_redacts_serial_diagnostics(self) -> None:
        raw_status = (
            "[ble] available=1 enabled=1 connected=0 pin=123456 "
            "name=Sentinel Node last_sync=7 address=AA:BB:CC:DD:EE:FF "
            f"uuids={BLE.NUS_UUID}"
        )
        with (
            mock.patch.object(
                BLE,
                "serial_cmd",
                side_effect=["ble on pin=123456", raw_status],
            ),
            self.assertLogs(BLE.LOG, level="INFO") as captured,
        ):
            status = BLE.ensure_ble_ready(mock.Mock())

        self.assertEqual(status["pin"], "123456")
        serialized_logs = "\n".join(captured.output)
        for sentinel in ("123456", "Sentinel Node", "AA:BB:CC:DD:EE:FF", BLE.NUS_UUID):
            self.assertNotIn(sentinel, serialized_logs)

    def test_default_scan_evidence_is_an_allowlist(self) -> None:
        devices = [
            {
                "address": "AA:BB:CC:DD:EE:FF",
                "name": "Sentinel Node",
                "rssi": "-40",
                "uuids": BLE.NUS_UUID,
            }
        ]

        self.assertEqual(
            BLE.ble_scan_evidence(devices),
            {"advertiser_count": 1, "nus_present": True, "passed": True},
        )

    def test_default_json_omits_pin_name_and_device_identifiers(self) -> None:
        devices = [
            {
                "address": "AA:BB:CC:DD:EE:FF",
                "name": "Sentinel Node",
                "rssi": "-40",
                "uuids": BLE.NUS_UUID,
            }
        ]

        async def fake_run(*_args):
            return None, devices, [
                BLE.CaseResult(
                    name="ble_advertise",
                    ok=True,
                    data=BLE.ble_scan_evidence(devices),
                )
            ]

        fake_serial = mock.Mock()
        status = {
            "available": 1,
            "enabled": 1,
            "connected": 0,
            "pin": "123456",
            "name": "Sentinel Node",
            "last_sync": 7,
        }
        output = io.StringIO()
        with (
            mock.patch.object(BLE, "_open_serial", return_value=fake_serial),
            mock.patch.object(BLE, "ensure_ble_ready", return_value=status),
            mock.patch.object(BLE, "run_protocol_invocation", side_effect=fake_run),
            mock.patch.object(BLE.time, "sleep"),
            contextlib.redirect_stdout(output),
        ):
            with tempfile.TemporaryDirectory() as directory:
                evidence = Path(directory) / "ble.json"
                self.assertEqual(
                    BLE.main(
                        [
                            "--auto",
                            "--transport",
                            "ble-health",
                            "--json-out",
                            str(evidence),
                        ]
                    ),
                    0,
                )
                evidence_text = evidence.read_text(encoding="utf-8")
                serialized = output.getvalue() + evidence_text
                parsed = json.loads(evidence_text)

        self.assertNotIn("123456", serialized)
        self.assertNotIn("Sentinel Node", serialized)
        self.assertNotIn("AA:BB:CC:DD:EE:FF", serialized)
        self.assertNotIn(BLE.NUS_UUID, serialized)
        self.assertEqual(
            parsed["ble_scan"],
            {"advertiser_count": 1, "nus_present": True, "passed": True},
        )

    def test_default_error_output_does_not_reintroduce_ble_identifiers(self) -> None:
        failure = (
            "address=AA:BB:CC:DD:EE:FF name=Sentinel Node pin=123456 "
            f"uuid={BLE.NUS_UUID}"
        )
        output = io.StringIO()
        with (
            mock.patch.object(
                BLE,
                "run_protocol_invocation",
                side_effect=RuntimeError(failure),
            ),
            contextlib.redirect_stdout(output),
            self.assertLogs(BLE.LOG, level="ERROR") as captured,
        ):
            self.assertEqual(
                BLE.main(["--transport", "ble-health", "--no-serial"]),
                2,
            )

        serialized = output.getvalue() + "\n".join(captured.output)
        for sentinel in ("123456", "Sentinel Node", "AA:BB:CC:DD:EE:FF", BLE.NUS_UUID):
            self.assertNotIn(sentinel, serialized)

    def test_identifier_opt_in_is_marked_non_publishable(self) -> None:
        devices = [
            {
                "address": "AA:BB:CC:DD:EE:FF",
                "name": "Sentinel Node",
                "rssi": "-40",
                "uuids": BLE.NUS_UUID,
            }
        ]

        async def fake_run(*_args):
            return None, devices, [
                BLE.CaseResult(name="ble_advertise", ok=True, data={"devices": devices})
            ]

        output = io.StringIO()
        with (
            mock.patch.object(BLE, "run_protocol_invocation", side_effect=fake_run),
            contextlib.redirect_stdout(output),
        ):
            self.assertEqual(
                BLE.main(
                    [
                        "--transport",
                        "ble-health",
                        "--no-serial",
                        "--include-device-identifiers",
                    ]
                ),
                0,
            )

        payload = json.loads(output.getvalue())
        self.assertFalse(payload["privacy"]["publishable"])
        self.assertTrue(payload["privacy"]["device_identifiers_included"])
        self.assertEqual(payload["ble_scan"]["devices"], devices)


class NvsEvidenceTests(unittest.TestCase):
    def write_image(self, pages: list[bytearray]) -> Path:
        handle = tempfile.NamedTemporaryFile(suffix=".bin", delete=False)
        path = Path(handle.name)
        handle.write(b"".join(bytes(page) for page in pages))
        handle.close()
        self.addCleanup(path.unlink, missing_ok=True)
        return path

    def require(self, pages: list[bytearray]) -> bool:
        return NVS.main(["--require", str(self.write_image(pages))]) == 0

    def test_live_crc_valid_string_is_accepted(self) -> None:
        page = make_page()
        write_standard_records(page)
        self.assertTrue(self.require([page]))

    def test_erased_record_bytes_do_not_count(self) -> None:
        page = make_page()
        write_standard_records(page)
        set_entry_state(page, 2, NVS.ENTRY_ERASED)
        self.assertFalse(self.require([page]))

    def test_newer_page_sequence_wins_over_older_duplicate(self) -> None:
        old = make_page(sequence=1)
        new = make_page(sequence=2)
        write_standard_records(old, marker=b"old-marker")
        write_standard_records(new, marker=b"gps-validation")
        self.assertTrue(self.require([old, new]))
        self.assertFalse(NVS.summarize(self.write_image([old, new])).has_marker_value("old-marker"))

    def test_corrupt_entry_crc_is_rejected(self) -> None:
        page = make_page()
        write_standard_records(page)
        page[NVS.ENTRY_DATA_OFFSET + 2 * NVS.ENTRY_SIZE + 8] ^= 0x01
        self.assertFalse(self.require([page]))

    def test_corrupt_variable_data_crc_is_rejected(self) -> None:
        page = make_page()
        write_standard_records(page)
        continuation = NVS.ENTRY_DATA_OFFSET + 3 * NVS.ENTRY_SIZE
        page[continuation] ^= 0x01
        self.assertFalse(self.require([page]))

    def test_truncated_multispan_record_is_rejected(self) -> None:
        page = make_page()
        write_entry(page, 0, 0, NVS.NVS_TYPE_U8, "gpsval", bytes((1,)))
        write_entry(page, 1, 1, NVS.NVS_TYPE_U32, "boot_count", struct.pack("<I", 7))
        data = b"gps-validation\x00"
        metadata = struct.pack("<HHI", len(data), 0xFFFF, NVS.crc32_le(data))
        write_entry(page, 2, 1, NVS.NVS_TYPE_STR, "marker", metadata, span=2)
        self.assertFalse(self.require([page]))

    def test_unrelated_marker_string_does_not_satisfy_marker_key(self) -> None:
        page = make_page()
        write_entry(page, 0, 0, NVS.NVS_TYPE_U8, "gpsval", bytes((1,)))
        write_entry(page, 1, 1, NVS.NVS_TYPE_U32, "boot_count", struct.pack("<I", 7))
        write_string(page, 2, 1, "other", b"gps-validation")
        self.assertFalse(self.require([page]))

    def test_corrupt_page_header_is_not_evidence(self) -> None:
        page = make_page()
        write_standard_records(page)
        page[28] ^= 0x01
        self.assertFalse(self.require([page]))

    def test_truncated_image_is_rejected(self) -> None:
        page = make_page()
        write_standard_records(page)
        path = self.write_image([page])
        path.write_bytes(path.read_bytes()[:-1])
        self.assertNotEqual(NVS.main(["--require", str(path)]), 0)


if __name__ == "__main__":
    unittest.main()
