#!/usr/bin/env python3
"""Run a host-side BLE companion smoke test against a flashed T-Deck."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import re
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  # host -> T-Deck
TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # T-Deck -> host

CMD_APP_START = 1
CMD_GET_CONTACTS = 4
CMD_GET_DEVICE_TIME = 5
CMD_SYNC_NEXT_MESSAGE = 10
CMD_GET_BATT_AND_STORAGE = 20
CMD_DEVICE_QUERY = 22
CMD_GET_STATS = 56

RESP_CONTACTS_START = 2
RESP_SELF_INFO = 5
RESP_CONTACT_MSG_RECV = 7
RESP_CHANNEL_MSG_RECV = 8
RESP_CURR_TIME = 9
RESP_NO_MORE_MESSAGES = 10
RESP_BATT_AND_STORAGE = 12
RESP_DEVICE_INFO = 13
RESP_CONTACT_MSG_RECV_V3 = 16
RESP_CHANNEL_MSG_RECV_V3 = 17
RESP_STATS = 24
RESP_CHANNEL_DATA_RECV = 27


@dataclass(frozen=True)
class BleStep:
    name: str
    frame: bytes
    expected_codes: tuple[int, ...]
    min_len: int = 1
    timeout_s: float = 10.0


DEFAULT_STEPS = [
    BleStep("device query", bytes([CMD_DEVICE_QUERY, 3]), (RESP_DEVICE_INFO,), 82),
    BleStep("app start", bytes([CMD_APP_START]) + bytes(7), (RESP_SELF_INFO,), 56),
    BleStep("contacts start", bytes([CMD_GET_CONTACTS]), (RESP_CONTACTS_START,), 5),
    BleStep("current time", bytes([CMD_GET_DEVICE_TIME]), (RESP_CURR_TIME,), 5),
    BleStep("battery and storage", bytes([CMD_GET_BATT_AND_STORAGE]), (RESP_BATT_AND_STORAGE,), 11),
    BleStep("core stats", bytes([CMD_GET_STATS, 0]), (RESP_STATS,), 2),
    BleStep(
        "sync empty or queued",
        bytes([CMD_SYNC_NEXT_MESSAGE]),
        (
            RESP_NO_MORE_MESSAGES,
            RESP_CONTACT_MSG_RECV,
            RESP_CHANNEL_MSG_RECV,
            RESP_CONTACT_MSG_RECV_V3,
            RESP_CHANNEL_MSG_RECV_V3,
            RESP_CHANNEL_DATA_RECV,
        ),
        1,
    ),
]

RECONNECT_STEPS = (
    DEFAULT_STEPS[0],
    DEFAULT_STEPS[1],
    DEFAULT_STEPS[-1],
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_out_dir() -> Path:
    stamp = datetime.now().strftime("%Y-%m-%d-%H%M%S")
    return repo_root() / ".pio" / "ble_companion_validation" / stamp


def frame_hex(frame: bytes) -> str:
    return frame.hex(" ")


def frame_summary(frame: bytes, include_frame_hex: bool) -> dict[str, Any]:
    summary: dict[str, Any] = {
        "code": frame[0] if frame else None,
        "len": len(frame),
    }
    if include_frame_hex:
        summary["hex"] = frame_hex(frame)
    return summary


def safe_error(exc: Exception) -> str:
    text = f"{type(exc).__name__}: {exc}"
    return re.sub(r" value b'[^']*'", " value <redacted>", text)


def enum_name(enum_cls: Any, value: Any) -> str:
    value_int = int(value)
    for name in dir(enum_cls):
        if name.startswith("_"):
            continue
        candidate = getattr(enum_cls, name)
        try:
            if int(candidate) == value_int:
                return name
        except Exception:
            continue
    return str(value_int)


def parse_ble_address(address: str) -> int:
    cleaned = address.replace(":", "").replace("-", "").strip()
    if len(cleaned) != 12:
        raise ValueError(f"BLE address must be 12 hex digits, got {address!r}")
    return int(cleaned, 16)


def device_name(device: Any, adv: Any | None = None) -> str:
    return (
        getattr(device, "name", None)
        or getattr(adv, "local_name", None)
        or ""
    )


async def find_device(args: argparse.Namespace):
    from bleak import BleakScanner

    if args.address:
        return args.address, {"address": args.address, "name": args.address}

    found = await BleakScanner.discover(timeout=args.scan_timeout, return_adv=True)
    candidates = []
    service_uuid_l = SERVICE_UUID.lower()
    for _addr, pair in found.items():
        device, adv = pair
        name = device_name(device, adv)
        services = [str(s).lower() for s in getattr(adv, "service_uuids", [])]
        service_match = service_uuid_l in services
        name_match = bool(name) and name.startswith(args.name_prefix)
        if service_match or name_match:
            candidates.append((device, adv, name, service_match, name_match))

    if not candidates:
        seen = [
            {
                "address": getattr(pair[0], "address", ""),
                "name": device_name(pair[0], pair[1]),
                "service_uuids": list(getattr(pair[1], "service_uuids", []) or []),
            }
            for pair in found.values()
        ]
        raise RuntimeError(
            "No MeshCore BLE candidate found. "
            f"Scanned {len(seen)} devices; first devices: {seen[:10]}"
        )

    name_matches = [item for item in candidates if item[4]]
    if name_matches:
        candidates = name_matches

    candidates.sort(key=lambda item: (not item[3], not item[4], item[2]))
    if len(candidates) > 1 and not args.allow_ambiguous:
        seen = [
            {
                "address": getattr(item[0], "address", ""),
                "name": item[2],
                "service_match": item[3],
                "name_match": item[4],
                "rssi": getattr(item[1], "rssi", None),
            }
            for item in candidates
        ]
        raise RuntimeError(
            "Multiple MeshCore BLE candidates found; rerun with --address or a narrower "
            f"--name-prefix. Candidates: {seen}"
        )
    device, adv, name, service_match, name_match = candidates[0]
    meta = {
        "address": getattr(device, "address", ""),
        "name": name,
        "service_match": service_match,
        "name_match": name_match,
        "service_uuids": list(getattr(adv, "service_uuids", []) or []),
        "rssi": getattr(adv, "rssi", None),
    }
    return device, meta


async def winrt_pin_pair(address: str, pin: str, unpair_first: bool) -> dict[str, Any]:
    if sys.platform != "win32":
        raise RuntimeError("--winrt-pin-env is only supported on Windows")
    if not re.fullmatch(r"\d{4,6}", pin):
        raise RuntimeError("PIN must be 4 to 6 decimal digits")

    from winrt.windows.devices.bluetooth import BluetoothLEDevice
    from winrt.windows.devices.enumeration import (
        DevicePairingKinds,
        DevicePairingProtectionLevel,
        DevicePairingResultStatus,
    )

    dev = await BluetoothLEDevice.from_bluetooth_address_async(parse_ble_address(address))
    if dev is None:
        raise RuntimeError(f"WinRT could not open BLE address {address}")

    result: dict[str, Any] = {}
    try:
        info = dev.device_information
        result["target_name"] = info.name
        result["initial_paired"] = bool(info.pairing.is_paired)
        result["initial_can_pair"] = bool(info.pairing.can_pair)
        result["initial_protection"] = enum_name(
            DevicePairingProtectionLevel, info.pairing.protection_level
        )

        if info.pairing.is_paired and unpair_first:
            unpair = await info.pairing.unpair_async()
            result["unpair_status"] = enum_name(DevicePairingResultStatus, unpair.status)
            dev.close()
            await asyncio.sleep(1.0)
            dev = await BluetoothLEDevice.from_bluetooth_address_async(parse_ble_address(address))
            if dev is None:
                raise RuntimeError(f"WinRT could not reopen BLE address {address}")
            info = dev.device_information

        if info.pairing.is_paired:
            result["pair_skipped"] = "already_paired"
            return result

        seen_kinds: list[str] = []
        custom = info.pairing.custom

        def handler(_sender: Any, event_args: Any) -> None:
            seen_kinds.append(enum_name(DevicePairingKinds, event_args.pairing_kind))
            if event_args.pairing_kind == DevicePairingKinds.PROVIDE_PIN:
                event_args.accept(pin)
            else:
                event_args.accept()

        token = custom.add_pairing_requested(handler)
        try:
            kinds = (
                DevicePairingKinds.PROVIDE_PIN
                | DevicePairingKinds.CONFIRM_ONLY
                | DevicePairingKinds.CONFIRM_PIN_MATCH
            )
            pair = await custom.pair_async(
                kinds,
                DevicePairingProtectionLevel.ENCRYPTION_AND_AUTHENTICATION,
            )
        finally:
            custom.remove_pairing_requested(token)

        result["pairing_kinds"] = seen_kinds
        result["pair_status"] = enum_name(DevicePairingResultStatus, pair.status)
        result["protection_used"] = enum_name(
            DevicePairingProtectionLevel, pair.protection_level_used
        )
        result["paired"] = pair.status in (
            DevicePairingResultStatus.PAIRED,
            DevicePairingResultStatus.ALREADY_PAIRED,
        )
        return result
    finally:
        dev.close()


async def run_ble(args: argparse.Namespace) -> dict[str, Any]:
    target, target_meta = await find_device(args)
    pair_info: dict[str, Any] = {"requested": args.pair}

    if args.winrt_pin_env:
        pin = os.environ.get(args.winrt_pin_env)
        if not pin:
            raise RuntimeError(f"Environment variable {args.winrt_pin_env} is not set")
        pair_info["winrt_pin_env"] = args.winrt_pin_env
        pair_info["winrt"] = await winrt_pin_pair(
            target_meta["address"],
            pin,
            args.winrt_unpair_first,
        )

    if args.pair_only:
        winrt = pair_info.get("winrt", {})
        return {
            "target": target_meta,
            "pairing": pair_info,
            "steps": [],
            "notifications": [],
            "pair_only": True,
            "passed": bool(winrt.get("paired") or winrt.get("pair_skipped")),
        }

    client_kwargs: dict[str, Any] = {
        "timeout": args.connect_timeout,
        "pair": args.pair,
    }
    sessions = [
        await run_command_session(
            target,
            args,
            client_kwargs,
            pair_info,
            DEFAULT_STEPS,
            "primary",
            explicit_pair=args.pair,
        )
    ]
    reconnect_kwargs: dict[str, Any] = {
        "timeout": args.connect_timeout,
        "pair": False,
    }
    for index in range(args.reconnects):
        await asyncio.sleep(args.reconnect_settle)
        sessions.append(
            await run_command_session(
                target,
                args,
                reconnect_kwargs,
                pair_info,
                RECONNECT_STEPS,
                f"reconnect-{index + 1}",
                explicit_pair=False,
            )
        )

    all_notifications: list[dict[str, Any]] = []
    for session in sessions:
        all_notifications.extend(session.get("notifications", []))

    return {
        "target": target_meta,
        "pairing": pair_info,
        "steps": sessions[0].get("steps", []),
        "sessions": sessions,
        "reconnects_requested": args.reconnects,
        "notifications": all_notifications,
        "passed": all(session.get("passed") for session in sessions),
    }


async def run_command_session(
    target: Any,
    args: argparse.Namespace,
    client_kwargs: dict[str, Any],
    pair_info: dict[str, Any],
    steps: list[BleStep] | tuple[BleStep, ...],
    label: str,
    explicit_pair: bool,
) -> dict[str, Any]:
    from bleak import BleakClient

    notifications: asyncio.Queue[bytes] = asyncio.Queue()
    all_notifications: list[dict[str, Any]] = []

    def on_notify(_sender: Any, data: bytearray) -> None:
        frame = bytes(data)
        all_notifications.append(frame_summary(frame, args.include_frame_hex))
        notifications.put_nowait(frame)

    steps_out: list[dict[str, Any]] = []
    session_out: dict[str, Any] = {
        "label": label,
        "steps": steps_out,
        "notifications": all_notifications,
        "passed": False,
    }
    try:
        async with BleakClient(target, **client_kwargs) as client:
            session_out["connected"] = True
            if explicit_pair:
                pair_info["explicit_attempted"] = True
                try:
                    pair_result = await asyncio.wait_for(client.pair(), timeout=args.pair_timeout)
                    pair_info["explicit_result"] = pair_result
                except Exception as exc:
                    pair_info["explicit_error"] = safe_error(exc)

            await client.start_notify(TX_UUID, on_notify)
            await asyncio.sleep(0.25)

            for step in steps:
                while not notifications.empty():
                    notifications.get_nowait()

                step_out: dict[str, Any] = {
                    "name": step.name,
                    "command_code": step.frame[0],
                    "expected_codes": list(step.expected_codes),
                    "passed": False,
                    "response_code": None,
                    "response_len": 0,
                }
                try:
                    await client.write_gatt_char(RX_UUID, step.frame, response=True)
                except Exception as exc:
                    step_out["write_error"] = safe_error(exc)
                    steps_out.append(step_out)
                    continue

                response: bytes | None = None
                try:
                    deadline = asyncio.get_running_loop().time() + step.timeout_s
                    while asyncio.get_running_loop().time() < deadline:
                        remaining = max(0.1, deadline - asyncio.get_running_loop().time())
                        candidate = await asyncio.wait_for(notifications.get(), timeout=remaining)
                        if candidate and candidate[0] in step.expected_codes:
                            response = candidate
                            break
                except asyncio.TimeoutError:
                    response = None

                ok = bool(response) and len(response) >= step.min_len
                step_out["passed"] = ok
                step_out["response_code"] = response[0] if response else None
                step_out["response_len"] = len(response) if response else 0
                if args.include_frame_hex:
                    step_out["response_hex"] = frame_hex(response) if response else ""
                steps_out.append(step_out)

            try:
                await client.stop_notify(TX_UUID)
            except Exception as exc:
                session_out["stop_notify_error"] = safe_error(exc)
    except Exception as exc:
        session_out["error"] = safe_error(exc)

    session_out["passed"] = (
        "error" not in session_out
        and len(steps_out) == len(steps)
        and all(step["passed"] for step in steps_out)
    )
    return session_out


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", help="BLE address or Windows device UUID")
    parser.add_argument("--name-prefix", default="MeshCore-")
    parser.add_argument(
        "--allow-ambiguous",
        action="store_true",
        help="allow the first sorted MeshCore candidate when multiple devices match",
    )
    parser.add_argument("--scan-timeout", type=float, default=10.0)
    parser.add_argument("--connect-timeout", type=float, default=30.0)
    parser.add_argument("--pair-timeout", type=float, default=60.0)
    parser.add_argument("--pair", action="store_true", help="request pairing during connect")
    parser.add_argument(
        "--winrt-pin-env",
        help="Windows only: env var containing the MeshCore PIN for PIN pairing",
    )
    parser.add_argument(
        "--winrt-unpair-first",
        action="store_true",
        help="Windows only: remove the cached bond before --winrt-pin-env pairing",
    )
    parser.add_argument(
        "--pair-only",
        action="store_true",
        help="stop after pairing; useful for authenticated-pair hardware evidence",
    )
    parser.add_argument(
        "--reconnects",
        type=int,
        default=0,
        help="after the primary command smoke, repeat a minimal connect/app-start/sync smoke this many times",
    )
    parser.add_argument(
        "--reconnect-settle",
        type=float,
        default=1.0,
        help="seconds to wait between BLE sessions when --reconnects is used",
    )
    parser.add_argument("--out-dir", default=str(default_out_dir()))
    parser.add_argument(
        "--include-frame-hex",
        action="store_true",
        help="debug only; raw frames may contain private payloads and are not PR-safe",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.reconnects < 0:
        print("ERROR: --reconnects must be >= 0", file=sys.stderr)
        return 2
    if args.reconnect_settle < 0:
        print("ERROR: --reconnect-settle must be >= 0", file=sys.stderr)
        return 2
    if args.pair_only and not args.winrt_pin_env:
        print("ERROR: --pair-only requires --winrt-pin-env", file=sys.stderr)
        return 2
    try:
        import bleak  # noqa: F401
    except ImportError:
        print("ERROR: bleak is required. Install with: python -m pip install bleak", file=sys.stderr)
        return 2

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    summary_path = out_dir / "summary.json"

    started = datetime.now().isoformat(timespec="seconds")
    try:
        summary = asyncio.run(run_ble(args))
        summary["started"] = started
    except Exception as exc:
        summary = {
            "started": started,
            "passed": False,
            "error": safe_error(exc),
        }

    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"BLE companion smoke {'PASS' if summary.get('passed') else 'FAIL'}")
    print(f"summary: {summary_path}")
    if "target" in summary:
        print(f"target:  {summary['target'].get('name')} {summary['target'].get('address')}")
    if summary.get("pair_only"):
        pairing = summary.get("pairing", {}).get("winrt", {})
        print(
            "pair:    "
            f"status={pairing.get('pair_status') or pairing.get('pair_skipped')} "
            f"protection={pairing.get('protection_used')}"
        )
    sessions = summary.get("sessions") or []
    if sessions:
        for session in sessions:
            session_status = "PASS" if session.get("passed") else "FAIL"
            print(f"session: {session_status} {session.get('label')}")
            if session.get("error"):
                print(f"  error: {session['error']}")
            for step in session.get("steps", []):
                status = "PASS" if step.get("passed") else "FAIL"
                print(
                    f"  {status:4} {step['name']}: "
                    f"cmd={step['command_code']} resp={step.get('response_code')} "
                    f"len={step.get('response_len')}"
                )
    else:
        for step in summary.get("steps", []):
            status = "PASS" if step.get("passed") else "FAIL"
            print(
                f"{status:4} {step['name']}: "
                f"cmd={step['command_code']} resp={step.get('response_code')} "
                f"len={step.get('response_len')}"
            )
    if "error" in summary:
        print(f"error:   {summary['error']}")
    return 0 if summary.get("passed") else 1


if __name__ == "__main__":
    raise SystemExit(main())
