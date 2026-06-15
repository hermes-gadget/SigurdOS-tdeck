#!/usr/bin/env python3
"""Run a small PlatformIO smoke-build matrix for SigurdOS firmware targets."""

from __future__ import annotations

import argparse
import os
import re
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


DEFAULT_PROFILE = "roadmap"

PROFILES = {
    "release": [
        "SigurdOS_TDeck",
    ],
    "debug": [
        "SigurdOS_TDeck_telemetry",
        "SigurdOS_TDeck_remote_test",
        "SigurdOS_TDeck_remote_test_radio",
    ],
    "roadmap": [
        "SigurdOS_TDeck",
        "SigurdOS_TDeck_telemetry",
        "SigurdOS_TDeck_remote_test",
        "SigurdOS_TDeck_remote_test_radio",
        "SigurdOS_TDeck_meshv2",
        "SigurdOS_TDeck_ble_validation",
        "SigurdOS_TDeck_gps_validation",
    ],
}


@dataclass
class BuildResult:
    env: str
    returncode: int
    seconds: float

    @property
    def ok(self) -> bool:
        return self.returncode == 0


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def discover_platformio_envs(project_dir: Path) -> list[str]:
    ini = project_dir / "platformio.ini"
    if not ini.is_file():
        return []

    envs: list[str] = []
    for line in ini.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r"\s*\[env:([^\]]+)\]\s*$", line)
        if match:
            envs.append(match.group(1))
    return envs


def split_env_args(values: list[str] | None) -> list[str]:
    if not values:
        return []

    envs: list[str] = []
    for value in values:
        for env in value.split(","):
            env = env.strip()
            if env:
                envs.append(env)
    return envs


def selected_envs(args: argparse.Namespace) -> list[str]:
    explicit = split_env_args(args.env)
    if explicit:
        return explicit
    return list(PROFILES[args.profile])


def command_string(cmd: list[str]) -> str:
    if os.name == "nt":
        return subprocess.list2cmdline(cmd)
    return shlex.join(cmd)


def run_build(project_dir: Path, pio_cmd: list[str], env: str, extra_args: list[str]) -> BuildResult:
    cmd = [*pio_cmd, "run", "-e", env, *extra_args]
    print(f"\n==> {command_string(cmd)}", flush=True)
    start = time.monotonic()
    try:
        completed = subprocess.run(cmd, cwd=project_dir)
        returncode = completed.returncode
    except FileNotFoundError:
        print(f"ERROR: PlatformIO command not found: {pio_cmd[0]}", file=sys.stderr)
        returncode = 127
    seconds = time.monotonic() - start
    return BuildResult(env=env, returncode=returncode, seconds=seconds)


def print_list(project_dir: Path) -> None:
    print("Smoke build profiles:")
    for name, envs in PROFILES.items():
        marker = " (default)" if name == DEFAULT_PROFILE else ""
        print(f"  {name}{marker}: {', '.join(envs)}")

    discovered = discover_platformio_envs(project_dir)
    if discovered:
        print("\nPlatformIO environments:")
        for env in discovered:
            print(f"  {env}")


def print_summary(results: list[BuildResult]) -> None:
    if not results:
        return

    print("\nSmoke build summary:")
    print(f"{'environment':36} {'status':8} {'time':>9}")
    print(f"{'-' * 36} {'-' * 8} {'-' * 9}")
    for result in results:
        status = "PASS" if result.ok else f"FAIL({result.returncode})"
        print(f"{result.env:36} {status:8} {result.seconds:8.1f}s")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run PlatformIO smoke builds for SigurdOS debug and telemetry targets.",
    )
    parser.add_argument(
        "--profile",
        choices=sorted(PROFILES),
        default=DEFAULT_PROFILE,
        help=f"named build profile to run when --env is not provided (default: {DEFAULT_PROFILE})",
    )
    parser.add_argument(
        "-e",
        "--env",
        action="append",
        help="PlatformIO environment to build; can be repeated or comma-separated",
    )
    parser.add_argument(
        "--project-dir",
        default=str(repo_root()),
        help="project directory containing platformio.ini",
    )
    parser.add_argument(
        "--pio",
        default="pio",
        help="PlatformIO command to run, for example 'pio' or 'python -m platformio'",
    )
    parser.add_argument(
        "--pio-arg",
        action="append",
        default=[],
        help="extra argument passed after each 'pio run -e <env>'; can be repeated",
    )
    parser.add_argument(
        "--fail-fast",
        action="store_true",
        help="stop at the first failed build instead of completing the selected matrix",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print selected commands without running builds",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="list build profiles and discovered PlatformIO environments",
    )
    parser.add_argument(
        "passthrough",
        nargs=argparse.REMAINDER,
        help="extra PlatformIO args after '--', for example: -- -v",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    project_dir = Path(args.project_dir).resolve()

    if args.list:
        print_list(project_dir)
        return 0

    pio_cmd = shlex.split(args.pio)
    envs = selected_envs(args)
    extra_args = list(args.pio_arg)
    passthrough = list(args.passthrough)
    if passthrough and passthrough[0] == "--":
        passthrough = passthrough[1:]
    extra_args.extend(passthrough)

    if args.dry_run:
        for env in envs:
            cmd = [*pio_cmd, "run", "-e", env, *extra_args]
            print(command_string(cmd))
        return 0

    results: list[BuildResult] = []
    for env in envs:
        result = run_build(project_dir, pio_cmd, env, extra_args)
        results.append(result)
        if args.fail_fast and not result.ok:
            break

    print_summary(results)
    return 0 if all(result.ok for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
