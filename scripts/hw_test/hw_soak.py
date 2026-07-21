"""Long-duration persistent-serial soak testing for SigurdOS T-Deck."""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from hw_test.hw_constants import (  # type: ignore[import-not-found]
        BOOT_WAIT_REMOTE_TEST_S,
        ROM_BOOT_MARKERS,
        SCREENSHOT_TIMEOUT_S,
        SERIAL_BAUD,
        CommandProtocol,
        first_existing_local_port,
    )
    from hw_test.hw_report import HeapSample, ScreenshotArtifact, utc_now  # type: ignore[import-not-found]
    from hw_test.hw_serial import (  # type: ignore[import-not-found]
        HardwareSerialError,
        PersistentSerial,
        contains_crash,
        parse_stat_line,
    )
else:
    from .hw_constants import (
        BOOT_WAIT_REMOTE_TEST_S,
        ROM_BOOT_MARKERS,
        SCREENSHOT_TIMEOUT_S,
        SERIAL_BAUD,
        CommandProtocol,
        first_existing_local_port,
    )
    from .hw_report import HeapSample, ScreenshotArtifact, utc_now
    from .hw_serial import HardwareSerialError, PersistentSerial, contains_crash, parse_stat_line


class SoakExit(IntEnum):
    PASS = 0
    LEAK = 1
    CRASH = 2


@dataclass(slots=True)
class SoakConfig:
    duration_s: float = 1800.0
    leak_threshold_bytes: int = 1000
    progress_interval_s: float = 60.0
    screenshot_interval_s: float = 300.0
    warmup_s: float = 60.0
    no_sample_timeout_s: float = 75.0
    capture_timeout_s: float = SCREENSHOT_TIMEOUT_S


@dataclass(slots=True)
class SoakResult:
    status: str
    exit_code: SoakExit
    started_at: str
    finished_at: str
    duration_s: float
    target_s: float
    samples: list[HeapSample] = field(default_factory=list)
    screenshots: list[ScreenshotArtifact] = field(default_factory=list)
    crashes: list[dict[str, object]] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)
    heap_delta_bytes: int | None = None

    def to_dict(self) -> dict[str, object]:
        return {
            "schema_version": 1,
            "status": self.status,
            "exit_code": int(self.exit_code),
            "started_at": self.started_at,
            "finished_at": self.finished_at,
            "duration_s": round(self.duration_s, 3),
            "target_s": self.target_s,
            "sample_count": len(self.samples),
            "heap_delta_bytes": self.heap_delta_bytes,
            "heap_min": min((item.heap_free for item in self.samples), default=None),
            "heap_max": max((item.heap_free for item in self.samples), default=None),
            "psram_min": min((item.psram_free for item in self.samples), default=None),
            "psram_max": max((item.psram_free for item in self.samples), default=None),
            "crashes": self.crashes,
            "screenshots": [item.to_dict() for item in self.screenshots],
            "notes": self.notes,
        }


