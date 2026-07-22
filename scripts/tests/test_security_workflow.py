"""Regression checks for automated dependency and source security gates."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ACTION_REF = re.compile(r"uses:\s+[^.\s][^@\s]+@([^\s]+)")


class SecurityWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = (
            ROOT / ".github" / "workflows" / "security.yml"
        ).read_text()

    def test_dependency_checks_run_on_dev_prs_pushes_and_a_schedule(self) -> None:
        self.assertIn("push:", self.workflow)
        self.assertIn("pull_request:", self.workflow)
        self.assertIn("schedule:", self.workflow)
        self.assertIn("npm audit --omit=dev --audit-level=high", self.workflow)
        self.assertIn("pio pkg list -e SigurdOS_TDeck", self.workflow)
        self.assertIn("osv-scanner\" scan source", self.workflow)
        self.assertIn("--lockfile \"${{ runner.temp }}/platformio-sbom.cdx.json\"", self.workflow)
        self.assertIn("--config ci/osv-scanner.toml", self.workflow)
        self.assertIn('osv-results.json\" lib', self.workflow)
        self.assertIn("pio run -e SigurdOS_TDeck", self.workflow)

    def test_dependency_inventories_are_required_artifacts(self) -> None:
        self.assertIn("npm sbom --sbom-format cyclonedx", self.workflow)
        self.assertIn("platformio-dependencies.txt", self.workflow)
        self.assertIn("platformio-sbom.cdx.json", self.workflow)
        self.assertIn("osv-results.json", self.workflow)
        self.assertIn("if-no-files-found: error", self.workflow)

    def test_platformio_packages_are_reverified_and_patch_verifier_is_read_only(self) -> None:
        self.assertIn("./.github/actions/cache-platformio", self.workflow)
        self.assertIn("pio pkg install --force", self.workflow)
        self.assertIn("scripts/check_security_patches.py", self.workflow)
        patch_script = (ROOT / "scripts" / "check_security_patches.py").read_text()
        self.assertNotIn("write_text", patch_script)
        self.assertNotIn("replace(before", patch_script)

    def test_font_inputs_and_converter_are_reproducible(self) -> None:
        self.assertIn("npm ci --ignore-scripts --prefix scripts/font-tools", self.workflow)
        self.assertIn("cmp src/fonts/emoji_font.c", self.workflow)
        self.assertIn("cmp src/fonts/latin_ext_font.c", self.workflow)
        for script_name in ("generate_emoji_font.sh", "generate_latin_ext_font.sh"):
            script = (ROOT / "scripts" / script_name).read_text()
            self.assertIn("raw.githubusercontent.com", script)
            self.assertIn("FONT_SHA256=", script)
            self.assertIn("curl --fail --location", script)
            self.assertIn("sha256sum --check", script)
            self.assertNotIn("raw/main", script)

    def test_codeql_runs_extended_cpp_queries(self) -> None:
        self.assertIn("github/codeql-action/init@e0647621c2984b5ed2f768cb892365bf2a616ad1", self.workflow)
        self.assertIn("languages: c-cpp", self.workflow)
        self.assertIn("build-mode: none", self.workflow)
        self.assertIn("queries: security-extended", self.workflow)
        self.assertIn("security-events: write", self.workflow)

    def test_actions_and_platformio_tooling_are_immutable(self) -> None:
        refs = ACTION_REF.findall(self.workflow)
        self.assertGreater(len(refs), 0)
        for ref in refs:
            self.assertRegex(ref, r"^[0-9a-f]{40}$")
        self.assertIn(
            "pip install --require-hashes -r ci/requirements-platformio.txt",
            self.workflow,
        )
        self.assertNotIn("pip install --upgrade platformio", self.workflow)


if __name__ == "__main__":
    unittest.main()
