#!/usr/bin/env python3
"""Validate literal paths in the repository CODEOWNERS file."""

from __future__ import annotations

import argparse
from pathlib import Path


WILDCARD_CHARS = frozenset("*?[]")


def literal_patterns(text: str) -> list[tuple[int, str]]:
    """Return CODEOWNERS patterns that name one concrete repository path."""

    patterns: list[tuple[int, str]] = []
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        pattern = line.split(maxsplit=1)[0]
        if pattern.startswith("!") or any(char in pattern for char in WILDCARD_CHARS):
            continue
        patterns.append((line_number, pattern.lstrip("/")))
    return patterns


def missing_literal_paths(
    repository_root: Path, codeowners_path: Path | None = None
) -> list[str]:
    """Return diagnostics for literal CODEOWNERS paths absent from the checkout."""

    codeowners = codeowners_path or repository_root / ".github" / "CODEOWNERS"
    patterns = literal_patterns(codeowners.read_text(encoding="utf-8"))
    return [
        f"line {line_number}: /{pattern}"
        for line_number, pattern in patterns
        if not (repository_root / pattern).exists()
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root",
    )
    parser.add_argument("--codeowners", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    codeowners = args.codeowners.resolve() if args.codeowners else None
    try:
        missing = missing_literal_paths(root, codeowners)
    except OSError as exc:
        print(f"CODEOWNERS validation failed: {exc}")
        return 1
    if missing:
        print("CODEOWNERS references missing literal paths:")
        print("\n".join(missing))
        return 1
    print("CODEOWNERS literal paths are valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
