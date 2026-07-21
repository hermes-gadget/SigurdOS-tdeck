"""Structured JSON, Markdown, and GitHub reporting for hardware tests."""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass, field
from datetime import UTC, datetime
from enum import StrEnum
from pathlib import Path
from typing import Any, Iterable


def utc_now() -> str:
    """Return an ISO-8601 UTC timestamp suitable for machine output."""

    return datetime.now(UTC).isoformat(timespec="seconds")


class TestStatus(StrEnum):
    PASS = "pass"
    FAIL = "fail"
    WARN = "warn"
    SKIP = "skip"


@dataclass(slots=True)
class HeapSample:
    timestamp: str
    elapsed_s: float
    heap_free: int
    heap_min_free: int
    psram_free: int
    battery_percent: int | None = None
    source: str = "stat"

    def to_dict(self) -> dict[str, Any]:
        return {
            "timestamp": self.timestamp,
            "elapsed_s": round(self.elapsed_s, 3),
            "heap_free": self.heap_free,
            "heap_min_free": self.heap_min_free,
            "psram_free": self.psram_free,
            "battery_percent": self.battery_percent,
            "source": self.source,
        }


@dataclass(slots=True)
class ScreenshotArtifact:
    path: str
    timestamp: str
    width: int
    height: int
    stride: int
    raw_bytes: int
    sha256: str
    screen: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return {
            "path": self.path,
            "timestamp": self.timestamp,
            "width": self.width,
            "height": self.height,
            "stride": self.stride,
            "raw_bytes": self.raw_bytes,
            "sha256": self.sha256,
            "screen": self.screen,
        }


@dataclass(slots=True)
class TestResult:
    name: str
    status: TestStatus
    started_at: str
    finished_at: str
    duration_s: float
    detail: str = ""
    critical: bool = False
    data: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "status": self.status.value,
            "started_at": self.started_at,
            "finished_at": self.finished_at,
            "duration_s": round(self.duration_s, 3),
            "detail": self.detail,
            "critical": self.critical,
            "data": self.data,
        }


@dataclass(slots=True)
class HardwareReport:
    mode: str
    transport: str
    started_at: str = field(default_factory=utc_now)
    finished_at: str | None = None
    metadata: dict[str, Any] = field(default_factory=dict)
    results: list[TestResult] = field(default_factory=list)
    heap_samples: list[HeapSample] = field(default_factory=list)
    screenshots: list[ScreenshotArtifact] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    def finalize(self) -> None:
        self.finished_at = self.finished_at or utc_now()

    @property
    def exit_code(self) -> int:
        failures = [item for item in self.results if item.status == TestStatus.FAIL]
        if any(item.critical for item in failures):
            return 2
        if failures:
            return 1
        return 0

    @property
    def outcome(self) -> str:
        return {0: "PASS", 1: "PARTIAL", 2: "CRITICAL"}[self.exit_code]

    def to_dict(self) -> dict[str, Any]:
        self.finalize()
        counts = {status.value: 0 for status in TestStatus}
        for result in self.results:
            counts[result.status.value] += 1
        return {
            "schema_version": 1,
            "mode": self.mode,
            "transport": self.transport,
            "started_at": self.started_at,
            "finished_at": self.finished_at,
            "outcome": self.outcome,
            "exit_code": self.exit_code,
            "counts": counts,
            "metadata": self.metadata,
            "results": [item.to_dict() for item in self.results],
            "heap_samples": [item.to_dict() for item in self.heap_samples],
            "screenshots": [item.to_dict() for item in self.screenshots],
            "notes": self.notes,
        }

    @classmethod
    def from_dict(cls, payload: dict[str, Any]) -> "HardwareReport":
        report = cls(
            mode=str(payload.get("mode", "unknown")),
            transport=str(payload.get("transport", "unknown")),
            started_at=str(payload.get("started_at", utc_now())),
            finished_at=payload.get("finished_at"),
            metadata=dict(payload.get("metadata", {})),
            notes=list(payload.get("notes", [])),
        )
        report.results = [
            TestResult(
                name=str(item["name"]),
                status=TestStatus(str(item["status"])),
                started_at=str(item["started_at"]),
                finished_at=str(item["finished_at"]),
                duration_s=float(item.get("duration_s", 0.0)),
                detail=str(item.get("detail", "")),
                critical=bool(item.get("critical", False)),
                data=dict(item.get("data", {})),
            )
            for item in payload.get("results", [])
        ]
        report.heap_samples = [HeapSample(**item) for item in payload.get("heap_samples", [])]
        report.screenshots = [
            ScreenshotArtifact(**item) for item in payload.get("screenshots", [])
        ]
        return report