def _stable_heap_delta(samples: list[HeapSample], warmup_s: float) -> int | None:
    candidates = [sample for sample in samples if sample.elapsed_s >= warmup_s]
    if len(candidates) < 2:
        candidates = samples
    if len(candidates) < 2:
        return None
    window = min(3, max(1, len(candidates) // 3))
    start = round(statistics.median(item.heap_free for item in candidates[:window]))
    end = round(statistics.median(item.heap_free for item in candidates[-window:]))
    return end - start


class SoakRunner:
    """Read runtime telemetry without reopening or toggling the serial port."""

    def __init__(
        self,
        connection: PersistentSerial,
        output_dir: Path,
        config: SoakConfig,
        *,
        protocol: CommandProtocol,
    ) -> None:
        self.connection = connection
        self.output_dir = output_dir
        self.config = config
        self.protocol = protocol

    def run(self) -> SoakResult:
        self.output_dir.mkdir(parents=True, exist_ok=True)
        screenshots_dir = self.output_dir / "screenshots"
        samples_path = self.output_dir / "samples.jsonl"
        serial_path = self.output_dir / "serial.log"
        started_at = utc_now()
        started = time.monotonic()
        deadline = started + self.config.duration_s
        next_progress = started + self.config.progress_interval_s
        next_screenshot = (
            started + self.config.screenshot_interval_s
            if self.config.screenshot_interval_s > 0
            else float("inf")
        )
        last_sample = started
        samples: list[HeapSample] = []
        screenshots: list[ScreenshotArtifact] = []
        crashes: list[dict[str, object]] = []
        notes: list[str] = []
        line_buffer = bytearray()

        # Boot output can legitimately contain ROM reset banners. Start crash
        # detection only after consuming what was already pending.
        self.connection.drain(0.5)
        print(
            f"[{utc_now()}] soak start: target={self.config.duration_s:.0f}s "
            f"protocol={self.protocol.value}",
            flush=True,
        )
        with samples_path.open("w", encoding="utf-8", buffering=1) as samples_file, serial_path.open(
            "w", encoding="utf-8", buffering=1
        ) as serial_file:
            while time.monotonic() < deadline:
                now = time.monotonic()
                try:
                    byte = self.connection.read(1)
                except Exception as exc:
                    crashes.append(
                        {
                            "timestamp": utc_now(),
                            "elapsed_s": round(now - started, 3),
                            "marker": "serial exception",
                            "detail": str(exc),
                        }
                    )
                    break

                if byte:
                    if byte == b"\n":
                        line = line_buffer.decode("utf-8", errors="replace").rstrip("\r")
                        line_buffer.clear()
                        serial_file.write(line + "\n")
                        marker = contains_crash(line)
                        if not marker:
                            marker = next((item for item in ROM_BOOT_MARKERS if item in line), None)
                        if marker:
                            crashes.append(
                                {
                                    "timestamp": utc_now(),
                                    "elapsed_s": round(time.monotonic() - started, 3),
                                    "marker": marker,
                                    "detail": line[-1000:],
                                }
                            )
                            print(f"[{utc_now()}] CRASH: {marker}", flush=True)
                            break
                        sample = parse_stat_line(line, elapsed_s=time.monotonic() - started)
                        if sample:
                            samples.append(sample)
                            last_sample = time.monotonic()
                            samples_file.write(json.dumps(sample.to_dict(), sort_keys=True) + "\n")
                    elif len(line_buffer) < 16_384:
                        line_buffer.extend(byte)
                    else:
                        line_buffer.clear()

                now = time.monotonic()
                if now >= next_progress:
                    latest = f" heap={samples[-1].heap_free:,}" if samples else " heap=no-samples"
                    print(
                        f"[{utc_now()}] progress {now-started:.0f}/{self.config.duration_s:.0f}s "
                        f"samples={len(samples)}{latest}",
                        flush=True,
                    )
                    next_progress += self.config.progress_interval_s

                if now >= next_screenshot:
                    shot_path = screenshots_dir / f"soak-{int(now-started):06d}s.png"
                    try:
                        screenshot = self.connection.capture_screenshot(
                            shot_path,
                            screen="soak",
                            timeout_s=self.config.capture_timeout_s,
                        )
                        screenshots.append(screenshot)
                        print(f"[{utc_now()}] screenshot {shot_path}", flush=True)
                    except HardwareSerialError as exc:
                        notes.append(f"screenshot at {now-started:.0f}s failed: {exc}")
                    # Capture consumes the serial stream, including any [stat]
                    # records emitted during a long framebuffer transfer. Reset
                    # the telemetry watchdog and schedule from completion.
                    now = time.monotonic()
                    last_sample = now
                    next_screenshot = now + self.config.screenshot_interval_s

                if (
                    self.protocol == CommandProtocol.REMOTE_TEST
                    and now - last_sample > self.config.no_sample_timeout_s
                ):
                    crashes.append(
                        {
                            "timestamp": utc_now(),
                            "elapsed_s": round(now - started, 3),
                            "marker": "no stat telemetry",
                            "detail": (
                                f"no [stat] sample for {now-last_sample:.0f}s; "
                                "firmware may be silent or unresponsive"
                            ),
                        }
                    )
                    break

        elapsed = time.monotonic() - started
        heap_delta = _stable_heap_delta(samples, self.config.warmup_s)
        if crashes:
            status, exit_code = "CRASH", SoakExit.CRASH
        elif elapsed < self.config.duration_s * 0.95:
            status, exit_code = "CRASH", SoakExit.CRASH
            notes.append(f"soak ended early after {elapsed:.1f}s")
        elif heap_delta is not None and heap_delta <= -self.config.leak_threshold_bytes:
            status, exit_code = "LEAK", SoakExit.LEAK
            notes.append(
                f"free heap fell {-heap_delta} bytes (threshold {self.config.leak_threshold_bytes})"
            )
        else:
            status, exit_code = "PASS", SoakExit.PASS
            if not samples and self.protocol == CommandProtocol.RELEASE:
                notes.append("release firmware emits no periodic [stat] output; crash-only soak passed")
            elif not samples:
                status, exit_code = "CRASH", SoakExit.CRASH
                notes.append("no heap samples were collected")

        result = SoakResult(
            status=status,
            exit_code=exit_code,
            started_at=started_at,
            finished_at=utc_now(),
            duration_s=elapsed,
            target_s=self.config.duration_s,
            samples=samples,
            screenshots=screenshots,
            crashes=crashes,
            notes=notes,
            heap_delta_bytes=heap_delta,
        )
        (self.output_dir / "results.json").write_text(
            json.dumps(result.to_dict(), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print(
            f"[{utc_now()}] SOAK {status}: duration={elapsed:.1f}s samples={len(samples)} "
            f"heap_delta={heap_delta}",
            flush=True,
        )
        return result


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=first_existing_local_port())
    parser.add_argument("--baud", type=int, default=SERIAL_BAUD)
    parser.add_argument("--duration", type=float, default=7200.0, help="seconds")
    parser.add_argument("--outdir", type=Path, default=Path("/tmp/sigurdos-soak"))
    parser.add_argument("--boot-wait", type=float, default=BOOT_WAIT_REMOTE_TEST_S)
    parser.add_argument("--leak-threshold", type=int, default=1000)
    parser.add_argument("--screenshot-interval", type=float, default=300.0)
    parser.add_argument("--progress-interval", type=float, default=60.0)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    connection = PersistentSerial(
        args.port,
        baud=args.baud,
        boot_wait_s=args.boot_wait,
    )
    try:
        connection.connect()
        info = connection.detect_firmware()
        if info.protocol == CommandProtocol.UNKNOWN:
            print("ERROR: firmware command protocol could not be detected", file=sys.stderr)
            return int(SoakExit.CRASH)
        config = SoakConfig(
            duration_s=args.duration,
            leak_threshold_bytes=args.leak_threshold,
            progress_interval_s=args.progress_interval,
            screenshot_interval_s=args.screenshot_interval,
        )
        result = SoakRunner(
            connection,
            args.outdir,
            config,
            protocol=info.protocol,
        ).run()
        return int(result.exit_code)
    except (HardwareSerialError, OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return int(SoakExit.CRASH)
    finally:
        connection.close()


if __name__ == "__main__":
    raise SystemExit(main())
