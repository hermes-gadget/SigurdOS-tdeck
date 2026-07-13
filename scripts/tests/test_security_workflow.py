"""Regression checks for automated dependency and source security gates."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


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
        self.assertIn("github/codeql-action/init@v4", self.workflow)
        self.assertIn("languages: c-cpp", self.workflow)
        self.assertIn("build-mode: none", self.workflow)
        self.assertIn("queries: security-extended", self.workflow)
        self.assertIn("security-events: write", self.workflow)


if __name__ == "__main__":
    unittest.main()
