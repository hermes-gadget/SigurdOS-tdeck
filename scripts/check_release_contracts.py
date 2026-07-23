#!/usr/bin/env python3
"""Enforce release-critical source and documentation invariants."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)
LOAD_RE = re.compile(r"lv_scr_load_anim\s*\((.*?)\)\s*;", re.DOTALL)


def without_comments(text: str) -> str:
    return COMMENT_RE.sub("", text)


def source_files(root: Path):
    for suffix in ("*.c", "*.cc", "*.cpp", "*.h", "*.hpp"):
        yield from (root / "src").rglob(suffix)


def documentation_files(root: Path):
    for name in ("README.md", "CONTRIBUTING.md"):
        path = root / name
        if path.is_file():
            yield path
    for directory in (root / "docs", root / "firmware"):
        if directory.is_dir():
            yield from directory.rglob("*.md")


def check(root: Path) -> list[str]:
    violations: list[str] = []

    for obsolete in (
        "scripts/screenshot.py",
        "scripts/capture_png.py",
        "scripts/decode_capture.py",
        "scripts/split_header.py",
        "scripts/split_header_v2.py",
    ):
        if (root / obsolete).exists():
            violations.append(f"{obsolete}: obsolete unsafe helper must stay removed")

    for path in source_files(root):
        code = without_comments(path.read_text(errors="replace"))
        relative = path.relative_to(root)
        if re.search(r"\bauto_del\s*=\s*true\b", code):
            violations.append(f"{relative}: auto_del=true is forbidden")
        for call in LOAD_RE.findall(code):
            arguments = [argument.strip() for argument in call.split(",")]
            if arguments and arguments[-1] == "true":
                violations.append(f"{relative}: lv_scr_load_anim auto-delete must be false")

    keyboard = root / "src/hal/keyboard.cpp"
    if not keyboard.is_file():
        violations.append("src/hal/keyboard.cpp: missing keyboard contract source")
    else:
        code = without_comments(keyboard.read_text(errors="replace"))
        if not re.search(r"\bCMD_MODE_KEY\s*=\s*0x04\b", code):
            violations.append("src/hal/keyboard.cpp: CMD_MODE_KEY must remain 0x04")
        if re.search(r"\bCMD_MODE_RAW\b|Wire\.write\s*\(\s*0x03\s*\)", code):
            violations.append("src/hal/keyboard.cpp: raw keyboard mode 0x03 is forbidden")
        for argument in re.findall(r"set_keyboard_mode\s*\(([^)]*)\)", code):
            argument = argument.strip()
            if argument.startswith("uint8_t "):
                continue
            if argument != "CMD_MODE_KEY":
                violations.append(
                    f"src/hal/keyboard.cpp: set_keyboard_mode({argument}) is not key mode"
                )

    for path in documentation_files(root):
        if path.name == "COMPANION_PARITY_ACTION_PLAN.md":
            continue
        if "releases/latest" in path.read_text(errors="replace"):
            violations.append(
                f"{path.relative_to(root)}: use a versioned /releases/download/<tag>/ URL"
            )

    release_workflow = root / ".github/workflows/build-release.yml"
    workflow = release_workflow.read_text(errors="replace") if release_workflow.is_file() else ""
    if "GITHUB_REF_NAME" not in workflow or "SIGURDOS_VERSION" not in workflow:
        violations.append(
            ".github/workflows/build-release.yml: release tag/version equality gate missing"
        )

    readme_path = root / "README.md"
    if readme_path.is_file():
        readme = readme_path.read_text(errors="replace")
        if re.search(r"Current firmware snapshot:[^\n]*`beta-[^`]+`", readme):
            violations.append("README.md: do not duplicate the firmware version literal")

    return violations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        violations = check(args.root.resolve())
    except OSError as exc:
        print(f"release contract check failed: {exc}", file=sys.stderr)
        return 2
    if violations:
        print("Release contract violations:", file=sys.stderr)
        for violation in violations:
            print(f"- {violation}", file=sys.stderr)
        return 1
    print("Release contracts: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
