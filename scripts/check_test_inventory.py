#!/usr/bin/env python3
"""Compare PlatformIO's discovered native tests with the documented catalog."""

from __future__ import annotations

import re
import subprocess
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
README = ROOT / "test" / "README.md"
COLLECTED_RE = re.compile(r"Collected\s+(\d+)\s+tests(?:\s+\(([^)]*)\))?")
SUMMARY_RE = re.compile(r"^native_test\s+(test_[A-Za-z0-9_]+)\s+", re.MULTILINE)
DOCUMENTED_RE = re.compile(r"^\|-- (test_[A-Za-z0-9_]+)/", re.MULTILINE)


def parse_discovered(output: str) -> list[str]:
    """Return the test names from PlatformIO's --list-tests output."""
    match = COLLECTED_RE.search(output)
    if not match:
        raise ValueError("could not parse PlatformIO test inventory")
    if match.group(2):
        names = [name.strip() for name in match.group(2).split(",") if name.strip()]
    else:
        names = SUMMARY_RE.findall(output)
    names = list(dict.fromkeys(names))
    if len(names) != int(match.group(1)):
        raise ValueError(
            f"PlatformIO reported {match.group(1)} tests but listed {len(names)}"
        )
    return names


def parse_documented(readme: str) -> list[str]:
    """Return test directory names from the README tree catalog."""
    return DOCUMENTED_RE.findall(readme)


def inventory_errors(discovered: list[str], documented: list[str]) -> list[str]:
    errors: list[str] = []
    duplicates = sorted(name for name, count in Counter(documented).items() if count > 1)
    missing = sorted(set(discovered) - set(documented))
    stale = sorted(set(documented) - set(discovered))
    if duplicates:
        errors.append("duplicate README entries: " + ", ".join(duplicates))
    if missing:
        errors.append("undocumented tests: " + ", ".join(missing))
    if stale:
        errors.append("documented but undiscovered tests: " + ", ".join(stale))
    return errors


def main() -> int:
    completed = subprocess.run(
        ["pio", "test", "-e", "native_test", "--list-tests"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout)
        sys.stderr.write(completed.stderr)
        return completed.returncode

    try:
        discovered = parse_discovered(completed.stdout + completed.stderr)
    except ValueError as error:
        print(f"test inventory check failed: {error}", file=sys.stderr)
        return 1

    documented = parse_documented(README.read_text(encoding="utf-8"))
    errors = inventory_errors(discovered, documented)
    if errors:
        for error in errors:
            print(f"test inventory check failed: {error}", file=sys.stderr)
        return 1

    print(f"Test inventory matches: {len(discovered)} PlatformIO targets documented")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
