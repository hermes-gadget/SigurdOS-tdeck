#!/usr/bin/env python3
"""Validate repository-local Markdown paths, anchors, and key source symbols."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote


LINK_RE = re.compile(r"(?<!!)\[[^]]*\]\(([^)]+)\)")
HEADING_RE = re.compile(r"^#{1,6}\s+(.+?)\s*#*\s*$")
INLINE_MARKUP_RE = re.compile(r"[`*~]")
PUNCT_RE = re.compile(r"[^\w\- ]", re.UNICODE)

SOURCE_CONTRACTS = (
    ("docs/SETTINGS_SCREEN.md", "NodePrefs::advert_loc_policy", "src/hal/prefs.h", "advert_loc_policy"),
    ("docs/FEATURES_OVERVIEW.md", "SIGURDOS_SERIAL_DEBUG_COMMANDS_ACTIVE", "src/hal/display.cpp", "SIGURDOS_SERIAL_DEBUG_COMMANDS_ACTIVE"),
    ("docs/FEATURES_OVERVIEW.md", "GPIO 46 (active high)", "src/hal/tdeck_pins.h", "#define PIN_BUZZER       46"),
    ("docs/LauncherCompatibility.md", "never sends raw-mode command `0x03`", "src/hal/keyboard.cpp", "CMD 0x03 (raw matrix mode) is intentionally unsupported"),
)


def markdown_files(root: Path) -> list[Path]:
    files = [root / name for name in ("README.md", "CONTRIBUTING.md")]
    for directory in ("docs", "firmware", "test"):
        files.extend((root / directory).rglob("*.md"))
    return sorted(path for path in files if path.is_file())


def visible_lines(text: str) -> list[str]:
    lines: list[str] = []
    fenced = False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if not fenced:
            lines.append(line)
    return lines


def slug(text: str) -> str:
    text = INLINE_MARKUP_RE.sub("", text).strip().lower()
    text = PUNCT_RE.sub("", text)
    # GitHub removes punctuation first, then replaces each space with a hyphen;
    # it does not collapse adjacent hyphens created by removed punctuation.
    return text.replace(" ", "-").strip("-")


def anchors(path: Path) -> set[str]:
    result: set[str] = set()
    counts: dict[str, int] = {}
    for line in visible_lines(path.read_text(encoding="utf-8")):
        match = HEADING_RE.match(line)
        if not match:
            continue
        base = slug(match.group(1))
        count = counts.get(base, 0)
        counts[base] = count + 1
        result.add(base if count == 0 else f"{base}-{count}")
    return result


def check_links(root: Path) -> list[str]:
    errors: list[str] = []
    anchor_cache: dict[Path, set[str]] = {}
    for source in markdown_files(root):
        text = "\n".join(visible_lines(source.read_text(encoding="utf-8")))
        for raw_target in LINK_RE.findall(text):
            target = raw_target.strip().split(maxsplit=1)[0].strip("<>")
            if not target or target.startswith(("http://", "https://", "mailto:")):
                continue
            path_text, _, fragment = unquote(target).partition("#")
            destination = source if not path_text else (source.parent / path_text).resolve()
            try:
                destination.relative_to(root)
            except ValueError:
                errors.append(f"{source.relative_to(root)}: link escapes repository: {raw_target}")
                continue
            if not destination.exists():
                errors.append(f"{source.relative_to(root)}: missing target: {raw_target}")
                continue
            if fragment and destination.is_file() and destination.suffix.lower() == ".md":
                available = anchor_cache.setdefault(destination, anchors(destination))
                if fragment not in available:
                    errors.append(f"{source.relative_to(root)}: missing anchor: {raw_target}")
    return errors


def check_source_contracts(root: Path) -> list[str]:
    errors: list[str] = []
    for doc, claim, source, symbol in SOURCE_CONTRACTS:
        if claim not in (root / doc).read_text(encoding="utf-8"):
            errors.append(f"{doc}: required documented claim missing: {claim}")
        if symbol not in (root / source).read_text(encoding="utf-8"):
            errors.append(f"{source}: documented source symbol missing: {symbol}")
    return errors


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    errors = check_links(root) + check_source_contracts(root)
    if errors:
        print("Documentation validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print("Documentation links, anchors, and source-symbol contracts are valid.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
