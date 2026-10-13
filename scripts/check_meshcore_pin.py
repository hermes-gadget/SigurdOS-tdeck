#!/usr/bin/env python3
"""Fail when the MeshCore gitlink, lock, or declared remote diverge.

The tree path (gitlink == lock line) is checked offline. ``--check-remote``
additionally proves the pinned commit is still fetchable: because the declared
branch tip moves with upstream development, the pin is required to be an
ancestor of the fetched branch tip rather than equal to it.
"""

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
SUBMODULE_PATH = ROOT / "lib" / "meshcore"
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


def _declared_options(path: Path) -> configparser.SectionProxy:
    parser = configparser.ConfigParser()
    parser.read(path, encoding="utf-8")
    section = 'submodule "lib/meshcore"'
    if not parser.has_section(section):
        raise ValueError(f"{path}: missing {section} section")
    return parser[section]


def declared_remote(path: Path = GITMODULES_PATH) -> str:
    section = _declared_options(path)
    if "url" not in section:
        raise ValueError(f"{path}: missing lib/meshcore URL")
    url = section.get("url", "").strip()
    if not url.startswith("https://github.com/") or not url.endswith(".git"):
        raise ValueError("lib/meshcore must use an explicit public HTTPS GitHub remote")
    return url


def declared_branch(path: Path = GITMODULES_PATH) -> str:
    section = _declared_options(path)
    branch = section.get("branch", "").strip()
    if not branch:
        raise ValueError(f"{path}: missing lib/meshcore branch")
    if not re.fullmatch(r"[A-Za-z0-9._/-]+", branch):
        raise ValueError(f"{path}: invalid lib/meshcore branch {branch!r}")
    return branch


def remote_history_contains(
    commit: str,
    remote: str,
    branch: str,
    submodule: Path = SUBMODULE_PATH,
) -> bool:
    """True when ``commit`` is reachable from ``branch``'s advertised history.

    Fetch the declared branch's commit graph (blobs are filtered out, so this
    stays cheap) into the submodule checkout and require the pinned commit to
    be an ancestor of the fetched tip. Works for shallow CI checkouts too,
    because the pinned commit is by construction present locally.
    """
    if not (submodule / ".git").exists():
        raise ValueError(
            "lib/meshcore is not initialized; run git submodule update --init"
        )
    subprocess.run(
        ["git", "fetch", "--no-tags", "--filter=blob:none", remote,
         f"refs/heads/{branch}"],
        cwd=submodule,
        check=True,
        capture_output=True,
        text=True,
        timeout=120,
    )
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", commit, "FETCH_HEAD"],
        cwd=submodule,
        capture_output=True,
        text=True,
        timeout=60,
    )
    return result.returncode == 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check-remote",
        action="store_true",
        help=(
            "also require the gitlink to be reachable from the declared "
            "branch on the declared remote"
        ),
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
        branch = declared_branch()
        if args.check_remote and not remote_history_contains(gitlink, remote, branch):
            raise ValueError(
                f"MeshCore commit {gitlink} is not in the history of "
                f"{branch} at {remote}"
            )
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"MeshCore pin check failed: {exc}", file=sys.stderr)
        return 1
    print(f"MeshCore pin OK: {gitlink} ({remote} {branch})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
