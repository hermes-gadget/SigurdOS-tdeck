import json
import subprocess
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PYTHON = sys.executable
REQUIRED_ARTIFACTS = {
    "firmware.bin",
    "firmware-merged.bin",
    "SigurdOS-tdeck-launcher.bin",
    "firmware-debug.bin",
    "manifest.json",
    "build-metadata.json",
    "sigurdos-tdeck-bootloader.bin",
    "sigurdos-tdeck-partitions.bin",
    "sigurdos-tdeck-boot_app0.bin",
    "sigurdos-tdeck-firmware.bin",
    "sigurdos-tdeck-full.bin",
    "sigurdos-tdeck-launcher.bin",
}


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

    def make_completed_evidence(self, commit, tag):
        requirements = json.loads(
            (ROOT / "ci/release_evidence_requirements.json").read_text()
        )["requirements"]
        tested_at = datetime.now(timezone.utc).date().isoformat()
        records = []
        for requirement in requirements:
            record = {
                "id": requirement["id"],
                "outcome": "pass",
                "evidence_url": f"https://example.invalid/evidence/{requirement['id']}",
                "tested_at": tested_at,
                "firmware_version": tag,
            }
            if requirement["hardware"]:
                record["peer_version"] = "test-peer-1"
            records.append(record)
        return {
            "schema_version": 1,
            "commit": commit,
            "tag": tag,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "artifacts": {
                name: f"{index:064x}" for index, name in enumerate(
                    sorted(REQUIRED_ARTIFACTS), start=1
                )
            },
            "requirements": records,
        }

    def run_evidence(self, evidence, commit="a" * 40, tag="beta-test"):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "evidence.json"
            path.write_text(json.dumps(evidence))
            return self.run_script(
                "verify_release_evidence.py",
                "--evidence", path,
                "--commit", commit,
                "--tag", tag,
            )

    def test_completed_evidence_is_bound_to_tag_and_commit(self):
        commit = "a" * 40
        tag = "beta-test"
        result = self.run_evidence(self.make_completed_evidence(commit, tag), commit, tag)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("completed release evidence OK", result.stdout)

    def test_incomplete_or_failed_evidence_blocks_release(self):
        commit = "a" * 40
        evidence = self.make_completed_evidence(commit, "beta-test")
        evidence["requirements"].pop()
        result = self.run_evidence(evidence, commit)
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing requirement evidence", result.stderr)

        evidence = self.make_completed_evidence(commit, "beta-test")
        evidence["requirements"][0]["outcome"] = "fail"
        result = self.run_evidence(evidence, commit)
        self.assertEqual(result.returncode, 1)
        self.assertIn("outcome must be pass", result.stderr)

    def test_mismatched_commit_and_missing_hashes_block_release(self):
        evidence = self.make_completed_evidence("b" * 40, "beta-test")
        result = self.run_evidence(evidence)
        self.assertEqual(result.returncode, 1)
        self.assertIn("tagged commit", result.stderr)

        evidence = self.make_completed_evidence("a" * 40, "beta-test")
        del evidence["artifacts"]["manifest.json"]
        result = self.run_evidence(evidence)
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing artifact hashes", result.stderr)

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

    def test_warning_budget_accepts_clean_log_and_rejects_delta(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "build.log"
            log.write_text(
                "lib/vendor.cpp:1: warning: ignored [-Wunused-variable]\n"
            )
            result = self.run_script("check_first_party_warnings.py", "--log", log)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            log.write_text(log.read_text() +
                           "src/new.cpp:9:2: warning: new debt [-Wunused-variable]\n")
            result = self.run_script("check_first_party_warnings.py", "--log", log)
            self.assertEqual(result.returncode, 1)
            self.assertIn("src/new.cpp", result.stdout)

    def test_stale_warning_budget_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            log = directory / "build.log"
            baseline = directory / "warnings.json"
            log.write_text("")
            baseline.write_text(json.dumps({
                "schema_version": 1,
                "environment": "SigurdOS_TDeck",
                "budgets": {"src/old.cpp|-Wunused-variable": 1},
            }))
            result = self.run_script(
                "check_first_party_warnings.py",
                "--log", log,
                "--baseline", baseline,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("Stale warning budgets", result.stdout)
            self.assertIn("src/old.cpp", result.stdout)

    def test_unclassified_first_party_warning_is_not_ignored(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "build.log"
            log.write_text("src/new.cpp:9: warning: compiler warning without an option\n")
            result = self.run_script("check_first_party_warnings.py", "--log", log)
            self.assertEqual(result.returncode, 1)
            self.assertIn("unclassified", result.stdout)


if __name__ == "__main__":
    unittest.main()
