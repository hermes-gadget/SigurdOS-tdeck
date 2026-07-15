#!/usr/bin/env python3
"""Update one reviewed, application-level PlatformIO dependency pin."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLATFORMIO_INI = ROOT / "platformio.ini"
PACKAGES = {
    "RadioLib": "jgromes/RadioLib",
    "Crypto": "rweather/Crypto",
    "RTClib": "adafruit/RTClib",
    "Melopero-RV3028": "melopero/Melopero RV3028",
    "CayenneLPP": "electroniccats/CayenneLPP",
    "ArduinoJson": "bblanchon/ArduinoJson",
}
VERSION_RE = re.compile(r"^[0-9][0-9A-Za-z.+_-]*$")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dependency", choices=PACKAGES)
    parser.add_argument("version")
    args = parser.parse_args()

    if not VERSION_RE.fullmatch(args.version):
        parser.error("version must be an exact registry version")

    package = PACKAGES[args.dependency]
    content = PLATFORMIO_INI.read_text(encoding="utf-8")
    pattern = re.compile(rf"^(\s*{re.escape(package)}\s*@\s*)\S+\s*$", re.MULTILINE)
    updated, count = pattern.subn(rf"\g<1>{args.version}", content)
    if count != 1:
        raise SystemExit(f"expected exactly one pin for {package}, found {count}")
    PLATFORMIO_INI.write_text(updated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
