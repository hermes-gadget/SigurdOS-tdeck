#!/usr/bin/env python3
"""Validate the release checklist contract and completed release evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from datetime import date, datetime, timezone
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
REQUIREMENTS = ROOT / "ci/release_evidence_requirements.json"
TEMPLATE = ROOT / ".github/PULL_REQUEST_TEMPLATE/release.md"
DOC = ROOT / "docs/RELEASE_EVIDENCE.md"
REQUIRED_ARTIFACTS = {
    "firmware.bin",
    "firmware-merged.bin",
    "SigurdOS-tdeck-launcher.bin",
    "manifest.json",
}
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


def load_requirements(path: Path = REQUIREMENTS) -> list[dict]:
    data = json.loads(path.read_text())
    if data.get("schema_version") != 1:
        raise ValueError("unsupported requirements schema")
    requirements = data.get("requirements")
    if not isinstance(requirements, list) or not requirements:
        raise ValueError("release requirements are empty")
    return requirements


def verify_contract() -> int:
    requirements = load_requirements()
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


def _https_url(value: object, field: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"{field} must be a non-empty HTTPS URL")
    parsed = urlparse(value)
    if parsed.scheme != "https" or not parsed.netloc:
        raise ValueError(f"{field} must be a non-empty HTTPS URL")
    return value


def _iso_date(value: object, field: str) -> date:
    if not isinstance(value, str):
        raise ValueError(f"{field} must use YYYY-MM-DD format")
    try:
        parsed = date.fromisoformat(value)
    except ValueError as exc:
        raise ValueError(f"{field} must use YYYY-MM-DD format") from exc
    if parsed.isoformat() != value:
        raise ValueError(f"{field} must use YYYY-MM-DD format")
    return parsed


def verify_evidence(
    evidence_path: Path,
    expected_commit: str,
    expected_tag: str,
    max_age_days: int = 30,
) -> int:
    if not COMMIT_RE.fullmatch(expected_commit):
        raise ValueError("expected commit must be a full lowercase 40-character SHA")
    if not expected_tag.strip():
        raise ValueError("expected tag must be non-empty")
    if max_age_days < 0:
        raise ValueError("maximum evidence age cannot be negative")

    requirements = load_requirements()
    evidence = json.loads(evidence_path.read_text())
    if evidence.get("schema_version") != 1:
        raise ValueError("unsupported evidence schema")
    if evidence.get("commit") != expected_commit:
        raise ValueError("evidence commit does not match the tagged commit")
    if evidence.get("tag") != expected_tag:
        raise ValueError("evidence tag does not match the release tag")

    generated_at = evidence.get("generated_at")
    if not isinstance(generated_at, str):
        raise ValueError("generated_at must be an ISO-8601 UTC timestamp")
    try:
        generated = datetime.fromisoformat(generated_at.replace("Z", "+00:00"))
    except ValueError as exc:
        raise ValueError("generated_at must be an ISO-8601 UTC timestamp") from exc
    if generated.tzinfo is None or generated.utcoffset() != timezone.utc.utcoffset(generated):
        raise ValueError("generated_at must include the UTC timezone")

    artifacts = evidence.get("artifacts")
    if not isinstance(artifacts, dict):
        raise ValueError("artifacts must map release filenames to SHA-256 digests")
    missing_artifacts = REQUIRED_ARTIFACTS - artifacts.keys()
    if missing_artifacts:
        raise ValueError(f"missing artifact hashes: {', '.join(sorted(missing_artifacts))}")
    for filename, digest in artifacts.items():
        if not isinstance(filename, str) or not SHA256_RE.fullmatch(str(digest)):
            raise ValueError(f"{filename!r}: artifact digest must be lowercase SHA-256")

    records = evidence.get("requirements")
    if not isinstance(records, list):
        raise ValueError("requirements evidence must be a list")
    by_id: dict[str, dict] = {}
    for record in records:
        if not isinstance(record, dict):
            raise ValueError("each evidence record must be an object")
        requirement_id = record.get("id")
        if not isinstance(requirement_id, str) or requirement_id in by_id:
            raise ValueError("evidence requirement IDs must be non-empty and unique")
        by_id[requirement_id] = record

    required_by_id = {item["id"]: item for item in requirements}
    missing = required_by_id.keys() - by_id.keys()
    extra = by_id.keys() - required_by_id.keys()
    if missing:
        raise ValueError(f"missing requirement evidence: {', '.join(sorted(missing))}")
    if extra:
        raise ValueError(f"unknown requirement evidence: {', '.join(sorted(extra))}")

    today = datetime.now(timezone.utc).date()
    for requirement_id, requirement in required_by_id.items():
        record = by_id[requirement_id]
        if record.get("outcome") != "pass":
            raise ValueError(f"{requirement_id}: outcome must be pass")
        _https_url(record.get("evidence_url"), f"{requirement_id}.evidence_url")
        tested_at = _iso_date(record.get("tested_at"), f"{requirement_id}.tested_at")
        age = (today - tested_at).days
        if age < 0 or age > max_age_days:
            raise ValueError(
                f"{requirement_id}: evidence is not within the allowed {max_age_days}-day window"
            )
        if record.get("firmware_version") != expected_tag:
            raise ValueError(f"{requirement_id}: firmware_version must match the release tag")
        if requirement["hardware"]:
            peer_version = record.get("peer_version")
            if not isinstance(peer_version, str) or not peer_version.strip():
                raise ValueError(f"{requirement_id}: hardware evidence needs a peer_version")
    return len(records)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--commit")
    parser.add_argument("--tag")
    parser.add_argument("--max-age-days", type=int, default=30)
    args = parser.parse_args()
    try:
        contract_count = verify_contract()
        if args.evidence:
            if not args.commit or not args.tag:
                raise ValueError("--evidence requires --commit and --tag")
            evidence_count = verify_evidence(
                args.evidence, args.commit, args.tag, args.max_age_days
            )
        elif args.commit or args.tag:
            raise ValueError("--commit and --tag require --evidence")
        else:
            evidence_count = None
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"release evidence verification failed: {exc}", file=sys.stderr)
        return 1
    print(f"release evidence contract OK: {contract_count} requirements")
    if evidence_count is not None:
        digest = hashlib.sha256(args.evidence.read_bytes()).hexdigest()
        print(f"completed release evidence OK: {evidence_count} requirements, sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
