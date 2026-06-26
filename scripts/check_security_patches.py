#!/usr/bin/env python3
"""
Pre-build security check — verifies critical framework patches are in place.

The platformio/espressif32 registry ships framework-arduinoespressif32@3.20017
which contains known CVEs. The patches below are applied directly to the
installed framework files and must be re-applied after any framework reinstall.
"""

import sys
import os

FRAMEWORK_DIR = os.path.expanduser(
    "~/.platformio/packages/framework-arduinoespressif32/libraries/WebServer/src"
)

CHECKS = [
    # GHSA-8cmm-3887-r32j: Stack buffer overflow from unbounded boundary VLA
    ("Parsing.cpp", "boundary.length() > 70", "boundary length limit"),
    # GHSA-5476-9jjq-563m: CRLF injection in sendHeader()
    ("WebServer.cpp", 'safeName.replace("\\r"', "CRLF sanitization"),
]

failed = False
for filename, needle, desc in CHECKS:
    path = os.path.join(FRAMEWORK_DIR, filename)
    if not os.path.exists(path):
        print(f"[sec-check] ERROR: {filename} not found at {path}")
        failed = True
        continue
    
    with open(path) as f:
        content = f.read()
    
    if needle not in content:
        print(f"[sec-check] FAIL: {desc} patch missing from {filename}")
        print(f"[sec-check] → Re-apply patches from git history or CONTRIBUTING.md")
        failed = True
    else:
        print(f"[sec-check] OK: {desc}")

if failed:
    print("[sec-check] HARD FAIL — build aborted to prevent shipping vulnerable firmware")
    sys.exit(1)

print("[sec-check] All security patches verified")
