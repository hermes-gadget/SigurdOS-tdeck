"""Regression checks for the production native-coverage inventory."""

from __future__ import annotations

import importlib.util
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check_native_coverage_inventory.py"
SPEC = importlib.util.spec_from_file_location("check_native_coverage_inventory", SCRIPT)
assert SPEC and SPEC.loader
inventory = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory)


class NativeCoverageInventoryTests(unittest.TestCase):
    def test_parses_only_production_sources_from_native_filter(self) -> None:
        config = [["env:native_test", [["build_src_filter", [
            "-<*>", "+<hal/storage.cpp>", "+<../test/mocks/mock_spiffs.cpp>",
        ]]]]]
        self.assertEqual(inventory.native_sources_from_config(config), {"src/hal/storage.cpp"})

    def test_rejects_native_environment_source_or_suite_filtering(self) -> None:
        base = [["build_src_filter", ["-<*>", "+<hal/storage.cpp>"]]]
        config = [
            ["env:native_test", base],
            ["env:native_sanitize", base + [["test_filter", "test_storage"]]],
            ["env:native_coverage", [["build_src_filter", ["-<*>"]]]],
        ]
        self.assertEqual(inventory.environment_errors(config), [
            "env:native_sanitize filters the discovered native test suite",
            "env:native_coverage does not use the complete native source filter",
        ])

    def test_requires_reason_and_sources_for_each_exclusion_group(self) -> None:
        excluded, errors = inventory.excluded_sources({"exclusions": [
            {"reason": "", "sources": ["src/a.cpp"]},
            {"reason": "hardware", "sources": []},
        ]})
        self.assertEqual(excluded, ["src/a.cpp"])
        self.assertEqual(errors, [
            "exclusion group 0 has no reason", "exclusion group 1 has no sources",
        ])

    def test_rejects_missing_stale_duplicate_and_overlapping_decisions(self) -> None:
        errors = inventory.inventory_errors(
            {"src/a.cpp", "src/b.cpp", "src/c.cpp"},
            {"src/a.cpp", "src/gone.cpp"},
            ["src/a.cpp", "src/c.cpp", "src/c.cpp", "src/stale.cpp"],
        )
        self.assertEqual(errors, [
            "duplicate exclusions: src/c.cpp",
            "linked sources must not remain excluded: src/a.cpp",
            "sources lack a native coverage decision: src/b.cpp",
            "stale exclusions: src/stale.cpp",
            "native-linked production sources do not exist: src/gone.cpp",
        ])

    def test_repository_inventory_is_complete(self) -> None:
        self.assertEqual(inventory.main(), 0)

    def test_workflow_enforces_the_reviewed_coverage_floors(self) -> None:
        policy = json.loads(
            (ROOT / "ci" / "native_coverage_policy.json").read_text(encoding="utf-8")
        )
        workflow = (ROOT / ".github" / "workflows" / "pr-ci.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn(f"--fail-under-line {policy['minimum_line_percent']}", workflow)
        self.assertIn(f"--fail-under-branch {policy['minimum_branch_percent']}", workflow)


if __name__ == "__main__":
    unittest.main()
