import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class DiagnosticsContractTests(unittest.TestCase):
    def test_crash_clear_query_has_terminal_record(self):
        source = (ROOT / "src" / "diagnostics" / "telemetry.cpp").read_text()
        self.assertIn('emit_end_resp("crash clear", 1, micros() - start);', source)

if __name__ == "__main__":
    unittest.main()
