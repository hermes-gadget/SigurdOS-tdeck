import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FONT_SOURCE = ROOT / "src/fonts/emoji_font.c"
FONT_SETUP = ROOT / "src/fonts/emoji_font_setup.cpp"


def generated_codepoints():
    source = FONT_SOURCE.read_text()
    values = re.findall(r"/\* U\+([0-9A-F]+)", source)
    return [int(value, 16) for value in values]


def indexed_codepoints():
    source = FONT_SETUP.read_text()
    match = re.search(
        r"static const char\* emoji_list\[\] = \{(.*?)\n\};", source, re.DOTALL
    )
    if not match:
        raise AssertionError("emoji_list was not found")

    codepoints = []
    for literal in re.findall(r'"((?:\\x[0-9A-Fa-f]{2})+)"', match.group(1)):
        raw = bytes(
            int(value, 16)
            for value in re.findall(r"\\x([0-9A-Fa-f]{2})", literal)
        )
        value = raw.decode("utf-8")
        if len(value) != 1:
            raise AssertionError(f"index entry is not one codepoint: {literal}")
        codepoints.append(ord(value))
    return codepoints


class EmojiFontAssetTests(unittest.TestCase):
    def test_generated_font_and_runtime_index_are_identical(self):
        generated = generated_codepoints()
        indexed = indexed_codepoints()

        self.assertEqual(len(generated), 365)
        self.assertEqual(len(generated), len(set(generated)))
        self.assertEqual(len(indexed), len(set(indexed)))
        self.assertEqual(set(generated), set(indexed))

    def test_corrected_and_previously_omitted_glyphs_are_present(self):
        required = {0x1F913, 0x1F923, 0x1F9D0}
        self.assertTrue(required.issubset(set(generated_codepoints())))
        self.assertTrue(required.issubset(set(indexed_codepoints())))


if __name__ == "__main__":
    unittest.main()
