"""Regression tests for the companion harness asyncio lifecycle."""

from __future__ import annotations

import asyncio
import importlib.util
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "meshcore_ble_companion_test.py"
SPEC = importlib.util.spec_from_file_location("meshcore_ble_companion_test", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class FakeCommands:
    def __init__(self, loop_ids: list[int]) -> None:
        self.loop_ids = loop_ids

    def _mark(self) -> None:
        self.loop_ids.append(id(asyncio.get_running_loop()))

    async def send_device_query(self):
        self._mark()
        return SimpleNamespace(type="DEVICE_QUERY", payload={})

    async def set_time(self, _now):
        self._mark()

    async def get_time(self):
        self._mark()
        return SimpleNamespace(type="TIME", payload={})

    async def get_contacts(self):
        self._mark()

    async def get_msg(self):
        self._mark()
        return SimpleNamespace(type="NO_MORE_MESSAGES")

    async def get_channel(self, _index):
        self._mark()
        return SimpleNamespace(type="CHANNEL", payload={})

    async def get_bat(self):
        self._mark()
        return SimpleNamespace(type="BATTERY", payload={})


class FakeClient:
    def __init__(self, loop_ids: list[int]) -> None:
        self.commands = FakeCommands(loop_ids)
        self.self_info = {"name": "test"}
        self.contacts = []
        self.loop_ids = loop_ids

    async def disconnect(self) -> None:
        self.loop_ids.append(id(asyncio.get_running_loop()))


class CompanionHarnessTests(unittest.TestCase):
    def test_usb_connect_matrix_and_disconnect_share_one_loop(self) -> None:
        loop_ids: list[int] = []
        client = FakeClient(loop_ids)

        async def connect(*_args):
            loop_ids.append(id(asyncio.get_running_loop()))
            return client

        with mock.patch.object(MODULE, "connect_usb", side_effect=connect):
            _, devices, results = asyncio.run(
                MODULE.run_protocol_invocation(
                    "usb",
                    "/dev/test",
                    115200,
                    None,
                    None,
                    1.0,
                    0.1,
                    False,
                )
            )

        self.assertEqual(devices, [])
        self.assertTrue(all(result.ok for result in results))
        self.assertGreater(len(loop_ids), 1)
        self.assertEqual(len(set(loop_ids)), 1)

    def test_ble_scan_connect_matrix_and_disconnect_share_one_loop(self) -> None:
        loop_ids: list[int] = []
        client = FakeClient(loop_ids)

        async def scan(_seconds):
            loop_ids.append(id(asyncio.get_running_loop()))
            return [{"address": "AA:BB", "name": "MeshCore", "rssi": "-40", "uuids": ""}]

        async def connect(*_args):
            loop_ids.append(id(asyncio.get_running_loop()))
            return client

        with (
            mock.patch.object(MODULE, "scan_meshcore", side_effect=scan),
            mock.patch.object(MODULE, "connect_ble", side_effect=connect),
        ):
            address, devices, results = asyncio.run(
                MODULE.run_protocol_invocation(
                    "ble",
                    "/dev/test",
                    115200,
                    None,
                    "123456",
                    1.0,
                    0.1,
                    False,
                )
            )

        self.assertEqual(address, "AA:BB")
        self.assertEqual(len(devices), 1)
        self.assertTrue(all(result.ok for result in results))
        self.assertGreater(len(loop_ids), 2)
        self.assertEqual(len(set(loop_ids)), 1)


if __name__ == "__main__":
    unittest.main()
