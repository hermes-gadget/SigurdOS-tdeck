#!/usr/bin/env python3
"""Validate the companion golden-frame corpus against pinned stock MeshCore."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CORPUS = ROOT / "test/fixtures/companion_golden_frames.json"
DEFINE_RE = re.compile(r"^#define\s+([A-Z][A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|[0-9]+)\b", re.MULTILINE)


def fail(message: str) -> None:
    raise ValueError(message)


def load_codes(source: Path) -> dict[str, int]:
    return {name: int(value, 0) for name, value in DEFINE_RE.findall(source.read_text())}


def verify(corpus_path: Path = DEFAULT_CORPUS) -> dict[str, int | str]:
    corpus = json.loads(corpus_path.read_text())
    if corpus.get("schema_version") != 1:
        fail("unsupported corpus schema_version")

    source_info = corpus.get("source", {})
    source = ROOT / source_info.get("path", "")
    if not source.is_file():
        fail(f"pinned source missing: {source}")
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    if digest != source_info.get("sha256"):
        fail("pinned MeshCore source changed; review and regenerate the golden corpus")

    expected_submodule = source_info.get("submodule_commit")
    actual_submodule = subprocess.run(
        ["git", "-C", str(ROOT / "lib/meshcore"), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if actual_submodule != expected_submodule:
        fail(f"MeshCore pin mismatch: expected {expected_submodule}, got {actual_submodule}")

    codes = load_codes(source)
    frames = corpus.get("frames")
    if not isinstance(frames, list) or not frames:
        fail("corpus must contain frames")

    names: set[str] = set()
    directions: set[str] = set()
    for index, frame in enumerate(frames):
        name = frame.get("name")
        if not isinstance(name, str) or not name or name in names:
            fail(f"frame {index} has missing or duplicate name")
        names.add(name)
        direction = frame.get("direction")
        if direction not in {"host_to_device", "device_to_host"}:
            fail(f"{name}: invalid direction")
        directions.add(direction)
        symbol = frame.get("symbol")
        if symbol not in codes:
            fail(f"{name}: {symbol!r} is not defined by pinned MyMesh.cpp")
        try:
            payload = bytes.fromhex(frame.get("hex", ""))
        except ValueError as exc:
            fail(f"{name}: invalid hex ({exc})")
        if not payload or len(payload) > 255:
            fail(f"{name}: frame length must be 1..255 bytes")
        if payload[0] != codes[symbol]:
            fail(f"{name}: first byte does not match {symbol}={codes[symbol]:#04x}")

    if directions != {"host_to_device", "device_to_host"}:
        fail("corpus must cover both protocol directions")
    return {"frames": len(frames), "source_sha256": digest, "submodule": actual_submodule}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", type=Path, default=DEFAULT_CORPUS)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        result = verify(args.corpus)
    except (OSError, ValueError, json.JSONDecodeError, subprocess.CalledProcessError) as exc:
        print(f"golden-frame verification failed: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(result, sort_keys=True))
    else:
        print(f"golden-frame corpus OK: {result['frames']} frames, MeshCore {result['submodule'][:12]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
