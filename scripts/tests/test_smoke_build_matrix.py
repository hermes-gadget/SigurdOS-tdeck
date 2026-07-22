"""Contracts for named and dynamically complete firmware smoke profiles."""

from __future__ import annotations

import argparse
import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "smoke_build_matrix", ROOT / "scripts" / "smoke_build_matrix.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class SmokeBuildMatrixTests(unittest.TestCase):
    def test_all_firmware_profile_is_discovered_and_excludes_native(self) -> None:
        args = argparse.Namespace(
            env=None,
            profile="all-firmware",
            project_dir=str(ROOT),
        )
        selected = MODULE.selected_envs(args)
        discovered = MODULE.discover_platformio_envs(ROOT)
        self.assertEqual(selected, [env for env in discovered if not env.startswith("native")])
        self.assertTrue(selected)

    def test_named_profiles_do_not_drift_outside_declared_environments(self) -> None:
        declared = set(MODULE.discover_platformio_envs(ROOT))
        for profile, environments in MODULE.PROFILES.items():
            with self.subTest(profile=profile):
                self.assertTrue(environments)
                self.assertLessEqual(set(environments), declared)


if __name__ == "__main__":
    unittest.main()
