#!/usr/bin/env python3
"""Fail quickly when the MeshCore gitlink, lock, or declared remote diverge."""

from __future__ import annotations

import argparse
import configparser
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "ci" / "platformio-packages.lock"
GITMODULES_PATH = ROOT / ".gitmodules"
LOCK_RE = re.compile(r"^MeshCore submodule @ ([0-9a-f]{40})$", re.MULTILINE)


def gitlink_commit(root: Path = ROOT) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD:lib/meshcore"],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    commit = result.stdout.strip()
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("lib/meshcore is not a valid gitlink")
    return commit


def locked_commit(path: Path = LOCK_PATH) -> str:
    match = LOCK_RE.search(path.read_text(encoding="utf-8"))
    if not match:
        raise ValueError(f"{path}: missing exact MeshCore submodule lock")
    return match.group(1)


def declared_remote(path: Path = GITMODULES_PATH) -> str:
    parser = configparser.ConfigParser()
    parser.read(path, encoding="utf-8")
    section = 'submodule "lib/meshcore"'
    if not parser.has_option(section, "url"):
        raise ValueError(f"{path}: missing lib/meshcore URL")
    url = parser.get(section, "url").strip()
    if not url.startswith("https://github.com/") or not url.endswith(".git"):
        raise ValueError("lib/meshcore must use an explicit public HTTPS GitHub remote")
    return url


def remote_advertises(commit: str, remote: str) -> bool:
    result = subprocess.run(
        ["git", "ls-remote", remote],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    )
    return any(line.split(maxsplit=1)[0] == commit for line in result.stdout.splitlines())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check-remote",
        action="store_true",
        help="also require the exact gitlink at an advertised remote ref",
    )
    args = parser.parse_args()
    try:
        gitlink = gitlink_commit()
        locked = locked_commit()
        if locked != gitlink:
            raise ValueError(
                f"MeshCore lock {locked} does not match repository gitlink {gitlink}"
            )
        remote = declared_remote()
        if args.check_remote and not remote_advertises(gitlink, remote):
            raise ValueError(f"MeshCore commit {gitlink} is not advertised by {remote}")
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"MeshCore pin check failed: {exc}", file=sys.stderr)
        return 1
    print(f"MeshCore pin OK: {gitlink} ({remote})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
