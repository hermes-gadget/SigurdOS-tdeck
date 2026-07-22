import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "merge_bin.py"


class MergeBinContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SCRIPT.read_text()

    def test_merge_uses_argument_list_and_propagates_failure(self):
        self.assertIn("_subprocess.run(command, check=True)", self.source)
        self.assertNotIn("env.Execute(merge_cmd)", self.source)
        self.assertNotIn("_os.system", self.source)

    def test_stale_output_is_removed_and_all_inputs_are_mandatory(self):
        self.assertIn("_os.path.lexists(merged_bin)", self.source)
        self.assertIn("_os.remove(merged_bin)", self.source)
        self.assertIn("missing mandatory merge input", self.source)
        self.assertNotIn("skipping merge", self.source.lower())

    def test_new_app_slice_and_launcher_copy_are_verified(self):
        self.assertIn("_assert_embedded_app(merged_bin, firmware_bin, app_offset)", self.source)
        self.assertIn("Launcher copy does not match the merged image", self.source)
        self.assertIn("ESP Web Tools manifest and build metadata written", self.source)


if __name__ == "__main__":
    unittest.main()
