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
PROTOCOL_PREFIXES = ("CMD_", "RESP_", "PUSH_", "ERR_")


def fail(message: str) -> None:
    raise ValueError(message)


def load_codes_text(source: str) -> dict[str, int]:
    return {name: int(value, 0) for name, value in DEFINE_RE.findall(source)}


def load_codes(source: Path) -> dict[str, int]:
    return load_codes_text(source.read_text())


def protocol_codes_digest(codes: dict[str, int]) -> str:
    protocol_codes = {
        name: value
        for name, value in codes.items()
        if name.startswith(PROTOCOL_PREFIXES)
    }
    if not protocol_codes:
        fail("source contains no companion protocol codes")
    canonical = json.dumps(
        protocol_codes, sort_keys=True, separators=(",", ":")
    ).encode()
    return hashlib.sha256(canonical).hexdigest()


def load_candidate_source(source_info: dict[str, str], ref: str) -> tuple[str, bytes]:
    meshcore = ROOT / "lib/meshcore"
    source_path = Path(source_info.get("path", ""))
    try:
        relative_source = source_path.relative_to("lib/meshcore")
    except ValueError:
        fail("golden source path is not inside lib/meshcore")

    commit = subprocess.run(
        ["git", "-C", str(meshcore), "rev-parse", "--verify", "--end-of-options",
         f"{ref}^{{commit}}"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    source = subprocess.run(
        ["git", "-C", str(meshcore), "show",
         f"{commit}:{relative_source.as_posix()}"],
        check=True,
        capture_output=True,
    ).stdout
    return commit, source


def verify(
    corpus_path: Path = DEFAULT_CORPUS,
    candidate_ref: str | None = None,
) -> dict[str, int | str]:
    corpus = json.loads(corpus_path.read_text())
    if corpus.get("schema_version") != 2:
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
    codes_digest = protocol_codes_digest(codes)
    if codes_digest != source_info.get("protocol_codes_sha256"):
        fail("pinned companion protocol code snapshot changed")

    reviewed_upstream = corpus.get("reviewed_upstream", {})
    reviewed_commit = reviewed_upstream.get("commit")
    reviewed_source_digest = reviewed_upstream.get("source_sha256")
    reviewed_codes_digest = reviewed_upstream.get("protocol_codes_sha256")
    if not re.fullmatch(r"[0-9a-f]{40}", reviewed_commit or ""):
        fail("reviewed upstream commit must be a full lowercase SHA")
    if not re.fullmatch(r"[0-9a-f]{64}", reviewed_source_digest or ""):
        fail("reviewed upstream source digest must be lowercase SHA-256")
    if reviewed_codes_digest != codes_digest:
        fail("pinned protocol codes differ from the reviewed upstream revision")
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
    result: dict[str, int | str] = {
        "frames": len(frames),
        "source_sha256": digest,
        "submodule": actual_submodule,
        "protocol_codes_sha256": codes_digest,
        "reviewed_upstream": reviewed_commit,
    }

    if candidate_ref:
        candidate_commit, candidate_source = load_candidate_source(
            source_info, candidate_ref
        )
        candidate_source_digest = hashlib.sha256(candidate_source).hexdigest()
        candidate_codes_digest = protocol_codes_digest(
            load_codes_text(candidate_source.decode("utf-8"))
        )
        if candidate_codes_digest != codes_digest:
            fail(
                "candidate MeshCore companion protocol codes differ from the "
                "pinned golden snapshot"
        )
        if candidate_commit == reviewed_commit:
            if candidate_source_digest != reviewed_source_digest:
                fail("reviewed upstream source digest does not match its commit")
        result["candidate"] = candidate_commit
        result["candidate_source_sha256"] = candidate_source_digest

    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus", type=Path, default=DEFAULT_CORPUS)
    parser.add_argument(
        "--candidate-ref",
        help="fetched MeshCore ref/SHA to compare with the pinned protocol codes",
    )
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        result = verify(args.corpus, args.candidate_ref)
    except (OSError, ValueError, json.JSONDecodeError, subprocess.CalledProcessError) as exc:
        print(f"golden-frame verification failed: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(result, sort_keys=True))
    else:
        message = (
            f"golden-frame corpus OK: {result['frames']} frames, "
            f"MeshCore {str(result['submodule'])[:12]}, reviewed upstream "
            f"{str(result['reviewed_upstream'])[:12]}"
        )
        if "candidate" in result:
            message += f", candidate {str(result['candidate'])[:12]}"
        print(message)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
