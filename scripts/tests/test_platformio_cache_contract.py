from pathlib import Path
import unittest

import yaml


ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
HELPER_ACTION = "./.github/actions/cache-platformio"
HASH_INPUTS = (
    "platformio.ini",
    "lib/meshcore/library.json",
    "ci/requirements-platformio.in",
    "ci/requirements-platformio.txt",
    "ci/platformio-packages.lock",
)


class PlatformioCacheContractTest(unittest.TestCase):
    def test_every_platformio_job_uses_the_shared_helper(self):
        for workflow_path in WORKFLOWS.glob("*.yml"):
            content = workflow_path.read_text(encoding="utf-8")
            workflow = yaml.safe_load(content)
            filename = workflow_path.name
            self.assertNotIn("uses: actions/cache@", content, filename)

            for job_name, job in workflow.get("jobs", {}).items():
                steps = job.get("steps", [])
                job_text = repr(job).lower()
                if "platformio" not in job_text and "pio " not in job_text:
                    continue

                helpers = [
                    step for step in steps
                    if step.get("uses") == HELPER_ACTION
                ]
                label = f"{filename}:{job_name}"
                self.assertEqual(len(helpers), 1, label)
                dependency_hash = helpers[0].get("with", {}).get(
                    "dependency-hash", ""
                )
                for dependency_input in HASH_INPUTS:
                    self.assertIn(dependency_input, dependency_hash, label)

    def test_helper_caches_packages_but_never_build_outputs(self):
        content = (
            ROOT / ".github" / "actions" / "cache-platformio" / "action.yml"
        ).read_text(encoding="utf-8")
        self.assertIn("~/.platformio/.cache", content)
        self.assertIn("~/.platformio/packages", content)
        self.assertIn("~/.platformio/platforms", content)
        self.assertNotIn(".pio/build", content)
        self.assertIn("runner.arch", content)
        self.assertIn("inputs.dependency-hash", content)


if __name__ == "__main__":
    unittest.main()
