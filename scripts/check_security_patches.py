#!/usr/bin/env python3
"""Fail closed unless the pinned Arduino WebServer package is fully hardened.

This verifier intentionally never edits PlatformIO's package cache.  A framework
upgrade must contain the complete reviewed functions below and update the
expected file hashes in the repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


EXPECTED_PACKAGE_VERSION = "3.20017.241212+sha.dcc1105b"
EXPECTED_HASHES = {
    "Parsing.cpp": "7485c16824cfe52c23e205f1d35c4cf1c4c1eb59c4dbb5952bbaee0578f2caea",
    "WebServer.cpp": "d026efc5ff120059ec405a85a5629f0bd1c5c4e56850aff24ab96718e2b5194a",
}
REQUIRED_BLOCKS = {
    "Parsing.cpp": """bool WebServer::_parseForm(WiFiClient& client, String boundary, uint32_t len){
  (void) len;
  log_v("Parse Form: Boundary: %s Length: %d", boundary.c_str(), len);
  if (boundary.length() > 70) {
    log_e("Multipart boundary too long: %d", boundary.length());
    return false;
  }
  String line;
""",
    "WebServer.cpp": """void WebServer::sendHeader(const String& name, const String& value, bool first) {
  String safeName = name;
  String safeValue = value;
  safeName.replace("\\r", "");
  safeName.replace("\\n", "");
  safeValue.replace("\\r", "");
  safeValue.replace("\\n", "");

  String headerLine = safeName;
  headerLine += F(": ");
  headerLine += safeValue;
  headerLine += "\\r\\n";
""",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(framework_root: Path, overlay_root: Path) -> list[str]:
    failures: list[str] = []
    package_json = framework_root / "package.json"
    source_dir = overlay_root / "src"
    try:
        package = json.loads(package_json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"cannot read framework package metadata: {exc}"]
    if package.get("version") != EXPECTED_PACKAGE_VERSION:
        failures.append(
            f"unsupported framework version {package.get('version')!r}; "
            f"expected {EXPECTED_PACKAGE_VERSION!r}"
        )

    for filename, required in REQUIRED_BLOCKS.items():
        path = source_dir / filename
        try:
            content = path.read_text(encoding="utf-8")
        except OSError as exc:
            failures.append(f"cannot read {filename}: {exc}")
            continue
        if required not in content:
            failures.append(f"{filename}: complete hardened function does not match")
        actual_hash = sha256(path)
        if actual_hash != EXPECTED_HASHES[filename]:
            failures.append(
                f"{filename}: unreviewed content hash {actual_hash}; "
                f"expected {EXPECTED_HASHES[filename]}"
            )
    return failures


def run(framework_root: Path, overlay_root: Path) -> None:
    failures = verify(framework_root, overlay_root)
    if failures:
        for failure in failures:
            print(f"[sec-check] FAIL: {failure}")
        raise SystemExit(1)
    print(f"[sec-check] OK: framework {EXPECTED_PACKAGE_VERSION} with tracked WebServer overlay")
    print("[sec-check] OK: boundary limit and four CR/LF sanitizers")


def cli() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--framework-root", type=Path, required=True)
    parser.add_argument("--overlay-root", type=Path, required=True)
    args = parser.parse_args()
    run(args.framework_root, args.overlay_root)


if __name__ == "__main__":
    cli()
elif "Import" in globals():
    Import("env")
    run(
        Path(env.subst("$PROJECT_PACKAGES_DIR")) / "framework-arduinoespressif32",
        Path(env.subst("$PROJECT_DIR")) / "lib" / "WebServer",
    )
