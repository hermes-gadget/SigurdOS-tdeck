import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "check_meshcore_pin", ROOT / "scripts" / "check_meshcore_pin.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class MeshCorePinTests(unittest.TestCase):
    def test_every_recursive_ci_workflow_checks_the_pin(self):
        workflows = ROOT / ".github" / "workflows"
        for path in workflows.glob("*.yml"):
            text = path.read_text()
            if "submodules: recursive" in text:
                with self.subTest(workflow=path.name):
                    self.assertIn("scripts/check_meshcore_pin.py", text)

    def test_platform_framework_and_esptool_are_exactly_constrained(self):
        config = (ROOT / "platformio.ini").read_text()
        lock = (ROOT / "ci" / "platformio-packages.lock").read_text()
        exact = (
            "platformio/framework-arduinoespressif32@3.20017.241212+sha.dcc1105b",
            "platformio/tool-esptoolpy@2.41100.0",
        )
        for package in exact:
            self.assertIn(package, config)
        self.assertIn(
            "framework-arduinoespressif32 @ 3.20017.241212+sha.dcc1105b "
            "(required: platformio/framework-arduinoespressif32 @ "
            "3.20017.241212+sha.dcc1105b)",
            lock,
        )
        self.assertIn(
            "tool-esptoolpy @ 2.41100.0 "
            "(required: platformio/tool-esptoolpy @ 2.41100.0)",
            lock,
        )

    def test_release_and_monitoring_workflows_verify_resolved_graph(self):
        for name in (
            "build-release.yml",
            "build-validation-matrix.yml",
            "nightly-smoke.yml",
            "pr-ci.yml",
            "security.yml",
        ):
            with self.subTest(workflow=name):
                self.assertIn(
                    "scripts/platformio_lock.py --check",
                    (ROOT / ".github" / "workflows" / name).read_text(),
                )

    def test_lock_requires_exact_gitlink_line(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "lock"
            path.write_text("MeshCore submodule @ " + "a" * 40 + "\n")
            self.assertEqual(MODULE.locked_commit(path), "a" * 40)
            path.write_text("MeshCore submodule @ main\n")
            with self.assertRaisesRegex(ValueError, "missing exact"):
                MODULE.locked_commit(path)

    def test_remote_must_be_explicit_public_https_url(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / ".gitmodules"
            path.write_text(
                '[submodule "lib/meshcore"]\n'
                "\tpath = lib/meshcore\n"
                "\turl = https://github.com/example/MeshCore.git\n"
            )
            self.assertEqual(
                MODULE.declared_remote(path),
                "https://github.com/example/MeshCore.git",
            )
            path.write_text(
                '[submodule "lib/meshcore"]\n'
                "\tpath = lib/meshcore\n"
                "\turl = ../private.git\n"
            )
            with self.assertRaisesRegex(ValueError, "public HTTPS"):
                MODULE.declared_remote(path)

    @mock.patch.object(MODULE.subprocess, "run")
    def test_remote_check_requires_exact_advertised_commit(self, run):
        run.return_value.stdout = (
            "a" * 40 + "\trefs/heads/main\n" + "b" * 40 + "\trefs/tags/v1\n"
        )
        self.assertTrue(MODULE.remote_advertises("a" * 40, "https://example.invalid/x.git"))
        self.assertFalse(MODULE.remote_advertises("c" * 40, "https://example.invalid/x.git"))


if __name__ == "__main__":
    unittest.main()
