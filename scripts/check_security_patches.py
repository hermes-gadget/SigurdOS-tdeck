#!/usr/bin/env python3
"""
Pre-build security hardening for the bundled Arduino WebServer framework.

The platformio/espressif32 package can install a framework version with known
WebServer CVEs. GitHub Actions starts from a clean package cache, so these
patches must be reproducible from tracked repo contents instead of relying on
manual edits under ~/.platformio.
"""

import sys
from pathlib import Path

Import("env")


FRAMEWORK_DIR = (
    Path(env.subst("$PROJECT_PACKAGES_DIR"))
    / "framework-arduinoespressif32"
    / "libraries"
    / "WebServer"
    / "src"
)

PATCHES = [
    {
        "filename": "Parsing.cpp",
        "desc": "boundary length limit",
        "needle": "boundary.length() > 70",
        "before": """bool WebServer::_parseForm(WiFiClient& client, String boundary, uint32_t len){
  (void) len;
  log_v("Parse Form: Boundary: %s Length: %d", boundary.c_str(), len);
  String line;
""",
        "after": """bool WebServer::_parseForm(WiFiClient& client, String boundary, uint32_t len){
  (void) len;
  log_v("Parse Form: Boundary: %s Length: %d", boundary.c_str(), len);
  if (boundary.length() > 70) {
    log_e("Multipart boundary too long: %d", boundary.length());
    return false;
  }
  String line;
""",
    },
    {
        "filename": "WebServer.cpp",
        "desc": "CRLF sanitization",
        "needle": 'safeName.replace("\\r", "");',
        "before": """void WebServer::sendHeader(const String& name, const String& value, bool first) {
  String headerLine = name;
  headerLine += F(": ");
  headerLine += value;
  headerLine += "\\r\\n";
""",
        "after": """void WebServer::sendHeader(const String& name, const String& value, bool first) {
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
    },
]


def patch_file(path, before, after, needle, desc):
    if not path.exists():
        raise FileNotFoundError(f"{path} not found")

    content = path.read_text()
    if needle in content:
        print(f"[sec-check] OK: {desc}")
        return

    if before not in content:
        raise RuntimeError(f"expected upstream block not found in {path.name}")

    path.write_text(content.replace(before, after, 1))
    verified = path.read_text()
    if needle not in verified:
        raise RuntimeError(f"{desc} patch did not verify in {path.name}")
    print(f"[sec-check] PATCHED: {desc}")


failed = False
for patch in PATCHES:
    path = FRAMEWORK_DIR / patch["filename"]
    try:
        patch_file(path, patch["before"], patch["after"], patch["needle"], patch["desc"])
    except Exception as exc:
        print(f"[sec-check] FAIL: {patch['desc']} ({exc})")
        failed = True

if failed:
    print("[sec-check] HARD FAIL - build aborted to prevent shipping vulnerable firmware")
    sys.exit(1)

print("[sec-check] All security patches verified")
