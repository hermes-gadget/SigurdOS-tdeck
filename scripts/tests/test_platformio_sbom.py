"""Tests for the deterministic firmware dependency SBOM."""

from __future__ import annotations

import importlib.util
import tempfile
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
        self.assertGreaterEqual(len(names), 13)
        github_purls = [
            component["purl"] for component in sbom["components"]
            if component["purl"].startswith("pkg:github/")
        ]
        self.assertGreaterEqual(len(github_purls), 9)

    def test_shipped_registry_libraries_use_exact_canonical_purls(self) -> None:
        components = {
            component["name"]: component["purl"]
            for component in MODULE.generate(ROOT / "ci" / "platformio-packages.lock")[
                "components"
            ]
        }
        expected = {
            "Crypto": "pkg:github/rweather/arduinolibs@0.4.0#libraries/Crypto",
            "Melopero RV3028": (
                "pkg:github/melopero/Melopero_RV-3028_Arduino_Library@1.2.0"
            ),
            "WebSockets": "pkg:github/Links2004/arduinoWebSockets@2.7.3",
        }
        for name, purl in expected.items():
            with self.subTest(name=name):
                self.assertEqual(components[name], purl)

    def test_unmapped_production_library_fails_closed(self) -> None:
        lock_text = """\
Libraries
└── Unreviewed @ 1.0.0 (required: example/Unreviewed @ 1.0.0)
"""
        with tempfile.TemporaryDirectory() as directory:
            lock = Path(directory) / "platformio-packages.lock"
            lock.write_text(lock_text, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Unreviewed"):
                MODULE.generate(lock)

    def test_output_is_deterministic(self) -> None:
        lock = ROOT / "ci" / "platformio-packages.lock"
        self.assertEqual(MODULE.generate(lock), MODULE.generate(lock))

    def test_inventory_includes_nested_dependencies_at_any_depth(self) -> None:
        lock_text = """\
Libraries
├── MeshCore @ 1.10.0 (required: file://lib/meshcore @ 1.10.0)
│   ├── RTClib @ 2.1.4 (required: adafruit/RTClib @ ^2.1.3)
│   │   └── RadioLib @ 7.7.1 (required: jgromes/RadioLib @ ^7.6.0)
"""
        with tempfile.TemporaryDirectory() as directory:
            lock = Path(directory) / "platformio-packages.lock"
            lock.write_text(lock_text, encoding="utf-8")
            components = MODULE.generate(lock)["components"]

        names = [component["name"] for component in components]
        self.assertEqual(names.count("RTClib"), 1)
        self.assertEqual(names.count("RadioLib"), 1)


if __name__ == "__main__":
    unittest.main()
