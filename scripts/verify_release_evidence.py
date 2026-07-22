#!/usr/bin/env python3
"""Validate human release evidence and bind it to the produced artifact bytes."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from datetime import date, datetime, timezone
from pathlib import Path
from urllib.parse import urlparse

from release_artifact_contract import (
    REQUIRED_RELEASE_ARTIFACTS,
    ReleaseArtifactError,
    validate_release_directory,
)


ROOT = Path(__file__).resolve().parents[1]
REQUIREMENTS = ROOT / "ci/release_evidence_requirements.json"
TEMPLATE = ROOT / ".github/PULL_REQUEST_TEMPLATE/release.md"
DOC = ROOT / "docs/RELEASE_EVIDENCE.md"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


def load_requirements(path: Path = REQUIREMENTS) -> list[dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1:
        raise ValueError("unsupported requirements schema")
    requirements = data.get("requirements")
    if not isinstance(requirements, list) or not requirements:
        raise ValueError("release requirements are empty")
    return requirements


def verify_contract() -> int:
    requirements = load_requirements()
    template = TEMPLATE.read_text(encoding="utf-8")
    documentation = DOC.read_text(encoding="utf-8")
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


def _utc_timestamp(value: object, field: str) -> str:
    if not isinstance(value, str):
        raise ValueError(f"{field} must be an ISO-8601 UTC timestamp")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as exc:
        raise ValueError(f"{field} must be an ISO-8601 UTC timestamp") from exc
    if parsed.tzinfo is None or parsed.utcoffset() != timezone.utc.utcoffset(parsed):
        raise ValueError(f"{field} must include the UTC timezone")
    return value


def _verify_requirements(document: dict, expected_tag: str, max_age_days: int) -> int:
    generated_at = _utc_timestamp(document.get("generated_at"), "generated_at")
    del generated_at
    records = document.get("requirements")
    if not isinstance(records, list):
        raise ValueError("requirements evidence must be a list")
    by_id: dict[str, dict] = {}
    for record in records:
        if not isinstance(record, dict):
            raise ValueError("each evidence record must be an object")
        requirement_id = record.get("id")
        if not isinstance(requirement_id, str) or not requirement_id or requirement_id in by_id:
            raise ValueError("evidence requirement IDs must be non-empty and unique")
        by_id[requirement_id] = record

    requirements = load_requirements()
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


def _declared_hashes(document: dict) -> dict[str, str] | None:
    artifacts = document.get("artifacts")
    if artifacts is None:
        return None
    if not isinstance(artifacts, dict):
        raise ValueError("artifacts must map release filenames to SHA-256 digests")
    for filename, digest in artifacts.items():
        if not isinstance(filename, str) or not SHA256_RE.fullmatch(str(digest)):
            raise ValueError(f"{filename!r}: artifact digest must be lowercase SHA-256")
    return artifacts


def verify_evidence(
    evidence_path: Path,
    expected_tag: str,
    *,
    expected_commit: str | None = None,
    artifacts_dir: Path | None = None,
    max_age_days: int = 30,
) -> tuple[int, dict]:
    if not expected_tag.strip():
        raise ValueError("expected tag must be non-empty")
    if expected_commit is not None and not COMMIT_RE.fullmatch(expected_commit):
        raise ValueError("expected commit must be a full lowercase 40-character SHA")
    if max_age_days < 0:
        raise ValueError("maximum evidence age cannot be negative")

    evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    schema = evidence.get("schema_version")
    if schema not in (1, 2):
        raise ValueError("unsupported evidence schema")
    if evidence.get("tag") != expected_tag:
        raise ValueError("evidence tag does not match the release tag")
    if schema == 1:
        commit = evidence.get("commit")
        if not COMMIT_RE.fullmatch(str(commit)):
            raise ValueError("legacy evidence commit must be a full lowercase SHA")
        if expected_commit is not None and commit != expected_commit:
            raise ValueError("evidence commit does not match the tagged commit")
    elif "commit" in evidence or "artifacts" in evidence:
        raise ValueError(
            "schema 2 source evidence must leave commit and artifacts to the post-build attestation"
        )

    count = _verify_requirements(evidence, expected_tag, max_age_days)
    declared = _declared_hashes(evidence)
    if schema == 1:
        if declared is None:
            raise ValueError("legacy evidence must declare artifact hashes")
        missing = REQUIRED_RELEASE_ARTIFACTS - declared.keys()
        if missing:
            raise ValueError(f"missing artifact hashes: {', '.join(sorted(missing))}")
    if artifacts_dir is not None:
        actual = validate_release_directory(
            artifacts_dir,
            expected_commit=expected_commit,
            expected_version=expected_tag,
        )
        if declared is not None:
            for filename, digest in actual.items():
                if declared.get(filename) != digest:
                    raise ValueError(f"artifact hash does not match produced bytes: {filename}")
    return count, evidence


def write_attestation(
    output_path: Path,
    evidence_path: Path,
    evidence: dict,
    commit: str,
    tag: str,
    artifacts_dir: Path,
) -> dict:
    hashes = validate_release_directory(
        artifacts_dir,
        expected_commit=commit,
        expected_version=tag,
    )
    attestation = {
        "schema_version": 2,
        "kind": "sigurdos-release-attestation",
        "commit": commit,
        "tag": tag,
        "generated_at": evidence["generated_at"],
        "source_evidence_sha256": hashlib.sha256(evidence_path.read_bytes()).hexdigest(),
        "artifacts": hashes,
        "requirements": evidence["requirements"],
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = output_path.with_suffix(output_path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(attestation, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(output_path)
    return attestation


def verify_attestation(
    path: Path,
    commit: str,
    tag: str,
    artifacts_dir: Path,
    max_age_days: int = 30,
) -> int:
    if not COMMIT_RE.fullmatch(commit):
        raise ValueError("expected commit must be a full lowercase 40-character SHA")
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema_version") != 2 or document.get("kind") != "sigurdos-release-attestation":
        raise ValueError("unsupported release attestation schema")
    if document.get("commit") != commit or document.get("tag") != tag:
        raise ValueError("release attestation does not match the tagged commit")
    source_digest = document.get("source_evidence_sha256")
    if not SHA256_RE.fullmatch(str(source_digest)):
        raise ValueError("release attestation has an invalid source evidence digest")
    declared = _declared_hashes(document)
    if declared is None or set(declared) != set(REQUIRED_RELEASE_ARTIFACTS):
        raise ValueError("release attestation must cover the complete artifact contract")
    actual = validate_release_directory(
        artifacts_dir,
        expected_commit=commit,
        expected_version=tag,
    )
    for filename, digest in actual.items():
        if declared.get(filename) != digest:
            raise ValueError(f"attested digest does not match produced bytes: {filename}")
    return _verify_requirements(document, tag, max_age_days)


def main() -> int:
    parser = argparse.ArgumentParser()
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--evidence", type=Path)
    source.add_argument("--attestation", type=Path)
    parser.add_argument("--commit")
    parser.add_argument("--tag")
    parser.add_argument("--artifacts-dir", type=Path)
    parser.add_argument("--write-attestation", type=Path)
    parser.add_argument("--max-age-days", type=int, default=30)
    args = parser.parse_args()
    try:
        contract_count = verify_contract()
        if args.evidence:
            if not args.tag:
                raise ValueError("--evidence requires --tag")
            if args.write_attestation and (not args.commit or not args.artifacts_dir):
                raise ValueError(
                    "--write-attestation requires --commit and --artifacts-dir"
                )
            evidence_count, evidence = verify_evidence(
                args.evidence,
                args.tag,
                expected_commit=args.commit,
                artifacts_dir=args.artifacts_dir,
                max_age_days=args.max_age_days,
            )
            if args.write_attestation:
                write_attestation(
                    args.write_attestation,
                    args.evidence,
                    evidence,
                    args.commit,
                    args.tag,
                    args.artifacts_dir,
                )
        elif args.attestation:
            if not args.commit or not args.tag or not args.artifacts_dir:
                raise ValueError(
                    "--attestation requires --commit, --tag, and --artifacts-dir"
                )
            if args.write_attestation:
                raise ValueError("--write-attestation is only valid with --evidence")
            evidence_count = verify_attestation(
                args.attestation,
                args.commit,
                args.tag,
                args.artifacts_dir,
                args.max_age_days,
            )
        elif args.commit or args.tag or args.artifacts_dir or args.write_attestation:
            raise ValueError("release arguments require --evidence or --attestation")
        else:
            evidence_count = None
    except (OSError, ValueError, json.JSONDecodeError, ReleaseArtifactError) as exc:
        print(f"release evidence verification failed: {exc}", file=sys.stderr)
        return 1
    print(f"release evidence contract OK: {contract_count} requirements")
    if evidence_count is not None:
        print(f"completed release evidence OK: {evidence_count} requirements")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
