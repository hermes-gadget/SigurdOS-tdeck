import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "check_release_contracts", ROOT / "scripts/check_release_contracts.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ReleaseContractTests(unittest.TestCase):
    def make_root(self):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        (root / "src/hal").mkdir(parents=True)
        (root / "src/ui").mkdir(parents=True)
        (root / "docs").mkdir()
        (root / ".github/workflows").mkdir(parents=True)
        (root / "src/hal/keyboard.cpp").write_text(
            "static constexpr unsigned CMD_MODE_KEY = 0x04;\n"
            "bool set_keyboard_mode(uint8_t command) { return true; }\n"
            "void init() { set_keyboard_mode(CMD_MODE_KEY); }\n"
        )
        (root / "src/ui/screen.cpp").write_text(
            "void show() { lv_scr_load_anim(scr, anim, 0, 0, false); }\n"
        )
        (root / "docs/README.md").write_text(
            "Use /releases/download/v1.2.3/firmware.bin.\n"
        )
        (root / ".github/workflows/build-release.yml").write_text(
            "run: compare GITHUB_REF_NAME with SIGURDOS_VERSION\n"
        )
        return temporary, root

    def test_current_repository_passes(self):
        self.assertEqual(MODULE.check(ROOT), [])

    def test_accepts_false_auto_delete_and_key_mode(self):
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        self.assertEqual(MODULE.check(root), [])

    def test_rejects_true_auto_delete_even_multiline(self):
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        (root / "src/ui/screen.cpp").write_text(
            "lv_scr_load_anim(\n  scr, LV_SCR_LOAD_ANIM_NONE, 0, 0,\n  true\n);\n"
        )
        self.assertTrue(any("auto-delete" in item for item in MODULE.check(root)))

    def test_rejects_named_true_auto_delete_policy(self):
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        (root / "src/ui/screen.cpp").write_text("bool auto_del = true;\n")
        self.assertTrue(any("auto_del=true" in item for item in MODULE.check(root)))

    def test_ignores_auto_delete_examples_in_comments(self):
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        (root / "src/ui/screen.cpp").write_text(
            "// lv_scr_load_anim(scr, anim, 0, 0, true);\n"
            "/* auto_del=true */\n"
        )
        self.assertEqual(MODULE.check(root), [])

    def test_rejects_raw_keyboard_mode(self):
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        with (root / "src/hal/keyboard.cpp").open("a") as stream:
            stream.write("void raw() { Wire.write(0x03); }\n")
        self.assertTrue(any("raw keyboard" in item for item in MODULE.check(root)))

    def test_rejects_bare_latest_documentation(self):
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        (root / "docs/README.md").write_text(
            "https://example.invalid/releases/latest/download/firmware.bin\n"
        )
        self.assertTrue(any("versioned" in item for item in MODULE.check(root)))


if __name__ == "__main__":
    unittest.main()
