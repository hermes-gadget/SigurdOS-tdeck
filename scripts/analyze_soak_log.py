#!/usr/bin/env python3
"""Produce a privacy-safe heap variance report from SigurdOS [stat] lines."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import statistics
import sys
from pathlib import Path

STAT_RE = re.compile(
    r"^\[stat\]\s+t=(?P<t>\d+)\s+heap=(?P<heap>\d+)/(?P<min_heap>\d+)\s+"
    r"psram=(?P<psram>\d+)\b"
)


def summarize(values: list[int]) -> dict[str, int]:
    return {
        "first": values[0],
        "last": values[-1],
        "minimum": min(values),
        "maximum": max(values),
        "mean": round(statistics.fmean(values)),
        "range": max(values) - min(values),
        "drop": max(0, values[0] - values[-1]),
    }


def analyze(path: Path, scenario: str, min_samples: int, min_duration: int,
            max_heap_range: int, max_heap_drop: int) -> tuple[dict, list[str]]:
    raw = path.read_bytes()
    samples = []
    for line in raw.decode("utf-8", errors="replace").splitlines():
        match = STAT_RE.match(line.strip())
        if match:
            samples.append({key: int(value) for key, value in match.groupdict().items()})
    errors = []
    if len(samples) < min_samples:
        errors.append(f"need at least {min_samples} [stat] samples; found {len(samples)}")
    duration = samples[-1]["t"] - samples[0]["t"] if len(samples) > 1 else 0
    if duration < min_duration:
        errors.append(f"need at least {min_duration} seconds; found {duration}")
    if any(current["t"] <= previous["t"] for previous, current in zip(samples, samples[1:])):
        errors.append("[stat] timestamps must be strictly increasing")
    report = {
        "schema_version": 1,
        "scenario": scenario,
        "source_sha256": hashlib.sha256(raw).hexdigest(),
        "sample_count": len(samples),
        "duration_seconds": duration,
    }
    if samples:
        report["heap_bytes"] = summarize([sample["heap"] for sample in samples])
        report["minimum_heap_bytes"] = min(sample["min_heap"] for sample in samples)
        report["psram_bytes"] = summarize([sample["psram"] for sample in samples])
        if report["heap_bytes"]["range"] > max_heap_range:
            errors.append(f"heap range exceeds {max_heap_range} bytes")
        if report["heap_bytes"]["drop"] > max_heap_drop:
            errors.append(f"heap drop exceeds {max_heap_drop} bytes")
    report["passed"] = not errors
    report["failures"] = errors
    return report, errors


def markdown(report: dict) -> str:
    lines = [
        f"# {report['scenario'].title()} soak report",
        "",
        f"- Result: **{'PASS' if report['passed'] else 'FAIL'}**",
        f"- Samples: {report['sample_count']}",
        f"- Duration: {report['duration_seconds']} seconds",
        f"- Source SHA-256: `{report['source_sha256']}`",
    ]
    if "heap_bytes" in report:
        heap = report["heap_bytes"]
        psram = report["psram_bytes"]
        lines += [
            f"- Heap: min {heap['minimum']}, max {heap['maximum']}, range {heap['range']}, drop {heap['drop']} bytes",
            f"- PSRAM: min {psram['minimum']}, max {psram['maximum']}, range {psram['range']}, drop {psram['drop']} bytes",
        ]
    if report["failures"]:
        lines += ["", "## Failures", ""] + [f"- {failure}" for failure in report["failures"]]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--scenario", required=True, choices=("idle", "active"))
    parser.add_argument("--json-out", type=Path, required=True)
    parser.add_argument("--markdown-out", type=Path, required=True)
    parser.add_argument("--min-samples", type=int, default=120)
    parser.add_argument("--min-duration", type=int, default=600)
    parser.add_argument("--max-heap-range", type=int, default=16384)
    parser.add_argument("--max-heap-drop", type=int, default=8192)
    args = parser.parse_args()
    try:
        report, errors = analyze(args.input, args.scenario, args.min_samples,
                                 args.min_duration, args.max_heap_range,
                                 args.max_heap_drop)
        args.json_out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        args.markdown_out.write_text(markdown(report))
    except OSError as exc:
        print(f"soak analysis failed: {exc}", file=sys.stderr)
        return 2
    print(f"{args.scenario} soak: {report['sample_count']} samples, {'PASS' if not errors else 'FAIL'}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
