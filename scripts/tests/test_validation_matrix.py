"""Keep scheduled firmware compilation coverage in sync with PlatformIO."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLATFORMIO_ENV_RE = re.compile(r"^\[env:([^]]+)]$", re.MULTILINE)
MATRIX_ENV_RE = re.compile(r"^\s+- env:\s+(\S+)\s*$", re.MULTILINE)


class ValidationMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.platformio = (ROOT / "platformio.ini").read_text()
        cls.workflow = (
            ROOT / ".github" / "workflows" / "build-validation-matrix.yml"
        ).read_text()

    def test_every_firmware_environment_is_scheduled(self) -> None:
        configured = {
            environment
            for environment in PLATFORMIO_ENV_RE.findall(self.platformio)
            if not environment.startswith("native")
        }
        scheduled = set(MATRIX_ENV_RE.findall(self.workflow))
        self.assertEqual(scheduled, configured)
        self.assertEqual(len(scheduled), len(MATRIX_ENV_RE.findall(self.workflow)))

    def test_every_cell_builds_and_requires_an_artifact(self) -> None:
        self.assertIn("schedule:", self.workflow)
        self.assertNotIn("requires_wifi", self.workflow)
        self.assertIn("SIGURDOS_GPS_VALIDATION_WIFI_SSID", self.workflow)
        self.assertIn("SIGURDOS_GPS_VALIDATION_WIFI_HOST", self.workflow)
        self.assertIn("if-no-files-found: error", self.workflow)


if __name__ == "__main__":
    unittest.main()
