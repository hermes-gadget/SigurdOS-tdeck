// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SigurdOS.
//
// SigurdOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SigurdOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SigurdOS.  If not, see <https://www.gnu.org/licenses/>.

#include "keyboard_layouts.h"

// ════════════════════════════════════════════════════════
// Phonetic transliteration tables for the T-Deck's physical
// US-QWERTY keyboard. Each language has up to four arrays:
//   lower[26]  — a..z remapping
//   upper[26]  — A..Z remapping
//   digits[10] — 0..9 remapping
//   digits_shift[10] — shifted number row remapping
//
// Based on wadamesh KeyboardLayouts.cpp by ALLFATHER-BV.
// License: MIT
// ════════════════════════════════════════════════════════

// ── Bulgarian (phonetic Cyrillic) a..z ─────────────────
static const char* bg_lower[26] = {
    "а", "б", "ц", "д", "е", "ф", "г", "х", "и", "й",
    "к", "л", "м", "н", "о", "п", "я", "р", "с", "т",
    "у", "ж", "в", "ь", "ъ", "з"
};
static const char* bg_upper[26] = {
    "А", "Б", "Ц", "Д", "Е", "Ф", "Г", "Х", "И", "Й",
    "К", "Л", "М", "Н", "О", "П", "Я", "Р", "С", "Т",
    "У", "Ж", "В", "Ь", "Ъ", "З"
};
static const char* bg_digits[10] = {
    nullptr, "ш", "щ", "ч", "ю", nullptr, nullptr, nullptr, nullptr, nullptr
};
static const char* bg_digits_shift[10] = {
    nullptr, "Ш", "Щ", "Ч", "Ю", nullptr, nullptr, nullptr, nullptr, nullptr
};

// ── Russian (phonetic Cyrillic) a..z ──────────────────
static const char* ru_lower[26] = {
    "а", "б", "ц", "д", "е", "ф", "г", "х", "и", "й",
    "к", "л", "м", "н", "о", "п", "я", "р", "с", "т",
    "у", "в", "ш", "ж", "ы", "з"
};
static const char* ru_upper[26] = {
    "А", "Б", "Ц", "Д", "Е", "Ф", "Г", "Х", "И", "Й",
    "К", "Л", "М", "Н", "О", "П", "Я", "Р", "С", "Т",
    "У", "В", "Ш", "Ж", "Ы", "З"
};
static const char* ru_digits[10] = {
    nullptr, "ч", "щ", "ъ", "ь", "э", "ю", "ё", nullptr, nullptr
};
static const char* ru_digits_shift[10] = {
    nullptr, "Ч", "Щ", "Ъ", "Ь", "Э", "Ю", "Ё", nullptr, nullptr
};

// ── Ukrainian (phonetic Cyrillic) a..z ─────────────────
static const char* uk_lower[26] = {
    "а", "б", "ц", "д", "е", "ф", "ґ", "г", "і", "й",
    "к", "л", "м", "н", "о", "п", "я", "р", "с", "т",
    "у", "в", "в", "х", "и", "з"
};
static const char* uk_upper[26] = {
    "А", "Б", "Ц", "Д", "Е", "Ф", "Ґ", "Г", "І", "Й",
    "К", "Л", "М", "Н", "О", "П", "Я", "Р", "С", "Т",
    "У", "В", "В", "Х", "И", "З"
};
static const char* uk_digits[10] = {
    nullptr, "ж", "ч", "ш", "щ", "є", "ї", "ь", "ю", nullptr
};
static const char* uk_digits_shift[10] = {
    nullptr, "Ж", "Ч", "Ш", "Щ", "Є", "Ї", "Ь", "Ю", nullptr
};

