#!/usr/bin/env python3
"""Fail when a firmware build adds first-party src/ compiler warnings."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

WARNING_RE = re.compile(r"^(src/[^:]+):\d+(?::\d+)?: warning: (.*)$")
FLAG_RE = re.compile(r"\[(-W[^]]+)\]\s*$")


def collect(path: Path) -> Counter[str]:
    warnings: Counter[str] = Counter()
    for line in path.read_text(errors="replace").splitlines():
        match = WARNING_RE.match(line)
        if match:
            flag_match = FLAG_RE.search(match.group(2))
            flag = flag_match.group(1) if flag_match else "unclassified"
            warnings[f"{match.group(1)}|{flag}"] += 1
    return warnings


def check(log: Path, baseline_path: Path) -> tuple[list[str], list[str]]:
    baseline = json.loads(baseline_path.read_text())
    if baseline.get("schema_version") != 1 or not isinstance(baseline.get("budgets"), dict):
        raise ValueError("invalid warning baseline")
    actual = collect(log)
    budgets = {key: int(value) for key, value in baseline["budgets"].items()}
    errors = [f"{key}: {count} warning(s), budget {budgets.get(key, 0)}"
              for key, count in sorted(actual.items()) if count > budgets.get(key, 0)]
    reductions = [f"{key}: {count}/{limit} (baseline can be reduced)"
                  for key, limit in sorted(budgets.items())
                  if (count := actual.get(key, 0)) < limit]
    return errors, reductions


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, default=Path("ci/first_party_warnings.json"))
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()
    try:
        errors, reductions = check(args.log, args.baseline)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"warning check failed: {exc}", file=sys.stderr)
        return 2
    lines = ["### First-party compiler warning budget", ""]
    if errors:
        lines += ["New/excess warnings:", ""] + [f"- {item}" for item in errors]
    else:
        lines.append("No warning fingerprint exceeded its checked-in budget.")
    if reductions:
        lines += ["", "Stale warning budgets:", ""] + [f"- {item}" for item in reductions]
    output = "\n".join(lines) + "\n"
    print(output, end="")
    if args.summary:
        with args.summary.open("a") as stream:
            stream.write(output)
    # Under-budget fingerprints are informational only. Failing on reductions
    # breaks multi-env matrices (GPS/telemetry builds do not compile UI sources
    # that legitimately still warn in SigurdOS_TDeck).
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
