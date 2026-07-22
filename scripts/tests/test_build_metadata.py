import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class BuildMetadataContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "scripts" / "build_metadata.py").read_text()

    def test_untracked_build_inputs_mark_build_dirty(self):
        self.assertIn('"--untracked-files=normal"', self.source)
        self.assertNotIn('"--untracked-files=no"', self.source)

    def test_ci_provenance_fields_are_bounded(self):
        for field in (
            "build_source",
            "actions_run_id",
            "actions_run_attempt",
            "actions_ref",
            "actions_run_url",
        ):
            self.assertIn(f"{field} = _bounded({field},", self.source)


if __name__ == "__main__":
    unittest.main()