// ── Serbian (phonetic Cyrillic) a..z ──────────────────
static const char* sr_lower[26] = {
    "а", "б", "ц", "д", "е", "ф", "г", "х", "и", "ј",
    "к", "л", "м", "н", "о", "п", "љ", "р", "с", "т",
    "у", "в", "ш", "џ", "њ", "з"
};
static const char* sr_upper[26] = {
    "А", "Б", "Ц", "Д", "Е", "Ф", "Г", "Х", "И", "Ј",
    "К", "Л", "М", "Н", "О", "П", "Љ", "Р", "С", "Т",
    "У", "В", "Ш", "Џ", "Њ", "З"
};
static const char* sr_digits[10] = {
    nullptr, "ч", "ћ", "ђ", "ж", nullptr, nullptr, nullptr, nullptr, nullptr
};
static const char* sr_digits_shift[10] = {
    nullptr, "Ч", "Ћ", "Ђ", "Ж", nullptr, nullptr, nullptr, nullptr, nullptr
};

// ── Greek (phonetic) a..z ─────────────────────────────
static const char* el_lower[26] = {
    "α", "β", "ψ", "δ", "ε", "φ", "γ", "η", "ι", "ξ",
    "κ", "λ", "μ", "ν", "ο", "π", nullptr, "ρ", "σ", "τ",
    "θ", "ω", "ς", "χ", "υ", "ζ"
};
static const char* el_upper[26] = {
    "Α", "Β", "Ψ", "Δ", "Ε", "Φ", "Γ", "Η", "Ι", "Ξ",
    "Κ", "Λ", "Μ", "Ν", "Ο", "Π", nullptr, "Ρ", "Σ", "Τ",
    "Θ", "Ω", "Σ", "Χ", "Υ", "Ζ"
};
// Greek fits entirely in a..z — no digit remaps needed

// ── Arabic (phonetic) a..z ────────────────────────────
static const char* ar_lower[26] = {
    "ش", "لا", "ؤ", "ي", "ث", "ب", "ل", "ا", "ه", "ت",
    "ن", "م", "ة", "ى", "خ", "ح", "ض", "ق", "س", "ف",
    "ع", "ر", "ص", "ء", "غ", "ئ"
};
// Arabic is unicameral — upper == lower
static const char* ar_digits[10] = {
    nullptr, "ج", "د", "ذ", "ز", "ط", "ظ", "و", nullptr, nullptr
};

// ── French (AZERTY remap) a..z ────────────────────────
// The physical keyboard is US-QWERTY; we swap letters
// to produce the familiar French AZERTY order.
static const char* fr_lower[26] = {
    "q", "b", "c", "d", "e", "f", "g", "h", "i", "j",
    "k", "l", "m", "n", "o", "p", "a", "r", "s", "t",
    "u", "v", "z", "x", "y", "w"
};
static const char* fr_upper[26] = {
    "Q", "B", "C", "D", "E", "F", "G", "H", "I", "J",
    "K", "L", "M", "N", "O", "P", "A", "R", "S", "T",
    "U", "V", "Z", "X", "Y", "W"
};

// ── German (QWERTZ remap) a..z ────────────────────────
static const char* de_lower[26] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
    "k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
    "u", "v", "w", "x", "z", "y"
};
static const char* de_upper[26] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
    "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
    "U", "V", "W", "X", "Z", "Y"
};

// ── Dutch (QWERTY + IJ shortcut) a..z ─────────────────
static const char* nl_lower[26] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
    "k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
    "u", "v", "w", "x", "y", "z"
};
static const char* nl_upper[26] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
    "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
    "U", "V", "W", "X", "Y", "Z"
};
static const char* nl_digits[10] = {
    nullptr, "ij", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};
static const char* nl_digits_shift[10] = {
    nullptr, "IJ", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};

// ── Spanish (QWERTY + ñ) a..z ─────────────────────────
static const char* es_lower[26] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
    "k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
    "u", "v", "w", "x", "y", "z"
};
static const char* es_upper[26] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
    "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
    "U", "V", "W", "X", "Y", "Z"
};
static const char* es_digits[10] = {
    nullptr, "ñ", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};
static const char* es_digits_shift[10] = {
    nullptr, "Ñ", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};

// ── Italian (QWERTY + àèì) a..z ───────────────────────
static const char* it_lower[26] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
    "k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
    "u", "v", "w", "x", "y", "z"
};
static const char* it_upper[26] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
    "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
    "U", "V", "W", "X", "Y", "Z"
};
static const char* it_digits[10] = {
    nullptr, "à", "è", "ì", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};
