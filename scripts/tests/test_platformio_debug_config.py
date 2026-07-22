"""Regression checks for resolved debug and validation compiler flags."""

from __future__ import annotations

import json
import re
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

DEBUG_ENVIRONMENTS = (
    "SigurdOS_TDeck_ble_validation",
    "SigurdOS_TDeck_ble_agent",
    "SigurdOS_TDeck_trackball_debug",
    "SigurdOS_TDeck_debug",
    "SigurdOS_TDeck_debug_869",
    "SigurdOS_TDeck_debug_display",
    "SigurdOS_TDeck_debug_mesh",
    "SigurdOS_TDeck_debug_ui",
    "SigurdOS_TDeck_debug_map",
    "SigurdOS_TDeck_debug_diag",
    "SigurdOS_TDeck_telemetry",
    "SigurdOS_TDeck_telemetry_diff",
    "SigurdOS_TDeck_remote_test",
    "SigurdOS_TDeck_remote_test_radio",
    "SigurdOS_TDeck_remote_test_radio_testfreq",
    "SigurdOS_TDeck_remote_test_radio_roomtest",
    "SigurdOS_TDeck_remote_test_radio_usca",
    "SigurdOS_TDeck_remote_test_radio_usca_rxonly",
    "SigurdOS_TDeck_remote_test_radio_meshv2",
    "SigurdOS_TDeck_companion_usb",
    "SigurdOS_TDeck_gps_validation",
    "SigurdOS_TDeck_gps_validation_wifi",
)
SILENT_DEBUG_ENVIRONMENTS = {"SigurdOS_TDeck_companion_usb"}
TELEMETRY_ENVIRONMENTS = (
    "SigurdOS_TDeck_telemetry",
    "SigurdOS_TDeck_telemetry_diff",
)

CONTROLLED_RADIO_MACROS = (
    "LORA_FREQ",
    "LORA_BW",
    "LORA_SF",
    "LORA_CR",
    "LORA_TX_PWR",
    "LORA_TX_POWER",
)


def macro_name(flag: str) -> str | None:
    match = re.fullmatch(r"-D\s*([A-Za-z_][A-Za-z0-9_]*)(?:=.*)?", flag)
    return match.group(1) if match else None


class PlatformIODebugConfigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        completed = subprocess.run(
            ["pio", "project", "config", "--json-output"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        cls.sections = {
            section: dict(options) for section, options in json.loads(completed.stdout)
        }

    def effective_flags(self, environment: str) -> list[str]:
        config = self.sections[f"env:{environment}"]
        removed = set(config.get("build_unflags", []))
        return [flag for flag in config["build_flags"] if flag not in removed]

    def test_debug_and_validation_environments_replace_release_controls(self) -> None:
        for environment in DEBUG_ENVIRONMENTS:
            with self.subTest(environment=environment):
                config = self.sections[f"env:{environment}"]
                flags = self.effective_flags(environment)
                macros = [macro_name(flag) for flag in flags]

                self.assertEqual(config.get("build_type"), "debug")
                self.assertIn("-Og", flags)
                self.assertIn("-UNDEBUG", flags)
                self.assertNotIn("NDEBUG", macros)
                self.assertEqual(macros.count("CORE_DEBUG_LEVEL"), 1)
                expected_level = 0 if environment in SILENT_DEBUG_ENVIRONMENTS else 5
                self.assertIn(f"-DCORE_DEBUG_LEVEL={expected_level}", flags)

    def test_controlled_radio_macros_have_one_effective_definition(self) -> None:
        for environment in DEBUG_ENVIRONMENTS:
            macros = [macro_name(flag) for flag in self.effective_flags(environment)]
            for macro in CONTROLLED_RADIO_MACROS:
                with self.subTest(environment=environment, macro=macro):
                    self.assertEqual(macros.count(macro), 1)

    def test_companion_usb_replaces_ble_transport_definition(self) -> None:
        flags = self.effective_flags("SigurdOS_TDeck_companion_usb")
        macros = [macro_name(flag) for flag in flags]
        self.assertEqual(macros.count("SIGURDOS_COMPANION_BLE"), 1)
        self.assertIn("-DSIGURDOS_COMPANION_BLE=0", flags)
        self.assertIn("-DCORE_DEBUG_LEVEL=0", flags)

    def test_telemetry_builds_compile_the_flash_coredump_contract(self) -> None:
        for environment in TELEMETRY_ENVIRONMENTS:
            with self.subTest(environment=environment):
                flags = self.effective_flags(environment)
                macros = [macro_name(flag) for flag in flags]
                self.assertEqual(macros.count("SIGURDOS_TELEMETRY"), 1)
                self.assertIn("-D SIGURDOS_TELEMETRY=1", flags)

        workflow = (ROOT / ".github/workflows/pr-ci.yml").read_text(encoding="utf-8")
        self.assertIn("pio run -e SigurdOS_TDeck_telemetry", workflow)

        source = (ROOT / "src/diagnostics/telemetry_crash.cpp").read_text(
            encoding="utf-8"
        )
        for symbol in (
            "CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH",
            "CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF",
            "CONFIG_ESP_COREDUMP_CHECKSUM_CRC32",
            "CONFIG_ESP_COREDUMP_MAX_TASKS_NUM",
        ):
            with self.subTest(symbol=symbol):
                self.assertIn(f'#error "Telemetry crash recovery', source)
                self.assertIn(symbol, source)


if __name__ == "__main__":
    unittest.main()
