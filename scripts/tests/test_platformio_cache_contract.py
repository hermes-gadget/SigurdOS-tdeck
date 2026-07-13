from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"
HELPER_REF = "uses: ./.github/actions/cache-platformio"
HASH_INPUTS = (
    "platformio.ini",
    "lib/meshcore/library.json",
    "ci/requirements-platformio.in",
    "ci/requirements-platformio.txt",
    "ci/platformio-packages.lock",
)


class PlatformioCacheContractTest(unittest.TestCase):
    def test_every_platformio_job_uses_the_shared_helper(self):
        expected_counts = {
            "build-release.yml": 2,
            "build-validation-matrix.yml": 1,
            "nightly-smoke.yml": 1,
            "pr-ci.yml": 4,
        }
        for filename, expected in expected_counts.items():
            content = (WORKFLOWS / filename).read_text(encoding="utf-8")
            self.assertEqual(content.count(HELPER_REF), expected, filename)
            self.assertNotIn("uses: actions/cache@", content, filename)
            for dependency_input in HASH_INPUTS:
                self.assertIn(dependency_input, content, filename)

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
