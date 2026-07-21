"""Unified SigurdOS T-Deck on-device hardware test runner."""

from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from hw_test.hw_constants import (  # type: ignore[import-not-found]
        DEFAULT_BUILD_ENV,
        PI_TDECK_PORT,
        SERIAL_BAUD,
        TEST_CHANNEL_PREFIX,
        TEST_RADIO_PARAMS,
        UI_SCREEN_TARGETS,
        CommandProtocol,
        boot_wait_for,
        capabilities_for,
        first_existing_local_port,
    )
    from hw_test.hw_flash import (  # type: ignore[import-not-found]
        FlashError,
        HardwareFlasher,
        find_pi_host,
        validate_merged_firmware,
    )
    from hw_test.hw_report import (  # type: ignore[import-not-found]
        HardwareReport,
        HeapSample,
        ScreenshotArtifact,
        TestResult,
        TestStatus,
        utc_now,
        write_report_bundle,
    )
    from hw_test.hw_serial import (  # type: ignore[import-not-found]
        DeviceInfo,
        HardwareSerialError,
        PersistentSerial,
        ScreenshotError,
        contains_crash,
    )
    from hw_test.hw_soak import SoakConfig, SoakRunner  # type: ignore[import-not-found]
else:
    from .hw_constants import (
        DEFAULT_BUILD_ENV,
        PI_TDECK_PORT,
        SERIAL_BAUD,
        TEST_CHANNEL_PREFIX,
        TEST_RADIO_PARAMS,
        UI_SCREEN_TARGETS,
        CommandProtocol,
        boot_wait_for,
        capabilities_for,
        first_existing_local_port,
    )
    from .hw_flash import FlashError, HardwareFlasher, find_pi_host, validate_merged_firmware
    from .hw_report import (
        HardwareReport,
        HeapSample,
        ScreenshotArtifact,
        TestResult,
        TestStatus,
        utc_now,
        write_report_bundle,
    )
    from .hw_serial import (
        DeviceInfo,
        HardwareSerialError,
        PersistentSerial,
        ScreenshotError,
        contains_crash,
    )
    from .hw_soak import SoakConfig, SoakRunner

STATUS_RE = re.compile(r"\[test\]\s+heap=(\d+)\s+psram=(\d+)")
GETRF_RE = re.compile(
    r"freq=([0-9.]+)\s+SF=(\d+)\s+BW=([0-9.]+)\s+CR=(\d+)\s+TX=(-?\d+)",
    re.IGNORECASE,
)


@dataclass(slots=True)
class PhaseSelection:
    smoke: bool = False
    ui: bool = False
    radio: bool = False
    soak: bool = False

    @property
    def label(self) -> str:
        enabled = [name for name in ("smoke", "ui", "radio", "soak") if getattr(self, name)]
        return "+".join(enabled)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_output_dir() -> Path:
    stamp = time.strftime("%Y%m%d-%H%M%S")
    return Path("/tmp") / f"sigurdos-hw-test-{stamp}"


