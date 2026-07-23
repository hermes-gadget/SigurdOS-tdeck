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
    r"^[├└]──\s+(.+?)\s+@\s+(\S+)\s+\(required:\s+(.+?)\s+@\s+(.+?)\)$"
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
}


def component(name: str, version: str, requirement: str) -> dict[str, object]:
    namespace = urllib.parse.quote(requirement.split(" @ ", 1)[0], safe="/")
    purl = UPSTREAM_PURLS.get(
        name, f"pkg:generic/{namespace}@{urllib.parse.quote(version, safe='.+-')}"
    )
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
            components.append(component(name, version, f"{owner} @ {requirement}"))
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
