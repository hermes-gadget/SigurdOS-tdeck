#!/usr/bin/env python3
"""Report native coverage scope by reviewed firmware risk domain."""

from __future__ import annotations

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT / "ci" / "native_coverage_policy.json"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}


def production_sources() -> set[str]:
    return {
        path.relative_to(ROOT).as_posix()
        for path in (ROOT / "src").rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    }


def excluded_sources(policy: dict[str, object]) -> set[str]:
    return {
        str(source)
        for group in policy.get("exclusions", [])
        for source in group.get("sources", [])
    }


def percent(part: int, whole: int) -> str:
    return f"{part * 100 / whole:.1f}%" if whole else "0.0%"


def main() -> int:
    policy = json.loads(POLICY.read_text(encoding="utf-8"))
    production = production_sources()
    excluded = excluded_sources(policy)
    domains = policy.get("risk_domains", [])
    errors: list[str] = []
    assigned: set[str] = set()

    if not isinstance(domains, list) or not domains:
        errors.append("coverage policy has no risk_domains")
        domains = []

    for domain in domains:
        if not isinstance(domain, dict):
            errors.append("risk domain must be an object")
            continue
        name = str(domain.get("name", "")).strip()
        sources = {str(source) for source in domain.get("sources", [])}
        if not name or not sources:
            errors.append("every risk domain needs a name and sources")
            continue
        unknown = sorted(sources - production)
        if unknown:
            errors.append(f"{name}: sources are not production files: {', '.join(unknown)}")
        not_excluded = sorted(sources - excluded)
        if not_excluded:
            errors.append(f"{name}: sources are not reviewed exclusions: {', '.join(not_excluded)}")
        duplicate = sorted(assigned & sources)
        if duplicate:
            errors.append(f"{name}: sources appear in multiple risk domains: {', '.join(duplicate)}")
        assigned.update(sources)

    if errors:
        for error in errors:
            print(f"coverage risk-domain report failed: {error}", file=sys.stderr)
        return 1

    print("Native coverage scope (the gcovr headline is not full-firmware coverage)")
    print(f"Production source files: {len(production)}")
    print(f"Reviewed excluded source files: {len(excluded)}")
    print(f"Overall excluded-source %: {percent(len(excluded), len(production))}")
    print("Excluded risk domains (percentage of all production source files):")
    for domain in domains:
        name = str(domain["name"])
        sources = {str(source) for source in domain["sources"]}
        print(
            f"- {name}: excluded-source % = {percent(len(sources), len(production))} "
            f"({len(sources)}/{len(production)})"
        )

    residual = sorted(excluded - assigned)
    if residual:
        print(
            f"- other reviewed exclusions: excluded-source % = "
            f"{percent(len(residual), len(production))} ({len(residual)}/{len(production)})"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
