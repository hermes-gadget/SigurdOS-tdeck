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

    def test_dependency_inventories_are_required_artifacts(self) -> None:
        self.assertIn("npm sbom --sbom-format cyclonedx", self.workflow)
        self.assertIn("platformio-dependencies.txt", self.workflow)
        self.assertIn("if-no-files-found: error", self.workflow)

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
        self.assertIn("./.github/actions/cache-platformio", self.workflow)
        self.assertIn(
            "pip install --require-hashes -r ci/requirements-platformio.txt",
            self.workflow,
        )
        self.assertNotIn("pip install --upgrade platformio", self.workflow)


if __name__ == "__main__":
    unittest.main()
