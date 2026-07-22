"""Regression checks for native coverage compile and link instrumentation."""

from __future__ import annotations

import json
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class FakeEnvironment:
    def __init__(self) -> None:
        self.link_flags: list[str] = []

    def AppendUnique(self, **values) -> None:
        for flag in values.get("LINKFLAGS", []):
            if flag not in self.link_flags:
                self.link_flags.append(flag)


class NativeCoverageConfigTests(unittest.TestCase):
    def test_environment_attaches_link_instrumentation_script(self) -> None:
        completed = subprocess.run(
            ["pio", "project", "config", "--json-output"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        sections = {
            section: dict(options)
            for section, options in json.loads(completed.stdout)
        }
        scripts = sections["env:native_coverage"]["extra_scripts"]
        self.assertIn("pre:scripts/native_coverage_flags.py", scripts)

    def test_script_adds_coverage_to_link_flags_once(self) -> None:
        fake_env = FakeEnvironment()
        namespace = {
            "env": fake_env,
            "Import": lambda _name: None,
        }
        script = ROOT / "scripts" / "native_coverage_flags.py"

        exec(compile(script.read_text(), str(script), "exec"), namespace)
        exec(compile(script.read_text(), str(script), "exec"), namespace)

        self.assertEqual(fake_env.link_flags, ["--coverage"])


if __name__ == "__main__":
    unittest.main()
