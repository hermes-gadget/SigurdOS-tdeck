import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class StorageSecurityContractTests(unittest.TestCase):
    def test_production_profile_requires_flash_and_nvs_encryption(self):
        config = (ROOT / "sdkconfig.defaults").read_text()
        for setting in (
            "CONFIG_SECURE_FLASH_ENC_ENABLED=y",
            "CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y",
            "CONFIG_NVS_ENCRYPTION=y",
            "CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=y",
        ):
            self.assertIn(setting, config)

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
