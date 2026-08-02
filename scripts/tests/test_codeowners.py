"""Regression checks for literal CODEOWNERS paths."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check_codeowners.py"
SPEC = importlib.util.spec_from_file_location("check_codeowners", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class CodeownersTests(unittest.TestCase):
    def test_repository_codeowners_literal_paths_exist(self) -> None:
        self.assertEqual(MODULE.missing_literal_paths(ROOT), [])

    def test_missing_literals_are_reported_and_globs_are_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "docs").mkdir()
            codeowners = root / "CODEOWNERS"
            codeowners.write_text(
                "/docs/KNOWN_ISSUES.md @owner\n"
                "/missing.md @owner\n"
                "/docs/* @owner\n",
                encoding="utf-8",
            )
            (root / "docs" / "KNOWN_ISSUES.md").write_text("ok", encoding="utf-8")

            self.assertEqual(
                MODULE.missing_literal_paths(root, codeowners),
                ["line 2: /missing.md"],
            )


if __name__ == "__main__":
    unittest.main()