def _escape_cell(value: object) -> str:
    return str(value).replace("|", "\\|").replace("\n", "<br>")


def heap_trend_graph(samples: Iterable[HeapSample], width: int = 60) -> str:
    """Render free-heap data as a compact Unicode sparkline."""

    values = [sample.heap_free for sample in samples]
    if not values:
        return "(no heap samples)"
    if len(values) > width:
        step = len(values) / width
        values = [values[min(len(values) - 1, math.floor(i * step))] for i in range(width)]
    low, high = min(values), max(values)
    blocks = ".:-=+*#@"
    if low == high:
        return blocks[3] * len(values) + f"  {low:,} bytes (stable)"
    chars = "".join(blocks[round((value - low) * 7 / (high - low))] for value in values)
    return f"{chars}  min={low:,} max={high:,} Δ={values[-1] - values[0]:+,}"


def markdown_report(report: HardwareReport, *, github_comment: bool = False) -> str:
    """Generate a readable report or PR-comment body."""

    report.finalize()
    title = "## T-Deck hardware test" if github_comment else "# T-Deck hardware test report"
    lines = [
        title,
        "",
        f"**Outcome:** {report.outcome}  ",
        f"**Mode:** `{_escape_cell(report.mode)}`  ",
        f"**Transport:** `{_escape_cell(report.transport)}`  ",
        f"**Started:** `{report.started_at}`  ",
        f"**Finished:** `{report.finished_at}`",
        "",
    ]
    if report.metadata:
        metadata = ", ".join(f"{key}={value}" for key, value in sorted(report.metadata.items()))
        lines.extend([f"Metadata: `{_escape_cell(metadata)}`", ""])

    lines.extend(
        [
            "| Test | Result | Duration | Detail |",
            "|---|---:|---:|---|",
        ]
    )
    icons = {
        TestStatus.PASS: "✅ PASS",
        TestStatus.FAIL: "❌ FAIL",
        TestStatus.WARN: "⚠️ WARN",
        TestStatus.SKIP: "⏭️ SKIP",
    }
    for item in report.results:
        lines.append(
            f"| `{_escape_cell(item.name)}` | {icons[item.status]} | "
            f"{item.duration_s:.1f}s | {_escape_cell(item.detail)} |"
        )
    if not report.results:
        lines.append("| — | ⏭️ SKIP | 0.0s | No tests recorded |")

    lines.extend(["", "### Heap trend", "", f"`{heap_trend_graph(report.heap_samples)}`"])

    if report.screenshots:
        lines.extend(
            [
                "",
                "### Screenshots",
                "",
                "| Screen | Preview | Frame |",
                "|---|---|---|",
            ]
        )
        for shot in report.screenshots:
            label = shot.screen or Path(shot.path).stem
            lines.append(
                f"| {_escape_cell(label)} | ![{_escape_cell(label)}]({_escape_cell(shot.path)}) | "
                f"{shot.width}×{shot.height}, `{shot.sha256[:12]}` |"
            )

    if report.notes:
        lines.extend(["", "### Notes", ""])
        lines.extend(f"- {_escape_cell(note)}" for note in report.notes)
    return "\n".join(lines).rstrip() + "\n"


def write_report_bundle(report: HardwareReport, output_dir: Path) -> dict[str, Path]:
    """Write CI JSON, Markdown, and GitHub-comment Markdown."""

    output_dir.mkdir(parents=True, exist_ok=True)
    paths = {
        "json": output_dir / "results.json",
        "markdown": output_dir / "report.md",
        "github": output_dir / "github-comment.md",
    }
    paths["json"].write_text(
        json.dumps(report.to_dict(), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    paths["markdown"].write_text(markdown_report(report), encoding="utf-8")
    paths["github"].write_text(
        markdown_report(report, github_comment=True),
        encoding="utf-8",
    )
    return paths


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("json_report", type=Path, help="runner results.json")
    parser.add_argument("--output", type=Path, help="Markdown output path")
    parser.add_argument("--github-comment", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        payload = json.loads(args.json_report.read_text(encoding="utf-8"))
        rendered = markdown_report(
            HardwareReport.from_dict(payload),
            github_comment=args.github_comment,
        )
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
        else:
            print(rendered, end="")
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(f"ERROR: cannot render report: {exc}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
