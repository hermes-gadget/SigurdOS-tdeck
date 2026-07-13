#!/usr/bin/env python3
"""Check that release evidence requirements remain represented in templates."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REQUIREMENTS = ROOT / "ci/release_evidence_requirements.json"
TEMPLATE = ROOT / ".github/PULL_REQUEST_TEMPLATE/release.md"
DOC = ROOT / "docs/RELEASE_EVIDENCE.md"


def verify() -> int:
    data = json.loads(REQUIREMENTS.read_text())
    if data.get("schema_version") != 1:
        raise ValueError("unsupported requirements schema")
    requirements = data.get("requirements")
    if not isinstance(requirements, list) or not requirements:
        raise ValueError("release requirements are empty")
    template = TEMPLATE.read_text()
    documentation = DOC.read_text()
    seen: set[str] = set()
    categories: set[str] = set()
    for item in requirements:
        requirement_id = item.get("id")
        if not isinstance(requirement_id, str) or not requirement_id or requirement_id in seen:
            raise ValueError("requirement IDs must be non-empty and unique")
        seen.add(requirement_id)
        category = item.get("category")
        if not isinstance(category, str) or not category:
            raise ValueError(f"{requirement_id}: missing category")
        categories.add(category)
        if not isinstance(item.get("description"), str) or not isinstance(item.get("hardware"), bool):
            raise ValueError(f"{requirement_id}: invalid description/hardware fields")
        token = f"`{requirement_id}`"
        if template.count(token) != 1:
            raise ValueError(f"release template must contain {token} exactly once")
    for category in categories:
        if category not in documentation.lower():
            raise ValueError(f"release documentation does not cover category {category}")
    return len(requirements)


def main() -> int:
    try:
        count = verify()
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"release evidence verification failed: {exc}", file=sys.stderr)
        return 1
    print(f"release evidence contract OK: {count} requirements")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
