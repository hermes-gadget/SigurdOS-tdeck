"""Keep workflow-dispatch input out of write-token shell source."""

import unittest
from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "dependency-refresh.yml"


class DependencyRefreshWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.content = WORKFLOW.read_text(encoding="utf-8")
        cls.workflow = yaml.safe_load(cls.content)

    def test_untrusted_inputs_are_environment_data_not_shell_source(self) -> None:
        steps = self.workflow["jobs"]["refresh"]["steps"]
        update = next(step for step in steps if step.get("name", "").startswith("Update one"))
        self.assertEqual(update["env"]["DEPENDENCY"], "${{ inputs.dependency }}")
        self.assertEqual(update["env"]["VERSION"], "${{ inputs.version }}")
        self.assertNotIn("${{ inputs.", update["run"])
        self.assertIn('"$DEPENDENCY" "$VERSION"', update["run"])
        self.assertNotIn("eval", update["run"])

    def test_dependency_is_a_closed_choice_and_version_is_python_validated(self) -> None:
        self.assertIn("type: choice", self.content)
        self.assertIn("VERSION_RE.fullmatch", (ROOT / "scripts/update_pio_dependency.py").read_text())


if __name__ == "__main__":
    unittest.main()
