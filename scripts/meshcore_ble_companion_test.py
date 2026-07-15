#!/usr/bin/env python3
"""Autonomous MeshCore companion-protocol test harness for SigurdOS-TDeck.

Uses the open-source `meshcore` Python package — the same companion host protocol
spoken by Liam Cottle's official MeshCore clients (Android/iOS/Web) over BLE NUS
or USB stream framing.

Transports
----------
  ble-health  Scan for MeshCore-* / Nordic UART Service (no pairing). Validates
              that the T-Deck is advertising the official companion BLE service.
  ble         Full protocol matrix over BLE (requires BlueZ MITM PIN pairing).
  usb         Full protocol matrix over USB serial companion framing
              (Heltec/T-Deck companion_usb). Same opcodes as the phone app.

Typical agent runs on hermes-pi (T-Deck USB + Bluetooth)::

    python3 meshcore_ble_companion_test.py --auto --transport ble-health
    python3 meshcore_ble_companion_test.py --auto --transport ble   # needs pairing

    # Full opcode matrix against a stock companion (Heltec USB on Hermes VM):
    python3 meshcore_ble_companion_test.py --transport usb --serial /dev/ttyUSB0 --no-serial

Exit codes: 0=pass, 1=partial fail, 2=setup/connect fail.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import os
import re
import sys
import time
from dataclasses import asdict, dataclass, field
from typing import Any, Optional

LOG = logging.getLogger("meshcore_ble_test")
NUS_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"


def _open_serial(port: str, baud: int = 115200, boot_wait: float = 10.0):
    import serial

    ser = serial.Serial(port, baud, timeout=0.5)
    time.sleep(boot_wait)
    ser.reset_input_buffer()
    return ser


def serial_cmd(ser, cmd: str, wait: float = 1.5) -> str:
    ser.reset_input_buffer()
    ser.write((cmd.strip() + "\n").encode("utf-8"))
    ser.flush()
    deadline = time.time() + wait
    chunks: list[bytes] = []
    while time.time() < deadline:
        n = ser.in_waiting
        if n:
            chunks.append(ser.read(n))
        else:
            time.sleep(0.05)
    return b"".join(chunks).decode("utf-8", "replace")


def parse_ble_status(text: str) -> dict[str, Any]:
    m = re.search(
        r"\[ble\]\s+available=(\d+)\s+enabled=(\d+)\s+connected=(\d+)\s+"
        r"pin=(\d+)\s+name=(.+?)\s+last_sync=(\d+)",
        text,
    )
    if m:
        return {
            "available": int(m.group(1)),
            "enabled": int(m.group(2)),
            "connected": int(m.group(3)),
            "pin": m.group(4),
            "name": m.group(5).strip(),
            "last_sync": int(m.group(6)),
        }
    out: dict[str, Any] = {}
    m = re.search(r"\[ble\]\s+pin=(\d+)", text)
    if m:
        out["pin"] = m.group(1)
    m = re.search(r"pin=(\d{6})", text)
    if m and "pin" not in out:
        out["pin"] = m.group(1)
    return out


def ensure_ble_ready(ser) -> dict[str, Any]:
    on_txt = serial_cmd(ser, "ble on", wait=3.0)
    LOG.info("ble on => %s", on_txt.strip().replace("\n", " | ")[:240])
    st_txt = serial_cmd(ser, "ble status", wait=2.0)
    LOG.info("ble status => %s", st_txt.strip().replace("\n", " | ")[:240])
    status = parse_ble_status(st_txt + "\n" + on_txt)
    if not status.get("pin"):
        pin_txt = serial_cmd(ser, "ble pin", wait=1.5)
        status.update(parse_ble_status(pin_txt))
    if not status.get("available", 1):
        raise RuntimeError(
            "Companion BLE unavailable — flash SigurdOS_TDeck_ble_agent"
        )
    if not status.get("pin"):
        raise RuntimeError("Could not read BLE PIN from serial (`ble pin`)")
    if status.get("enabled") == 0:
        serial_cmd(ser, "ble on", wait=2.5)
        status = parse_ble_status(serial_cmd(ser, "ble status", 2.0)) or status
    return status


async def scan_meshcore(seconds: float = 10.0) -> list[dict[str, str]]:
    from bleak import BleakScanner

    found: dict[str, dict[str, str]] = {}

    def cb(device, adv):
        name = adv.local_name or device.name or ""
        uuids = [u.lower() for u in (adv.service_uuids or [])]
        if name.startswith("MeshCore") or NUS_UUID in uuids:
            found[device.address] = {
                "address": device.address,
                "name": name or "(no-name)",
                "rssi": str(getattr(adv, "rssi", "")),
                "uuids": ",".join(uuids),
            }

    scanner = BleakScanner(detection_callback=cb)
    await scanner.start()
    await asyncio.sleep(seconds)
    await scanner.stop()
    return list(found.values())


@dataclass
class CaseResult:
    name: str
    ok: bool
    detail: str = ""
    data: dict[str, Any] = field(default_factory=dict)


async def run_protocol_matrix(mc) -> list[CaseResult]:
    results: list[CaseResult] = []

    def add(name: str, ok: bool, detail: str = "", **data: Any) -> None:
        results.append(CaseResult(name=name, ok=ok, detail=detail, data=data))
        LOG.log(
            logging.INFO if ok else logging.ERROR,
            "%s: %s %s",
            "PASS" if ok else "FAIL",
            name,
            detail,
        )

    add("connect", True, "session established")

    try:
        ev = await mc.commands.send_device_query()
        add(
            "device_query",
            True,
            f"event={getattr(ev, 'type', ev)}",
            payload=str(getattr(ev, "payload", ""))[:300],
        )
    except Exception as e:
        add("device_query", False, f"{type(e).__name__}: {e}")

    try:
        info = mc.self_info
        add(
            "self_info",
            bool(info),
            f"keys={list(info.keys()) if isinstance(info, dict) else type(info)}",
        )
    except Exception as e:
        add("self_info", False, f"{type(e).__name__}: {e}")

    try:
        now = int(time.time())
        await mc.commands.set_time(now)
        add("set_device_time", True, f"ts={now}")
    except Exception as e:
        add("set_device_time", False, f"{type(e).__name__}: {e}")

    try:
        ev = await mc.commands.get_time()
        add("get_device_time", True, f"event={getattr(ev, 'type', ev)}")
    except Exception as e:
        add("get_device_time", False, f"{type(e).__name__}: {e}")

    try:
        await mc.commands.get_contacts()
        n = len(mc.contacts) if hasattr(mc, "contacts") else -1
        add("get_contacts", True, f"contacts={n}")
    except Exception as e:
        add("get_contacts", False, f"{type(e).__name__}: {e}")

    synced = 0
    try:
        for _ in range(32):
            ev = await mc.commands.get_msg()
            et = str(getattr(ev, "type", ev)).upper()
            if "NO_MORE" in et.replace(" ", "_") or "NOMORE" in et.replace("_", ""):
                break
            if "ERROR" in et:
                break
            synced += 1
        add("sync_next_message", True, f"drained={synced}")
    except Exception as e:
        add("sync_next_message", False, f"{type(e).__name__}: {e}")

    try:
        ev = await mc.commands.get_channel(0)
        add("get_channel_0", True, f"event={getattr(ev, 'type', ev)}")
    except Exception as e:
        add("get_channel_0", False, f"{type(e).__name__}: {e}")

    try:
        ev = await mc.commands.get_bat()
        add("get_batt_storage", True, f"event={getattr(ev, 'type', ev)}")
    except Exception as e:
        add("get_batt_storage", False, f"{type(e).__name__}: {e}")

    try:
        await mc.disconnect()
        add("disconnect", True)
    except Exception as e:
        add("disconnect", False, f"{type(e).__name__}: {e}")

    return results


async def connect_usb(port: str, baud: int, timeout: float, debug: bool):
    from meshcore import MeshCore

    return await MeshCore.create_serial(
        port, baud, debug=debug, default_timeout=timeout, auto_reconnect=False
    )


async def connect_ble(address: Optional[str], pin: Optional[str], timeout: float, debug: bool):
    from meshcore import MeshCore

    return await MeshCore.create_ble(
        address=address,
        pin=str(pin) if pin is not None else None,
        debug=debug,
        default_timeout=timeout,
        auto_reconnect=False,
    )


def main(argv: Optional[list[str]] = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument(
        "--transport",
        choices=["ble-health", "ble", "usb"],
        default="ble-health",
        help="ble-health=scan only; ble=full BLE matrix; usb=USB companion matrix",
    )
    p.add_argument(
        "--serial",
        default=os.environ.get("TDECK_PORT", "/dev/ttyACM0"),
        help="USB serial port",
    )
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--boot-wait", type=float, default=10.0)
    p.add_argument("--address", default=None, help="BLE MAC address")
    p.add_argument("--pin", default=None, help="6-digit BLE pairing PIN")
    p.add_argument("--timeout", type=float, default=8.0)
    p.add_argument("--scan-seconds", type=float, default=10.0)
    p.add_argument(
        "--auto",
        action="store_true",
        help="For ble/ble-health: enable BLE via T-Deck serial and read PIN",
    )
    p.add_argument("--no-serial", action="store_true")
    p.add_argument("--json-out", default=None)
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )

    pin = args.pin
    address = args.address
    ser = None
    payload: dict[str, Any] = {"transport": args.transport}

    try:
        if args.transport in ("ble-health", "ble") and (args.auto or not args.no_serial):
            LOG.info("Opening T-Deck serial %s", args.serial)
            ser = _open_serial(args.serial, baud=args.baud, boot_wait=args.boot_wait)
            status = ensure_ble_ready(ser)
            pin = pin or status.get("pin")
            payload["device_status"] = {k: status[k] for k in status if k != "pin"}
            payload["pin_present"] = bool(pin)
            if args.transport == "ble":
                ser.close()
                ser = None
                time.sleep(1.0)
            else:
                time.sleep(2.0)

        if args.transport == "ble-health":
            devices = asyncio.run(scan_meshcore(args.scan_seconds))
            payload["devices"] = devices
            payload["summary"] = {
                "total": 1,
                "passed": 1 if devices else 0,
                "failed": 0 if devices else 1,
            }
            payload["results"] = [
                {
                    "name": "ble_advertise",
                    "ok": bool(devices),
                    "detail": f"found={len(devices)}",
                    "data": {"devices": devices},
                }
            ]
            print(json.dumps(payload, indent=2))
            if args.json_out:
                with open(args.json_out, "w", encoding="utf-8") as f:
                    json.dump(payload, f, indent=2)
            return 0 if devices else 2

        if args.transport == "usb":
            mc = asyncio.run(
                connect_usb(args.serial, args.baud, args.timeout, args.verbose)
            )
            if mc is None:
                raise RuntimeError("MeshCore.create_serial failed")
            results = asyncio.run(run_protocol_matrix(mc))
        else:
            devices = asyncio.run(scan_meshcore(args.scan_seconds))
            payload["devices"] = devices
            if not address and devices:
                address = devices[0]["address"]
            if not address:
                raise RuntimeError("No MeshCore BLE advertiser found")
            if not pin:
                raise RuntimeError("BLE PIN required (use --auto or --pin)")
            LOG.info("Connecting BLE address=%s pin=******", address)
            mc = asyncio.run(connect_ble(address, pin, args.timeout, args.verbose))
            if mc is None:
                raise RuntimeError(
                    "MeshCore.create_ble failed — BlueZ MITM pairing often needs a "
                    "registered agent; see docs/COMPANION_BLE_TEST_ENV.md"
                )
            results = asyncio.run(run_protocol_matrix(mc))

        payload["results"] = [asdict(r) for r in results]
        payload["summary"] = {
            "total": len(results),
            "passed": sum(1 for r in results if r.ok),
            "failed": sum(1 for r in results if not r.ok),
        }
        payload["address"] = address
        payload["pin_present"] = bool(pin)
        print(json.dumps(payload, indent=2))
        if args.json_out:
            with open(args.json_out, "w", encoding="utf-8") as f:
                json.dump(payload, f, indent=2)

        failed = [r for r in results if not r.ok]
        required = {
            "connect",
            "device_query",
            "self_info",
            "set_device_time",
            "get_contacts",
            "sync_next_message",
        }
        if any(r.name == "connect" and not r.ok for r in results):
            return 2
        if any(r.name in required and not r.ok for r in results):
            return 1
        return 1 if failed else 0

    except Exception as e:
        LOG.exception("Setup failed: %s", e)
        payload["error"] = str(e)
        print(json.dumps(payload, indent=2))
        if args.json_out:
            with open(args.json_out, "w", encoding="utf-8") as f:
                json.dump(payload, f, indent=2)
        return 2
    finally:
        if ser is not None:
            try:
                ser.close()
            except Exception:
                pass


if __name__ == "__main__":
    sys.exit(main())
