#!/usr/bin/env python3
"""Require every production source to be native-linked or explicitly excluded."""

from __future__ import annotations

import json
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT / "ci" / "native_coverage_policy.json"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}


def native_sources_from_config(config: list[list[object]]) -> set[str]:
    sections = {name: dict(options) for name, options in config}
    filters = sections["env:native_test"]["build_src_filter"]
    return {
        "src/" + entry[2:-1]
        for entry in filters
        if entry.startswith("+<") and not entry.startswith("+<../")
    }


def environment_errors(config: list[list[object]]) -> list[str]:
    sections = {name: dict(options) for name, options in config}
    baseline = sections["env:native_test"]
    errors: list[str] = []
    for environment in ("env:native_sanitize", "env:native_coverage"):
        candidate = sections[environment]
        if candidate["build_src_filter"] != baseline["build_src_filter"]:
            errors.append(f"{environment} does not use the complete native source filter")
        if candidate.get("test_filter") or candidate.get("test_ignore"):
            errors.append(f"{environment} filters the discovered native test suite")
    return errors


def excluded_sources(policy: dict[str, object]) -> tuple[list[str], list[str]]:
    sources: list[str] = []
    errors: list[str] = []
    for index, group in enumerate(policy.get("exclusions", [])):
        reason = str(group.get("reason", "")).strip()
        group_sources = group.get("sources", [])
        if not reason:
            errors.append(f"exclusion group {index} has no reason")
        if not group_sources:
            errors.append(f"exclusion group {index} has no sources")
        sources.extend(str(source) for source in group_sources)
    return sources, errors


def inventory_errors(production: set[str], linked: set[str], excluded: list[str]) -> list[str]:
    errors: list[str] = []
    duplicates = sorted(path for path, count in Counter(excluded).items() if count > 1)
    excluded_set = set(excluded)
    overlap = sorted(linked & excluded_set)
    missing = sorted(production - linked - excluded_set)
    stale = sorted(excluded_set - production)
    missing_linked = sorted(linked - production)
    if duplicates:
        errors.append("duplicate exclusions: " + ", ".join(duplicates))
    if overlap:
        errors.append("linked sources must not remain excluded: " + ", ".join(overlap))
    if missing:
        errors.append("sources lack a native coverage decision: " + ", ".join(missing))
    if stale:
        errors.append("stale exclusions: " + ", ".join(stale))
    if missing_linked:
        errors.append("native-linked production sources do not exist: " + ", ".join(missing_linked))
    return errors


def main() -> int:
    completed = subprocess.run(
        ["pio", "project", "config", "--json-output"], cwd=ROOT,
        check=False, capture_output=True, text=True,
    )
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout)
        sys.stderr.write(completed.stderr)
        return completed.returncode

    config = json.loads(completed.stdout)
    policy = json.loads(POLICY.read_text(encoding="utf-8"))
    production = {
        path.relative_to(ROOT).as_posix()
        for path in (ROOT / "src").rglob("*")
        if path.suffix in SOURCE_SUFFIXES
    }
    linked = native_sources_from_config(config)
    excluded, errors = excluded_sources(policy)
    errors.extend(environment_errors(config))
    errors.extend(inventory_errors(production, linked, excluded))
    if errors:
        for error in errors:
            print(f"native coverage inventory failed: {error}", file=sys.stderr)
        return 1
    print(f"Native coverage inventory matches: {len(linked)} linked, {len(excluded)} reviewed exclusions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