static const char* it_digits_shift[10] = {
    nullptr, "À", "È", "Ì", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};

// ════════════════════════════════════════════════════════
// Layout name labels
// ════════════════════════════════════════════════════════

static const char* k_names[KEYBOARD_LAYOUT_COUNT] = {
    "EN", "BG", "RU", "UK", "SR", "EL", "AR", "FR", "NL", "DE", "ES", "IT"
};

const char* keyboardLayoutName(KeyboardLayoutId id) {
    uint8_t idx = static_cast<uint8_t>(id);
    return (idx < KEYBOARD_LAYOUT_COUNT) ? k_names[idx] : "EN";
}

// ════════════════════════════════════════════════════════
// Layout table registry
// ════════════════════════════════════════════════════════

struct HwPhoneticMap {
    const char* const* lower;         // 26, a..z
    const char* const* upper;         // 26, A..Z
    const char* const* digits;        // 10, '0'..'9'
    const char* const* digits_shift;  // 10, shifted number row
};

static const HwPhoneticMap k_hw_maps[KEYBOARD_LAYOUT_COUNT] = {
    /* EN */ { nullptr,  nullptr,  nullptr,  nullptr },
    /* BG */ { bg_lower, bg_upper, bg_digits, bg_digits_shift },
    /* RU */ { ru_lower, ru_upper, ru_digits, ru_digits_shift },
    /* UK */ { uk_lower, uk_upper, uk_digits, uk_digits_shift },
    /* SR */ { sr_lower, sr_upper, sr_digits, sr_digits_shift },
    /* EL */ { el_lower, el_upper, nullptr,  nullptr },
    /* AR */ { ar_lower, ar_lower, ar_digits, ar_digits },
    /* FR */ { fr_lower, fr_upper, nullptr,  nullptr },
    /* NL */ { nl_lower, nl_upper, nl_digits, nl_digits_shift },
    /* DE */ { de_lower, de_upper, nullptr,  nullptr },
    /* ES */ { es_lower, es_upper, es_digits, es_digits_shift },
    /* IT */ { it_lower, it_upper, it_digits, it_digits_shift },
};

// ════════════════════════════════════════════════════════
// Public API
// ════════════════════════════════════════════════════════

static KeyboardLayoutId s_active_layout = KeyboardLayoutId::EN;

KeyboardLayoutId keyboardLayoutsGetActive() {
    return s_active_layout;
}

void keyboardLayoutsSetActive(KeyboardLayoutId id) {
    if (static_cast<uint8_t>(id) < KEYBOARD_LAYOUT_COUNT) {
        s_active_layout = id;
    }
}

const char* keyboardLayoutMapHwKey(KeyboardLayoutId id, int key, bool shifted) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= KEYBOARD_LAYOUT_COUNT) return nullptr;
    const HwPhoneticMap& m = k_hw_maps[idx];
    if (!m.lower) return nullptr;  // English / pass-through

    // Alpha keys
    if (key >= 'a' && key <= 'z') return m.lower[key - 'a'];
    if (key >= 'A' && key <= 'Z') return m.upper[key - 'A'];

    // Digit keys
    if (key >= '0' && key <= '9') {
        int d = key - '0';
        const char* const* tbl = shifted ? m.digits_shift : m.digits;
        return tbl ? tbl[d] : nullptr;
    }

    // A keyboard that sends the shifted symbol ('!' for Shift+1, etc.)
    // instead of the digit+shift flag: map the US-QWERTY top-row symbol
    // to its digit index and reuse the layout's shifted-digit glyph.
    static const char sym[] = ")!@#$%^&*(";  // sym[d] == Shift+d
    for (int d = 0; d <= 9; ++d) {
        if (key == sym[d]) {
            return m.digits_shift ? m.digits_shift[d] : nullptr;
        }
    }
    return nullptr;  // pass through unchanged (space, punctuation, etc.)
}
