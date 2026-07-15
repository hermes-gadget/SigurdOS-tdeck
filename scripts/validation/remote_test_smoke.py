#!/usr/bin/env python3
"""Run a remote-test serial smoke against a flashed T-Deck.

The script never enumerates serial ports. Pass the approved port explicitly.
It writes all serial traffic and a machine-readable summary under the selected
output directory so hardware evidence can be attached to release PRs.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

DEFAULT_BAUD = 115200
serial = None


@dataclass(frozen=True)
class SmokeStep:
    name: str
    command: str
    expect: tuple[str, ...]
    timeout_s: float = 8.0
    settle_s: float = 0.25


PROFILES: dict[str, list[SmokeStep]] = {
    "ui": [
        SmokeStep("screen starts readable", "screen", (r"\[test\] current screen:",)),
        SmokeStep("heap status", "status", (r"\[test\] heap=\d+ psram=\d+",)),
        SmokeStep("navigate chat", "nav chat", (r"\[test\] nav -> chat",)),
        SmokeStep("screen is chat", "screen", (r"\[test\] current screen: chat",)),
        SmokeStep(
            "inject channel message",
            "inject RC2Bot channel=general rc2-smoke-inbound",
            (r"\[test\] injected channel msg from RC2Bot in #general:",),
        ),
        SmokeStep("visible widget dump", "widgets", (r"\[widgets\] total visible: \d+",)),
        SmokeStep("navigate settings", "nav settings", (r"\[test\] nav -> settings",)),
        SmokeStep("navigate signal", "nav signal", (r"\[test\] nav -> signal",)),
        SmokeStep("backlight query", "backlight", (r"\[test\] backlight: (on|off)",)),
        SmokeStep("return home", "nav home", (r"\[test\] nav -> home",)),
    ],
    "telemetry": [
        SmokeStep("screen starts readable", "screen", (r"\[test\] current screen:",)),
        SmokeStep("heap status", "status", (r"\[test\] heap=\d+ psram=\d+",)),
        SmokeStep("query build", "query build", (r"@build\|", r"fw=", r"git=", r"dirty=", r"mcore=", r"env=", r"part=")),
        SmokeStep("query state", "query state", (r"@heap\|", r"@lvgl\|", r"@mesh\|")),
        SmokeStep("query screen", "query screen", (r"@screen\|",)),
        SmokeStep("query radio", "query radio", (r"@radio\|",)),
        SmokeStep("query gps", "query gps", (r"@gps\|",)),
        SmokeStep("query nvs", "query nvs", (r"@nvs\|",)),
        SmokeStep("visible widget dump", "widgets", (r"\[widgets\] total visible: \d+",)),
    ],
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_out_dir() -> Path:
    stamp = datetime.now().strftime("%Y-%m-%d-%H%M%S")
    return repo_root() / ".pio" / "rc2_hardware_validation" / stamp


def normalize_port(port: str) -> str:
    return port.strip().upper().removeprefix("\\\\.\\")


def parse_command_arg(raw: str) -> SmokeStep:
    parts = raw.split("::")
    if len(parts) < 2:
        raise argparse.ArgumentTypeError(
            "--command must use 'command::expected-regex[::expected-regex...]'",
        )
    command = parts[0].strip()
    expects = tuple(part for part in parts[1:] if part)
    if not command or not expects:
        raise argparse.ArgumentTypeError("--command requires a command and expectation")
    return SmokeStep(name=command, command=command, expect=expects)


def read_available(ser: serial.Serial) -> str:
    chunks: list[bytes] = []
    while ser.in_waiting:
        chunks.append(ser.read(ser.in_waiting))
        time.sleep(0.02)
    if not chunks:
        return ""
    return b"".join(chunks).decode("utf-8", errors="replace")


def read_until(
    ser: serial.Serial,
    patterns: tuple[re.Pattern[str], ...],
    timeout_s: float,
) -> tuple[str, bool]:
    deadline = time.monotonic() + timeout_s
    buf = ""
    while time.monotonic() < deadline:
        chunk = read_available(ser)
        if chunk:
            buf += chunk
            if all(pattern.search(buf) for pattern in patterns):
                return buf, True
        time.sleep(0.05)
    return buf, all(pattern.search(buf) for pattern in patterns)


def run_step(ser: serial.Serial, step: SmokeStep, log) -> dict[str, object]:
    ser.write((step.command + "\n").encode("utf-8"))
    ser.flush()
    log.write(f"\n>>> {step.command}\n")
    patterns = tuple(re.compile(pattern, re.MULTILINE) for pattern in step.expect)
    output, ok = read_until(ser, patterns, step.timeout_s)
    if output:
        log.write(output)
    time.sleep(step.settle_s)
    return {
        "name": step.name,
        "command": step.command,
        "expect": list(step.expect),
        "passed": ok,
        "output_chars": len(output),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="explicit serial port, for example COM8")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--profile", choices=sorted(PROFILES), default="telemetry")
    parser.add_argument("--out-dir", default=str(default_out_dir()))
    parser.add_argument(
        "--command",
        action="append",
        type=parse_command_arg,
        help="custom command step as 'command::expected-regex'; overrides --profile",
    )
    parser.add_argument(
        "--forbid-port",
        action="append",
        default=[],
        help="port name that must not be opened; can be repeated",
    )
    parser.add_argument("--startup-timeout", type=float, default=5.0)
    parser.add_argument("--command-timeout", type=float, default=None)
    return parser


def main(argv: list[str] | None = None) -> int:
    global serial
    args = build_parser().parse_args(argv)
    try:
        import serial as serial_module
    except ImportError:  # pragma: no cover - exercised only on missing dependency
        print(
            "ERROR: pyserial is required. Install with: python -m pip install pyserial",
            file=sys.stderr,
        )
        return 2
    serial = serial_module

    normalized = normalize_port(args.port)
    forbidden = {normalize_port(port) for port in args.forbid_port}
    if normalized in forbidden:
        print(f"ERROR: refusing to open forbidden port {args.port}", file=sys.stderr)
        return 2

    steps = list(args.command) if args.command else list(PROFILES[args.profile])
    if args.command_timeout is not None:
        steps = [
            SmokeStep(s.name, s.command, s.expect, args.command_timeout, s.settle_s)
            for s in steps
        ]

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    serial_log = out_dir / "serial.log"
    summary_path = out_dir / "summary.json"

    started = datetime.now().isoformat(timespec="seconds")
    results: list[dict[str, object]] = []
    banner = ""
    with serial.Serial(args.port, args.baud, timeout=0.1) as ser:
        time.sleep(0.5)
        ser.reset_input_buffer()
        ser.write(b"\n")
        ser.flush()
        time.sleep(args.startup_timeout)
        banner = read_available(ser)
        with serial_log.open("w", encoding="utf-8") as log:
            log.write(f"# started={started} port={args.port} baud={args.baud}\n")
            if banner:
                log.write(banner)
            for step in steps:
                results.append(run_step(ser, step, log))

    passed = all(bool(result["passed"]) for result in results)
    summary = {
        "started": started,
        "port": args.port,
        "baud": args.baud,
        "profile": args.profile,
        "passed": passed,
        "banner_seen": "Remote Test Controller" in banner
        or "REMOTE TEST MODE" in banner,
        "step_count": len(results),
        "results": results,
        "serial_log": str(serial_log),
    }
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    print(f"RC2 remote-test smoke {'PASS' if passed else 'FAIL'}")
    print(f"summary: {summary_path}")
    print(f"serial:  {serial_log}")
    for result in results:
        status = "PASS" if result["passed"] else "FAIL"
        print(f"{status:4} {result['name']}: {result['command']}")

    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
