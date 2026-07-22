import re
import unittest
from pathlib import Path

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
LOCKED_REQUIREMENTS = {
    "ci/requirements-coverage.txt",
    "ci/requirements-platformio.txt",
}
EXTERNAL_ACTION_REF = re.compile(r"uses:\s+(?!\./)[^@\s]+@([^\s]+)")


class PlatformioCacheContractTest(unittest.TestCase):
    def test_every_external_action_is_commit_pinned(self):
        for workflow_path in WORKFLOWS.glob("*.yml"):
            content = workflow_path.read_text(encoding="utf-8")
            for ref in EXTERNAL_ACTION_REF.findall(content):
                self.assertRegex(ref, r"^[0-9a-f]{40}$", workflow_path.name)

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

    def test_hash_locked_installs_do_not_append_unlocked_packages(self):
        for workflow_path in WORKFLOWS.glob("*.yml"):
            content = workflow_path.read_text(encoding="utf-8")
            for line_number, line in enumerate(content.splitlines(), start=1):
                if "pip install --require-hashes" not in line:
                    continue
                fields = line.split()
                requirement_index = fields.index("-r")
                requirement = fields[requirement_index + 1]
                self.assertTrue(
                    requirement in LOCKED_REQUIREMENTS
                    and requirement_index + 2 == len(fields),
                    f"{workflow_path.name}:{line_number}: {line.strip()}",
                )

    def test_coverage_cache_tracks_its_dedicated_tool_lock(self):
        workflow = yaml.safe_load(
            (WORKFLOWS / "pr-ci.yml").read_text(encoding="utf-8")
        )
        steps = workflow["jobs"]["coverage"]["steps"]
        helper = next(step for step in steps if step.get("uses") == HELPER_ACTION)
        dependency_hash = helper["with"]["dependency-hash"]
        self.assertIn("ci/requirements-coverage.in", dependency_hash)
        self.assertIn("ci/requirements-coverage.txt", dependency_hash)

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
