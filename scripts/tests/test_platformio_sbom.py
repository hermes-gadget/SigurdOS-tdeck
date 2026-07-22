"""Tests for the deterministic firmware dependency SBOM."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "platformio_sbom", ROOT / "scripts" / "generate_platformio_sbom.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class PlatformioSbomTests(unittest.TestCase):
    def test_inventory_covers_framework_toolchain_libraries_and_meshcore(self) -> None:
        sbom = MODULE.generate(ROOT / "ci" / "platformio-packages.lock")
        names = {component["name"] for component in sbom["components"]}
        self.assertIn("framework-arduinoespressif32", names)
        self.assertIn("toolchain-xtensa-esp32s3", names)
        self.assertIn("ArduinoJson", names)
        self.assertIn("MeshCore", names)
        self.assertIn("WebServer", names)
        self.assertGreaterEqual(len(names), 14)
        github_purls = [
            component["purl"] for component in sbom["components"]
            if component["purl"].startswith("pkg:github/")
        ]
        self.assertGreaterEqual(len(github_purls), 10)

    def test_output_is_deterministic(self) -> None:
        lock = ROOT / "ci" / "platformio-packages.lock"
        self.assertEqual(MODULE.generate(lock), MODULE.generate(lock))


if __name__ == "__main__":
    unittest.main()
