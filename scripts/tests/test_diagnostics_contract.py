import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class DiagnosticsContractTests(unittest.TestCase):
    def test_crash_clear_query_has_terminal_record(self):
        source = (ROOT / "src" / "diagnostics" / "telemetry.cpp").read_text()
        self.assertIn('emit_end_resp("crash clear", 1, micros() - start);', source)

    def test_ui_text_dump_is_opt_in(self):
        config = (ROOT / "src" / "diagnostics" / "debug_cfg.h").read_text()
        debug = (ROOT / "src" / "diagnostics" / "debug.cpp").read_text()
        self.assertIn("#define SIGURDOS_DEBUG_UI_TEXT 0", config)
        self.assertIn("#if SIGURDOS_DEBUG_UI_TEXT", debug)


if __name__ == "__main__":
    unittest.main()
