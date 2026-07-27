import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class StorageSecurityContractTests(unittest.TestCase):
    def test_default_profile_disables_efuse_burning_security_features(self):
        config = (ROOT / "sdkconfig.defaults").read_text()
        for setting in (
            "CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK",
            "CONFIG_SECURE_BOOT",
            "CONFIG_SECURE_BOOT_V2_ENABLED",
            "CONFIG_SECURE_FLASH_ENC_ENABLED",
            "CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE",
            "CONFIG_SECURE_DISABLE_ROM_DL_MODE",
        ):
            self.assertNotRegex(
                config,
                rf"(?m)^{re.escape(setting)}=(?:y|1)$",
                f"{setting} must not be enabled in sdkconfig.defaults",
            )
            self.assertIn(f"# {setting} is not set", config)

    def test_canonical_partition_table_protects_secret_bearing_data(self):
        platformio = (ROOT / "platformio.ini").read_text()
        self.assertIn(
            "board_build.partitions = partitions_sigurdos_16MB.csv",
            platformio,
        )
        table = (ROOT / "partitions_sigurdos_16MB.csv").read_text()
        self.assertIn("nvs_keys, data, nvs_keys", table)
        self.assertRegex(table, r"nvs_keys,.*encrypted")
        self.assertRegex(table, r"spiffs,.*encrypted")


if __name__ == "__main__":
    unittest.main()
