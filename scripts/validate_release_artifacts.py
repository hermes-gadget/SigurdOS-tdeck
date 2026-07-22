#!/usr/bin/env python3
"""Validate all release files, references, hashes, aliases, and image layouts."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from release_artifact_contract import ReleaseArtifactError, validate_release_directory


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts-dir", type=Path, required=True)
    parser.add_argument("--expected-commit")
    parser.add_argument("--expected-version")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        hashes = validate_release_directory(
            args.artifacts_dir,
            expected_commit=args.expected_commit,
            expected_version=args.expected_version,
        )
    except (OSError, ReleaseArtifactError) as exc:
        print(f"release artifact validation failed: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps({"artifacts": hashes}, indent=2, sort_keys=True))
    else:
        print(f"release artifact set OK: {len(hashes)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
