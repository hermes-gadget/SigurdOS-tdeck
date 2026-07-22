"""Tests for the fail-closed Arduino WebServer verifier."""

from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check_security_patches.py"


def load_verifier():
    spec = importlib.util.spec_from_file_location("security_patch_verifier", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader
    spec.loader.exec_module(module)
    return vars(module)


class SecurityPatchVerifierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.verifier = load_verifier()
        cls.blocks = cls.verifier["REQUIRED_BLOCKS"]

    def make_framework(self) -> tuple[Path, Path]:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        framework = root / "framework"
        overlay = root / "overlay"
        source = overlay / "src"
        source.mkdir(parents=True)
        framework.mkdir()
        (framework / "package.json").write_text(json.dumps({
            "version": self.verifier["EXPECTED_PACKAGE_VERSION"]
        }))
        for filename, block in self.blocks.items():
            (source / filename).write_text(block)
        return framework, overlay

    def verify_blocks_only(self, roots: tuple[Path, Path]) -> list[str]:
        framework, overlay = roots
        with mock.patch.dict(self.verifier["EXPECTED_HASHES"], {
            name: self.verifier["sha256"](
                overlay / "src" / name
            )
            for name in self.blocks
        }, clear=True):
            return self.verifier["verify"](framework, overlay)

    def test_complete_reviewed_blocks_pass(self) -> None:
        self.assertEqual(self.verify_blocks_only(self.make_framework()), [])

    def test_each_security_line_is_mandatory(self) -> None:
        required_lines = [
            "if (boundary.length() > 70)",
            "return false;",
            'safeName.replace("\\r", "");',
            'safeName.replace("\\n", "");',
            'safeValue.replace("\\r", "");',
            'safeValue.replace("\\n", "");',
            "String headerLine = safeName;",
            "headerLine += safeValue;",
        ]
        for required_line in required_lines:
            with self.subTest(line=required_line):
                root = self.make_framework()
                _, overlay = root
                for filename in self.blocks:
                    path = overlay / "src" / filename
                    path.write_text(path.read_text().replace(required_line, "// removed", 1))
                self.assertTrue(self.verify_blocks_only(root))

    def test_unknown_framework_version_fails(self) -> None:
        root = self.make_framework()
        framework, _ = root
        (framework / "package.json").write_text(json.dumps({"version": "unknown"}))
        self.assertTrue(self.verify_blocks_only(root))


if __name__ == "__main__":
    unittest.main()
