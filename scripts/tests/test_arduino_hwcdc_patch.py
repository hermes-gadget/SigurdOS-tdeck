"""Regression checks for the Arduino-ESP32 2.0.17 HWCDC backport."""

from __future__ import annotations

import json
import re
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "patches" / "arduino-esp32-2.0.17-hwcdc.patch"
BACKPORT_SCRIPT = "pre:scripts/arduino_hwcdc_fix.py"
HW_RUNNER = ROOT / "scripts" / "hw_test" / "hw_test_runner.py"


class ArduinoHwcdcPatchTests(unittest.TestCase):
    def test_unified_diff_hunk_counts_are_valid(self) -> None:
        lines = PATCH.read_text(encoding="utf-8").splitlines()
        for index, line in enumerate(lines):
            if not line.startswith("@@ "):
                continue
            match = re.match(
                r"@@ -\d+(?:,(\d+))? \+\d+(?:,(\d+))? @@", line
            )
            self.assertIsNotNone(match, line)
            expected_old = int(match.group(1) or "1")
            expected_new = int(match.group(2) or "1")

            actual_old = 0
            actual_new = 0
            for body_line in lines[index + 1 :]:
                if body_line.startswith("@@ "):
                    break
                if body_line.startswith(("diff --git ", "--- ", "+++ ")):
                    break
                actual_old += not body_line.startswith("+")
                actual_new += not body_line.startswith("-")

            self.assertEqual(expected_old, actual_old, line)
            self.assertEqual(expected_new, actual_new, line)

    def test_patch_carries_upstream_tx_race_guards(self) -> None:
        patch = PATCH.read_text(encoding="utf-8")
        for required in (
            "hw_cdc_tx_mux",
            "tx_stash_buf",
            "hw_cdc_enable_tx_intr_from_isr",
            "xRingbufferGetMaxItemSize",
            "max_consecutive_timeouts",
        ):
            with self.subTest(required=required):
                self.assertIn(required, patch)

    def test_every_tdeck_firmware_environment_uses_backport(self) -> None:
        completed = subprocess.run(
            ["pio", "project", "config", "--json-output"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        sections = {
            name: dict(options)
            for name, options in json.loads(completed.stdout)
            if name.startswith("env:SigurdOS_TDeck")
        }
        self.assertTrue(sections)
        for environment, config in sections.items():
            with self.subTest(environment=environment):
                self.assertIn(BACKPORT_SCRIPT, config.get("extra_scripts", []))

    def test_hardware_status_probe_waits_for_complete_metrics(self) -> None:
        runner = HW_RUNNER.read_text(encoding="utf-8")
        self.assertIn('STATUS_RESPONSE_MARKER = "lvmem_used_pct="', runner)
        self.assertNotIn('expected=("[test] heap=",)', runner)


if __name__ == "__main__":
    unittest.main()
