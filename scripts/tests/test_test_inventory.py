"""Regression checks for the generated native-test inventory."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check_test_inventory.py"
SPEC = importlib.util.spec_from_file_location("check_test_inventory", SCRIPT)
assert SPEC and SPEC.loader
inventory = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory)


class TestInventoryTests(unittest.TestCase):
    def test_parses_platformio_collected_line(self) -> None:
        output = "Collected 3 tests (test_alpha, test_beta, test_gamma)\n"
        self.assertEqual(
            inventory.parse_discovered(output),
            ["test_alpha", "test_beta", "test_gamma"],
        )

    def test_rejects_unrecognized_platformio_output(self) -> None:
        with self.assertRaisesRegex(ValueError, "could not parse"):
            inventory.parse_discovered("no tests here")

    def test_parses_platformio_summary_table(self) -> None:
        output = (
            "Collected 2 tests\n"
            "native_test  test_alpha  SKIPPED\n"
            "native_test  test_beta   SKIPPED\n"
            "native_sanitize  test_alpha  SKIPPED\n"
        )
        self.assertEqual(inventory.parse_discovered(output), ["test_alpha", "test_beta"])

    def test_rejects_reported_count_mismatch(self) -> None:
        with self.assertRaisesRegex(ValueError, "reported 2 tests but listed 1"):
            inventory.parse_discovered("Collected 2 tests (test_alpha)")

    def test_parses_only_catalog_entries(self) -> None:
        readme = "|-- mocks/\n|-- test_alpha/ description\ntext test_beta/\n"
        self.assertEqual(inventory.parse_documented(readme), ["test_alpha"])

    def test_reports_missing_stale_and_duplicate_entries(self) -> None:
        errors = inventory.inventory_errors(
            ["test_alpha", "test_beta"],
            ["test_alpha", "test_alpha", "test_stale"],
        )
        self.assertEqual(
            errors,
            [
                "duplicate README entries: test_alpha",
                "undocumented tests: test_beta",
                "documented but undiscovered tests: test_stale",
            ],
        )

    def test_repository_catalog_matches_test_directories(self) -> None:
        directories = sorted(
            path.name
            for path in (ROOT / "test").glob("test_*")
            if path.is_dir()
        )
        documented = inventory.parse_documented(
            (ROOT / "test" / "README.md").read_text(encoding="utf-8")
        )
        self.assertEqual(sorted(documented), directories)


if __name__ == "__main__":
    unittest.main()
