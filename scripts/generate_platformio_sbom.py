#!/usr/bin/env python3
"""Generate a deterministic CycloneDX inventory from the reviewed PIO lock."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import urllib.parse
import uuid
from pathlib import Path


PACKAGE = re.compile(
    r"^[│ ]*[├└]──\s+(.+?)\s+@\s+(\S+)\s+\(required:\s+(.+?)\s+@\s+(.+?)\)$"
)
# URL/local-path requirements (e.g. lvgl) lack the `owner @ version` form.
PACKAGE_URL = re.compile(
    r"^[│ ]*[├└]──\s+(.+?)\s+@\s+(\S+)\s+\(required:\s+(https?://.+)\)$"
)
MESHCORE = re.compile(r"^MeshCore submodule @ ([0-9a-f]{40})$")
PLATFORM = re.compile(
    r"^Platform\s+(.+?)\s+@\s+(\S+)\s+\(required:\s+(.+?)\s+@\s+(.+?)\)$"
)

# PlatformIO is not an OSV ecosystem. Map reviewed packages to their canonical
# upstream GitHub release so OSV can evaluate them as Git dependencies.
UPSTREAM_PURLS = {
    "espressif32": "pkg:github/platformio/platform-espressif32@v7.0.1",
    "framework-arduinoespressif32": "pkg:github/espressif/arduino-esp32@2.0.17",
    "tool-esptoolpy": "pkg:github/espressif/esptool@v4.11.0",
    "RadioLib": "pkg:github/jgromes/RadioLib@7.7.1",
    "LovyanGFX": "pkg:github/lovyan03/LovyanGFX@1.2.21",
    "lvgl": "pkg:github/lvgl/lvgl@v9.3.0",
    "Adafruit BusIO": "pkg:github/adafruit/Adafruit_BusIO@1.17.4",
    "RTClib": "pkg:github/adafruit/RTClib@2.1.4",
    "CayenneLPP": "pkg:github/ElectronicCats/CayenneLPP@1.6.1",
    "ArduinoJson": "pkg:github/bblanchon/ArduinoJson@v7.4.3",
    "Crypto": "pkg:github/rweather/arduinolibs@0.4.0#libraries/Crypto",
    "Melopero RV3028": (
        "pkg:github/melopero/Melopero_RV-3028_Arduino_Library@1.2.0"
    ),
    "WebSockets": "pkg:github/Links2004/arduinoWebSockets@2.7.3",
}

# Toolchains are resolved production inputs but do not have a canonical library
# repository coordinate. Every production library must be listed in UPSTREAM_PURLS.
UNMAPPED_ALLOWLIST = {
    "toolchain-riscv32-esp",
    "toolchain-xtensa-esp32s3",
}


def component(name: str, version: str, requirement: str) -> dict[str, object]:
    namespace = urllib.parse.quote(requirement.split(" @ ", 1)[0], safe="/")
    purl = UPSTREAM_PURLS.get(name)
    if purl is None and name not in UNMAPPED_ALLOWLIST:
        raise ValueError(f"no canonical upstream PURL for production package {name!r}")
    if purl is None:
        purl = f"pkg:generic/{namespace}@{urllib.parse.quote(version, safe='.+-')}"
    return {
        "type": "library",
        "bom-ref": purl,
        "name": name,
        "version": version,
        "purl": purl,
        "properties": [{"name": "sigurdos:platformio-requirement", "value": requirement}],
    }


def generate(lock_path: Path) -> dict[str, object]:
    text = lock_path.read_text(encoding="utf-8")
    components: list[dict[str, object]] = []
    for raw_line in text.splitlines():
        line = raw_line.strip()
        match = PLATFORM.match(line) or PACKAGE.match(line)
        if match:
            name, version, owner, requirement = match.groups()
            if name == "MeshCore" and owner.startswith("file://"):
                # The git submodule is represented by the dedicated
                # "MeshCore submodule @ <commit>" record below.
                continue
            components.append(component(name, version, f"{owner} @ {requirement}"))
            continue
        url_match = PACKAGE_URL.match(line)
        if url_match:
            name, version, url = url_match.groups()
            components.append(component(name, version, url))
            continue
        meshcore = MESHCORE.match(line)
        if meshcore:
            commit = meshcore.group(1)
            components.append({
                "type": "library",
                "bom-ref": f"pkg:github/meshcore-dev/MeshCore@{commit}",
                "name": "MeshCore",
                "version": commit,
                "purl": f"pkg:github/meshcore-dev/MeshCore@{commit}",
            })

    project_root = lock_path.resolve().parents[1]
    webserver_root = project_root / "lib" / "WebServer"
    if webserver_root.is_dir():
        version = "unknown"
        for line in (webserver_root / "library.properties").read_text().splitlines():
            if line.startswith("version="):
                version = line.split("=", 1)[1]
        digest = hashlib.sha256()
        for source in sorted((webserver_root / "src").rglob("*")):
            if source.is_file():
                digest.update(source.relative_to(webserver_root).as_posix().encode())
                digest.update(source.read_bytes())
        components.append({
            "type": "library",
            "bom-ref": f"pkg:github/espressif/arduino-esp32@2.0.17#libraries/WebServer",
            "name": "WebServer",
            "version": version,
            "purl": "pkg:github/espressif/arduino-esp32@2.0.17#libraries/WebServer",
            "hashes": [{"alg": "SHA-256", "content": digest.hexdigest()}],
            "properties": [{"name": "sigurdos:source", "value": "tracked-security-overlay"}],
        })

    components.sort(key=lambda item: str(item["bom-ref"]))
    lock_digest = hashlib.sha256(text.encode()).hexdigest()
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "serialNumber": f"urn:uuid:{uuid.uuid5(uuid.NAMESPACE_URL, lock_digest)}",
        "version": 1,
        "metadata": {
            "component": {
                "type": "firmware",
                "bom-ref": "pkg:github/hermes-gadget/SigurdOS-tdeck",
                "name": "SigurdOS-tdeck",
            },
            "properties": [{"name": "sigurdos:lock-sha256", "value": lock_digest}],
        },
        "components": components,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, default=Path("ci/platformio-packages.lock"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(
        json.dumps(generate(args.lock), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
