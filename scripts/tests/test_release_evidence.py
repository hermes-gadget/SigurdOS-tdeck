import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PYTHON = sys.executable


class ReleaseEvidenceTests(unittest.TestCase):
    def run_script(self, name, *args):
        return subprocess.run(
            [PYTHON, str(ROOT / "scripts" / name), *map(str, args)],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )

    def test_golden_frames_match_pinned_stock_source(self):
        result = self.run_script("verify_companion_golden_frames.py", "--json")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["frames"], 19)

    def test_release_template_covers_machine_readable_requirements(self):
        result = self.run_script("verify_release_evidence.py")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_soak_report_is_numeric_and_privacy_safe(self):
        with tempfile.TemporaryDirectory() as directory:
            json_out = Path(directory) / "idle.json"
            md_out = Path(directory) / "idle.md"
            result = self.run_script(
                "analyze_soak_log.py", ROOT / "test/fixtures/soak/idle.log",
                "--scenario", "idle", "--json-out", json_out,
                "--markdown-out", md_out, "--min-samples", "4",
                "--min-duration", "15",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(json_out.read_text())
            self.assertTrue(report["passed"])
            self.assertEqual(report["sample_count"], 4)
            self.assertEqual(report["heap_bytes"]["range"], 300)
            self.assertNotIn("boot", json_out.read_text().lower())

    def test_soak_threshold_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_script(
                "analyze_soak_log.py", ROOT / "test/fixtures/soak/active.log",
                "--scenario", "active", "--json-out", Path(directory) / "active.json",
                "--markdown-out", Path(directory) / "active.md", "--min-samples", "4",
                "--min-duration", "15",
                "--max-heap-range", "1000",
            )
            self.assertEqual(result.returncode, 1)

    def test_warning_budget_accepts_baseline_and_rejects_delta(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "build.log"
            log.write_text(
                "src/hal/keyboard.cpp:306:13: warning: unused [-Wunused-function]\n"
                "lib/vendor.cpp:1: warning: ignored [-Wunused-variable]\n"
            )
            result = self.run_script("check_first_party_warnings.py", "--log", log)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            log.write_text(log.read_text() +
                           "src/new.cpp:9:2: warning: new debt [-Wunused-variable]\n")
            result = self.run_script("check_first_party_warnings.py", "--log", log)
            self.assertEqual(result.returncode, 1)
            self.assertIn("src/new.cpp", result.stdout)

    def test_unclassified_first_party_warning_is_not_ignored(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "build.log"
            log.write_text("src/new.cpp:9: warning: compiler warning without an option\n")
            result = self.run_script("check_first_party_warnings.py", "--log", log)
            self.assertEqual(result.returncode, 1)
            self.assertIn("unclassified", result.stdout)


if __name__ == "__main__":
    unittest.main()