def _run(
    command: list[str],
    *,
    cwd: Path | None = None,
    timeout: float = 1800,
    echo: bool = False,
) -> subprocess.CompletedProcess[str]:
    if echo:
        print("+ " + shlex.join(command), flush=True)
    try:
        return subprocess.run(
            command,
            cwd=cwd,
            capture_output=True,
            text=True,
            errors="replace",
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise RuntimeError(f"command failed: {shlex.join(command)}: {exc}") from exc


def _record(
    report: HardwareReport,
    name: str,
    status: TestStatus,
    detail: str,
    started: float,
    started_at: str,
    *,
    critical: bool = False,
    data: dict[str, Any] | None = None,
) -> None:
    result = TestResult(
        name=name,
        status=status,
        started_at=started_at,
        finished_at=utc_now(),
        duration_s=time.monotonic() - started,
        detail=detail,
        critical=critical,
        data=data or {},
    )
    report.results.append(result)
    print(f"{status.value.upper():4} {name}: {detail}", flush=True)


def _run_check(
    report: HardwareReport,
    name: str,
    check: Callable[[], tuple[bool, str, dict[str, Any]]],
    *,
    critical: bool = False,
) -> bool:
    started, started_at = time.monotonic(), utc_now()
    try:
        passed, detail, data = check()
        _record(
            report,
            name,
            TestStatus.PASS if passed else TestStatus.FAIL,
            detail,
            started,
            started_at,
            critical=critical and not passed,
            data=data,
        )
        return passed
    except Exception as exc:
        _record(
            report,
            name,
            TestStatus.FAIL,
            str(exc),
            started,
            started_at,
            critical=critical,
        )
        return False


def _add_screenshot(
    report: HardwareReport,
    artifact: ScreenshotArtifact,
    output_dir: Path,
) -> None:
    path = Path(artifact.path)
    try:
        artifact.path = str(path.relative_to(output_dir))
    except ValueError:
        artifact.path = str(path)
    report.screenshots.append(artifact)


def _sample_status(report: HardwareReport, response: str, elapsed_s: float) -> None:
    match = STATUS_RE.search(response)
    if not match:
        return
    report.heap_samples.append(
        HeapSample(
            timestamp=utc_now(),
            elapsed_s=elapsed_s,
            heap_free=int(match.group(1)),
            heap_min_free=int(match.group(1)),
            psram_free=int(match.group(2)),
            source="status",
        )
    )


def _check_variant(
    report: HardwareReport,
    info: DeviceInfo,
    expected_environment: str,
    selection: PhaseSelection,
) -> bool:
    started, started_at = time.monotonic(), utc_now()
    report.metadata.update(
        {
            "firmware_protocol": info.protocol.value,
            "firmware_environment": info.build_environment or "unknown",
            "firmware_radio": info.radio_available,
            "serial_recovered": info.recovered,
        }
    )
    if info.protocol == CommandProtocol.UNKNOWN:
        _record(
            report,
            "firmware.detect",
            TestStatus.FAIL,
            "neither remote-test nor release command protocol responded",
            started,
            started_at,
            critical=True,
            data={"evidence": info.evidence[-1200:]},
        )
        return False

    _record(
        report,
        "firmware.detect",
        TestStatus.PASS,
        f"{info.protocol.value} protocol; env={info.build_environment or 'not reported'}",
        started,
        started_at,
        data={"recovered": info.recovered},
    )
    expected = capabilities_for(expected_environment)
    mismatch: list[str] = []
    if expected:
        if expected.command_protocol != info.protocol:
            mismatch.append(
                f"protocol expected {expected.command_protocol.value}, found {info.protocol.value}"
            )
        if selection.radio and not expected.radio:
            mismatch.append(f"{expected_environment} has no radio capability")
        if info.radio_available is not None and info.radio_available != expected.radio:
            actual = "radio-enabled" if info.radio_available else "no-radio"
            wanted = "radio-enabled" if expected.radio else "no-radio"
            mismatch.append(f"expected {wanted} firmware, found {actual} firmware")
    if info.build_environment and info.build_environment != expected_environment:
        mismatch.append(f"expected env {expected_environment}, found {info.build_environment}")
    if mismatch:
        now, now_at = time.monotonic(), utc_now()
        _record(
            report,
            "firmware.expected_variant",
            TestStatus.WARN,
            "; ".join(mismatch),
            now,
            now_at,
        )
        report.notes.append("Firmware variant mismatch: " + "; ".join(mismatch))
    return True


def _nav_check(connection: PersistentSerial, screen: str) -> tuple[bool, str, dict[str, Any]]:
    expected = (
        (f"[test] nav -> {screen}",)
        if connection.protocol == CommandProtocol.REMOTE_TEST
        else (f"[serial] NAV {screen} -> OK",)
    )
    response = connection.send_command(
        f"nav {screen}",
        timeout_s=5,
        expected=expected,
    )
    marker = contains_crash(response.output)
    if marker:
        return False, f"crash marker {marker}", {"output": response.output[-1200:]}
    passed = any(item in response.output for item in expected)
    return passed, f"nav {screen} {'confirmed' if passed else 'not confirmed'}", {
        "wire_command": response.wire_command,
        "attempts": response.attempts,
        "recovered": response.recovered,
        "output": response.output[-1200:],
    }


def run_smoke(
    connection: PersistentSerial,
    report: HardwareReport,
    output_dir: Path,
) -> None:
    phase_started = time.monotonic()
    if connection.protocol == CommandProtocol.REMOTE_TEST:
        _run_check(
            report,
            "smoke.help",
            lambda: _command_expect(
                connection,
                "help",
                ("SigurdOS Remote Test Controller",),
                timeout_s=5,
            ),
        )
        _run_check(
            report,
            "smoke.screen",
            lambda: _command_expect(
                connection,
                "screen",
                ("[test] current screen:",),
                timeout_s=4,
            ),
        )
        status_response: list[str] = []

        def status_check() -> tuple[bool, str, dict[str, Any]]:
            response = connection.send_command(
                "status",
                timeout_s=5,
                expected=("[test] heap=",),
            )
            status_response.append(response.output)
            passed = STATUS_RE.search(response.output) is not None
            return passed, "heap and PSRAM reported" if passed else "status telemetry missing", {
                "output": response.output[-1200:]
            }

        _run_check(report, "smoke.status", status_check)
        if status_response:
            _sample_status(report, status_response[-1], time.monotonic() - phase_started)

    _run_check(report, "smoke.nav.home", lambda: _nav_check(connection, "home"))
    _capture_check(connection, report, output_dir, "home", "smoke.screenshot.home")
    _run_check(report, "smoke.nav.settings", lambda: _nav_check(connection, "settings"))
    _capture_check(connection, report, output_dir, "settings", "smoke.screenshot.settings")
    _run_check(report, "smoke.return_home", lambda: _nav_check(connection, "home"))


def _command_expect(
    connection: PersistentSerial,
    command: str,
    expected: tuple[str, ...],
    *,
    timeout_s: float,
) -> tuple[bool, str, dict[str, Any]]:
    response = connection.send_command(command, timeout_s=timeout_s, expected=expected)
    marker = contains_crash(response.output)
    passed = marker is None and all(item in response.output for item in expected)
    detail = f"received {', '.join(expected)}" if passed else f"missing {', '.join(expected)}"
    if marker:
        detail = f"crash marker {marker}"
    return passed, detail, {
        "attempts": response.attempts,
        "recovered": response.recovered,
        "output": response.output[-1600:],
    }


def _capture_check(
    connection: PersistentSerial,
    report: HardwareReport,
    output_dir: Path,
    screen: str,
    name: str,
) -> None:
    started, started_at = time.monotonic(), utc_now()
    path = output_dir / "screenshots" / f"{screen}.png"
    try:
        artifact = connection.capture_screenshot(path, screen=screen)
        _add_screenshot(report, artifact, output_dir)
        _record(
            report,
            name,
            TestStatus.PASS,
            f"{artifact.width}x{artifact.height}, {artifact.raw_bytes} framebuffer bytes",
            started,
            started_at,
            data=artifact.to_dict(),
        )
    except ScreenshotError as exc:
        _record(report, name, TestStatus.FAIL, str(exc), started, started_at)


def run_ui(
    connection: PersistentSerial,
    report: HardwareReport,
    output_dir: Path,
    *,
    iterations: int,
) -> None:
    phase_started = time.monotonic()
    for iteration in range(1, iterations + 1):
        for screen in UI_SCREEN_TARGETS:
            _run_check(
                report,
                f"ui.{iteration}.{screen}",
                lambda target=screen: _nav_check(connection, target),
            )
        if connection.protocol == CommandProtocol.REMOTE_TEST:
            response = connection.send_command(
                "status",
                timeout_s=5,
                expected=("[test] heap=",),
            )
            _sample_status(report, response.output, time.monotonic() - phase_started)
        if iteration == 1:
            for screen in ("home", "settings"):
                if not (output_dir / "screenshots" / f"{screen}.png").exists():
                    _run_check(
                        report,
                        f"ui.capture_nav.{screen}",
                        lambda target=screen: _nav_check(connection, target),
                    )
                    _capture_check(
                        connection,
                        report,
                        output_dir,
                        screen,
                        f"ui.screenshot.{screen}",
                    )


def run_radio(
    connection: PersistentSerial,
    report: HardwareReport,
    *,
    frequency_mhz: float,
    spreading_factor: int,
    bandwidth_khz: float,
    coding_rate: int,
    tx_power_dbm: int,
) -> None:
    if connection.protocol != CommandProtocol.REMOTE_TEST:
        now, now_at = time.monotonic(), utc_now()
        _record(
            report,
            "radio.protocol",
            TestStatus.FAIL,
            "radio automation requires a remote-test-radio build",
            now,
            now_at,
            critical=True,
        )
        return
    if connection.device_info and connection.device_info.radio_available is False:
        now, now_at = time.monotonic(), utc_now()
        _record(
            report,
            "radio.capability",
            TestStatus.FAIL,
            "detected remote-test firmware was compiled without LoRa radio support",
            now,
            now_at,
            critical=True,
        )
        return

    _run_check(
        report,
        "radio.query_initial",
        lambda: _command_expect(connection, "getrf", ("[test] getrf:",), timeout_s=6),
    )
    params = f"{frequency_mhz:.3f} {spreading_factor} {bandwidth_khz:g} {coding_rate} {tx_power_dbm}"
    configured = _run_check(
        report,
        "radio.configure",
        lambda: _command_expect(
            connection,
            f"setrf {params}",
            ("radio params saved to NVS",),
            timeout_s=7,
        ),
        critical=True,
    )
    if not configured:
        return

    reboot_started, reboot_at = time.monotonic(), utc_now()
    response = connection.send_command("reboot", timeout_s=2, recover_on_silence=False)
    connection.close()
    time.sleep(2)
    try:
        connection.connect()
        info = connection.detect_firmware()
        passed = info.protocol == CommandProtocol.REMOTE_TEST
        _record(
            report,
            "radio.reboot",
            TestStatus.PASS if passed else TestStatus.FAIL,
            "remote-test controller returned" if passed else "controller did not return",
            reboot_started,
            reboot_at,
            critical=not passed,
            data={"response": response.output[-800:]},
        )
    except HardwareSerialError as exc:
        _record(
            report,
            "radio.reboot",
            TestStatus.FAIL,
            str(exc),
            reboot_started,
            reboot_at,
            critical=True,
        )
        return

    observed: list[str] = []

    def verify_params() -> tuple[bool, str, dict[str, Any]]:
        response = connection.send_command("getrf", timeout_s=6, expected=("[test] getrf:",))
        observed.append(response.output)
        match = GETRF_RE.search(response.output)
        if not match:
            return False, "radio parameters could not be parsed", {"output": response.output[-1600:]}
        values = (
            float(match.group(1)),
            int(match.group(2)),
            float(match.group(3)),
            int(match.group(4)),
            int(match.group(5)),
        )
        expected = (frequency_mhz, spreading_factor, bandwidth_khz, coding_rate, tx_power_dbm)
        passed = (
            abs(values[0] - expected[0]) < 0.002
            and values[1] == expected[1]
            and abs(values[2] - expected[2]) < 0.2
            and values[3:] == expected[3:]
        )
        detail = (
            f"observed freq={values[0]:.3f} SF={values[1]} BW={values[2]:.1f} "
            f"CR={values[3]} TX={values[4]}"
        )
        return passed, detail, {"observed": values, "expected": expected}

    if not _run_check(report, "radio.verify_config", verify_params, critical=True):
        return

    channel = f"{TEST_CHANNEL_PREFIX}-{int(time.time()) % 100000:05d}"
    added_output: list[str] = []

    def add_channel() -> tuple[bool, str, dict[str, Any]]:
        response = connection.send_command(
            f"addchannel {channel}",
            timeout_s=7,
            expected=("addchannel",),
        )
        added_output.append(response.output)
        passed = "addchannel OK" in response.output
        return passed, f"created #{channel}" if passed else "channel creation failed", {
            "output": response.output[-1600:]
        }

    if not _run_check(report, "radio.channel", add_channel):
        return
    message = f"hw-test-{int(time.time())}"
    _run_check(
        report,
        "radio.transmit",
        lambda: _command_expect(
            connection,
            f"sendchannel {channel} {message}",
            ("sendchannel OK",),
            timeout_s=12,
        ),
        critical=True,
    )
    _run_check(
        report,
        "radio.cleanup",
        lambda: _command_expect(
            connection,
            f"removechannel {channel}",
            ("OK",),
            timeout_s=7,
        ),
    )


def _prepare_pr_worktree(pr_number: int) -> tuple[Path, str]:
    root = repo_root()
    temporary = Path(tempfile.mkdtemp(prefix=f"sigurdos-pr-{pr_number}-"))
    ref = f"refs/hw-test/pr-{pr_number}"
    fetched = _run(
        ["git", "fetch", "origin", f"+pull/{pr_number}/head:{ref}"],
        cwd=root,
        timeout=300,
        echo=True,
    )
    if fetched.returncode != 0:
        raise RuntimeError(f"cannot fetch PR #{pr_number}: {(fetched.stdout + fetched.stderr)[-2000:]}")
    added = _run(
        ["git", "worktree", "add", "--detach", str(temporary), ref],
        cwd=root,
        timeout=120,
        echo=True,
    )
    if added.returncode != 0:
        raise RuntimeError(f"cannot create PR worktree: {(added.stdout + added.stderr)[-2000:]}")
    submodules = _run(
        ["git", "submodule", "update", "--init", "--recursive"],
        cwd=temporary,
        timeout=900,
        echo=True,
    )
    if submodules.returncode != 0:
        _remove_pr_worktree(temporary)
        raise RuntimeError(f"cannot initialize PR submodules: {(submodules.stdout + submodules.stderr)[-2000:]}")
    sha = _run(["git", "rev-parse", "HEAD"], cwd=temporary, timeout=20).stdout.strip()
    return temporary, sha


def _remove_pr_worktree(path: Path) -> None:
    _run(
        ["git", "worktree", "remove", "--force", str(path)],
        cwd=repo_root(),
        timeout=120,
    )


def _build_or_flash(args: argparse.Namespace, metadata: dict[str, Any]) -> None:
    if not (args.pr or args.build or args.flash):
        return
    worktree: Path | None = None
    build_root = repo_root()
    try:
        if args.pr:
            worktree, sha = _prepare_pr_worktree(args.pr)
            build_root = worktree
            metadata.update({"pr": args.pr, "pr_sha": sha})
        pi_mode = bool(args.pi_mode)
        port = args.port or (PI_TDECK_PORT if pi_mode else first_existing_local_port())
        flasher = HardwareFlasher(
            build_root,
            pi_mode=pi_mode,
            port=port,
            pi_host=args.pi_host,
            esptool=args.esptool,
        )
        if args.firmware:
            firmware = validate_merged_firmware(args.firmware)
        else:
            build = flasher.build(args.env)
            firmware = build.firmware
            metadata.update(
                {
                    "build_environment": build.environment,
                    "build_duration_s": round(build.duration_s, 3),
                }
            )
            print(f"BUILD PASS: {build.environment} -> {firmware}", flush=True)
        if args.flash or args.pr:
            flashed = flasher.flash(
                firmware,
                environment=args.env,
                verify=not args.no_boot_verify,
                cold_boot=args.cold_boot,
            )
            metadata.update(
                {
                    "flash_target": flashed.target,
                    "flash_port": flashed.port,
                    "flash_duration_s": round(flashed.duration_s, 3),
                    "flash_boot_verified": flashed.boot_verified,
                }
            )
            print(f"FLASH PASS: {flashed.target}:{flashed.port}", flush=True)
    finally:
        if worktree is not None:
            _remove_pr_worktree(worktree)


def _selection(args: argparse.Namespace) -> PhaseSelection:
    if args.all:
        return PhaseSelection(True, True, True, True)
    if args.full:
        return PhaseSelection(True, True, True, False)
    if args.ui:
        return PhaseSelection(ui=True)
    if args.radio:
        return PhaseSelection(radio=True)
    if args.soak:
        return PhaseSelection(soak=True)
    return PhaseSelection(smoke=True)


def _run_local(args: argparse.Namespace, metadata: dict[str, Any]) -> int:
    selection = _selection(args)
    output_dir: Path = args.outdir
    output_dir.mkdir(parents=True, exist_ok=True)
    report = HardwareReport(mode=selection.label, transport="local", metadata=dict(metadata))
    report.metadata.update({"port": args.port, "baud": args.baud, "expected_environment": args.env})
    connection = PersistentSerial(
        args.port,
        baud=args.baud,
        boot_wait_s=boot_wait_for(args.env),
        esptool=args.esptool,
    )
    try:
        started, started_at = time.monotonic(), utc_now()
        try:
            connection.connect()
            info = connection.detect_firmware()
            _record(
                report,
                "serial.connect",
                TestStatus.PASS,
                f"persistent connection opened on {args.port}",
                started,
                started_at,
            )
        except HardwareSerialError as exc:
            _record(
                report,
                "serial.connect",
                TestStatus.FAIL,
                str(exc),
                started,
                started_at,
                critical=True,
            )
            return _finish(report, output_dir)
        if not _check_variant(report, info, args.env, selection):
            return _finish(report, output_dir)

        if selection.smoke:
            run_smoke(connection, report, output_dir)
        if selection.ui:
            run_ui(connection, report, output_dir, iterations=args.iterations)
        if selection.radio:
            run_radio(
                connection,
                report,
                frequency_mhz=args.radio_frequency,
                spreading_factor=args.radio_sf,
                bandwidth_khz=args.radio_bw,
                coding_rate=args.radio_cr,
                tx_power_dbm=args.radio_power,
            )
        if selection.soak:
            soak = SoakRunner(
                connection,
                output_dir / "soak",
                SoakConfig(
                    duration_s=args.duration,
                    leak_threshold_bytes=args.leak_threshold,
                    screenshot_interval_s=args.screenshot_interval,
                    progress_interval_s=args.progress_interval,
                ),
                protocol=connection.protocol,
            ).run()
            report.heap_samples.extend(soak.samples)
            for artifact in soak.screenshots:
                _add_screenshot(report, artifact, output_dir)
            now, now_at = time.monotonic(), soak.started_at
            status = TestStatus.PASS if int(soak.exit_code) == 0 else TestStatus.FAIL
            _record(
                report,
                "soak.stability",
                status,
                f"{soak.status}; {len(soak.samples)} samples; heap delta={soak.heap_delta_bytes}",
                now - soak.duration_s,
                now_at,
                critical=soak.status == "CRASH",
                data=soak.to_dict(),
            )
            report.notes.extend(soak.notes)
    except KeyboardInterrupt:
        now, now_at = time.monotonic(), utc_now()
        _record(
            report,
            "runner.interrupted",
            TestStatus.FAIL,
            "interrupted by operator",
            now,
            now_at,
        )
    except Exception as exc:
        now, now_at = time.monotonic(), utc_now()
        _record(
            report,
            "runner.exception",
            TestStatus.FAIL,
            str(exc),
            now,
            now_at,
            critical=True,
        )
    finally:
        connection.close()
    return _finish(report, output_dir)


def _finish(report: HardwareReport, output_dir: Path) -> int:
    paths = write_report_bundle(report, output_dir)
    print(f"\nHARDWARE TEST {report.outcome} (exit {report.exit_code})", flush=True)
    print(f"JSON: {paths['json']}", flush=True)
    print(f"Markdown: {paths['markdown']}", flush=True)
    print(f"GitHub comment: {paths['github']}", flush=True)
    return report.exit_code


def _worker_arguments(args: argparse.Namespace, remote_output: str) -> list[str]:
    selection = _selection(args)
    mode = "--smoke"
    if selection == PhaseSelection(True, True, True, True):
        mode = "--all"
    elif selection == PhaseSelection(True, True, True, False):
        mode = "--full"
    elif selection.ui:
        mode = "--ui"
    elif selection.radio:
        mode = "--radio"
    elif selection.soak:
        mode = "--soak"
    return [
        "python3",
        "-m",
        "hw_test.hw_test_runner",
        mode,
        "--local",
        "--remote-worker",
        "--port",
        args.port or PI_TDECK_PORT,
        "--baud",
        str(args.baud),
        "--env",
        args.env,
        "--outdir",
        remote_output,
        "--iterations",
        str(args.iterations),
        "--duration",
        str(args.duration),
        "--leak-threshold",
        str(args.leak_threshold),
        "--screenshot-interval",
        str(args.screenshot_interval),
        "--progress-interval",
        str(args.progress_interval),
        "--radio-frequency",
        str(args.radio_frequency),
        "--radio-sf",
        str(args.radio_sf),
        "--radio-bw",
        str(args.radio_bw),
        "--radio-cr",
        str(args.radio_cr),
        "--radio-power",
        str(args.radio_power),
    ]


def _merge_report_metadata(
    output_dir: Path,
    metadata: dict[str, Any],
    *,
    transport: str | None = None,
) -> None:
    path = output_dir / "results.json"
    if not path.is_file() or not metadata:
        return
    payload = json.loads(path.read_text(encoding="utf-8"))
    payload.setdefault("metadata", {}).update(metadata)
    if transport is not None:
        payload["transport"] = transport
    report = HardwareReport.from_dict(payload)
    report.metadata.update(metadata)
    write_report_bundle(report, output_dir)


def _run_pi_worker(args: argparse.Namespace, metadata: dict[str, Any]) -> int:
    host = find_pi_host(args.pi_host)
    remote_root = f"/tmp/sigurdos-hw-test-runner-{int(time.time())}"
    remote_output = f"{remote_root}/results"
    created = _run(["ssh", host, "mkdir", "-p", remote_root], timeout=20)
    if created.returncode != 0:
        raise RuntimeError(f"cannot create Pi worker directory: {created.stderr.strip()}")
    package_dir = Path(__file__).resolve().parent
    copied = _run(["scp", "-r", str(package_dir), f"{host}:{remote_root}/"], timeout=120, echo=True)
    if copied.returncode != 0:
        raise RuntimeError(f"cannot deploy Pi worker: {copied.stderr.strip()}")
    worker = _worker_arguments(args, remote_output)
    command = f"cd {shlex.quote(remote_root)} && {shlex.join(worker)}"
    print(f"Running hardware tests on {host}:{args.port or PI_TDECK_PORT}", flush=True)
    result = subprocess.run(["ssh", host, command], check=False)
    args.outdir.mkdir(parents=True, exist_ok=True)
    fetched = _run(
        ["scp", "-r", f"{host}:{remote_output}/.", str(args.outdir)],
        timeout=180,
        echo=True,
    )
    if fetched.returncode != 0:
        raise RuntimeError(f"cannot retrieve Pi results: {fetched.stderr.strip()}")
    metadata.update({"pi_host": host, "pi_port": args.port or PI_TDECK_PORT})
    _merge_report_metadata(args.outdir, metadata, transport="pi")
    if result.returncode in (0, 1, 2):
        return result.returncode
    return 2


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--smoke", action="store_true", help="quick commands, navigation, screenshots")
    mode.add_argument("--full", action="store_true", help="smoke + UI + radio (no long soak)")
    mode.add_argument("--ui", action="store_true", help="navigation stability sweep")
    mode.add_argument("--radio", action="store_true", help="radio configuration and low-power TX")
    mode.add_argument("--soak", action="store_true", help="persistent runtime soak")
    mode.add_argument("--all", action="store_true", help="smoke + UI + radio + soak")

    transport = parser.add_mutually_exclusive_group()
    transport.add_argument("--pi-mode", action="store_true", help="deploy and run through SSH on the Pi")
    transport.add_argument("--local", action="store_true", help="use a directly attached T-Deck")
    parser.add_argument("--pi-host", help="override hermes-pi.local/hermes-pi discovery")
    parser.add_argument("--port", help="device serial port")
    parser.add_argument("--baud", type=int, default=SERIAL_BAUD)
    parser.add_argument("--env", default=DEFAULT_BUILD_ENV, help="expected/build environment")
    parser.add_argument("--outdir", type=Path, default=default_output_dir())

    parser.add_argument("--iterations", type=int, default=1, help="UI sweep iterations")
    parser.add_argument("--duration", type=float, default=1800.0, help="soak duration in seconds")
    parser.add_argument("--leak-threshold", type=int, default=1000)
    parser.add_argument("--screenshot-interval", type=float, default=300.0)
    parser.add_argument("--progress-interval", type=float, default=60.0)

    parser.add_argument("--radio-frequency", type=float, default=TEST_RADIO_PARAMS.frequency_mhz)
    parser.add_argument("--radio-sf", type=int, default=TEST_RADIO_PARAMS.spreading_factor)
    parser.add_argument("--radio-bw", type=float, default=TEST_RADIO_PARAMS.bandwidth_khz)
    parser.add_argument("--radio-cr", type=int, default=TEST_RADIO_PARAMS.coding_rate)
    parser.add_argument("--radio-power", type=int, default=TEST_RADIO_PARAMS.tx_power_dbm)

    parser.add_argument("--pr", type=int, help="build and flash an isolated GitHub PR worktree")
    parser.add_argument("--build", action="store_true", help="build --env before testing")
    parser.add_argument("--flash", action="store_true", help="flash before testing (builds unless --firmware)")
    parser.add_argument("--firmware", type=Path, help="existing firmware-merged.bin")
    parser.add_argument("--cold-boot", action="store_true")
    parser.add_argument("--no-boot-verify", action="store_true")
    parser.add_argument("--esptool")
    parser.add_argument("--remote-worker", action="store_true", help=argparse.SUPPRESS)
    return parser


def validate_args(parser: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    if args.iterations < 1:
        parser.error("--iterations must be at least 1")
    if args.duration <= 0:
        parser.error("--duration must be positive")
    if args.pr is not None and args.pr <= 0:
        parser.error("--pr must be a positive issue number")
    if args.firmware and args.build:
        parser.error("--firmware and --build are mutually exclusive")
    if args.pr is not None and args.firmware:
        parser.error("--pr always builds the PR branch and cannot use --firmware")
    if args.firmware and not args.flash:
        parser.error("--firmware requires --flash")
    if args.cold_boot and not (args.flash or args.pr):
        parser.error("--cold-boot requires --flash or --pr")
    if not args.pi_mode and not args.local:
        # Explicit transport is safest around machines that also have a Heltec
        # on /dev/ttyUSB0. Direct Pi workers force --local internally.
        parser.error("choose --pi-mode or --local")
    if args.local and not args.port:
        args.port = first_existing_local_port()
    if args.pi_mode and not args.port:
        args.port = PI_TDECK_PORT


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    validate_args(parser, args)
    metadata: dict[str, Any] = {}
    try:
        if not args.remote_worker:
            _build_or_flash(args, metadata)
        if args.pi_mode and not args.remote_worker:
            return _run_pi_worker(args, metadata)
        return _run_local(args, metadata)
    except (FlashError, HardwareSerialError, RuntimeError, OSError, ValueError) as exc:
        print(f"CRITICAL: {exc}", file=sys.stderr)
        args.outdir.mkdir(parents=True, exist_ok=True)
        report = HardwareReport(
            mode=_selection(args).label,
            transport="pi" if args.pi_mode else "local",
            metadata=metadata,
        )
        now, now_at = time.monotonic(), utc_now()
        _record(
            report,
            "runner.preflight",
            TestStatus.FAIL,
            str(exc),
            now,
            now_at,
            critical=True,
        )
        return _finish(report, args.outdir)


if __name__ == "__main__":
    raise SystemExit(main())
