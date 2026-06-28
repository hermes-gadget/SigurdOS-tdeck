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

#pragma once

#include <cstdint>

// ════════════════════════════════════════════════════════
// Multi-language keyboard layout system (Phase 2 — issue #752)
//
// Wadamesh-compatible phonetic transliteration for the T-Deck's
// physical US-QWERTY keyboard. Each layout remaps the alpha keys
// (a-z, A-Z) and optionally digit keys (0-9) to the target
// language's characters via simple array lookup.
//
// Based on wadamesh KeyboardLayouts.cpp by ALLFATHER-BV.
// License: MIT
// ════════════════════════════════════════════════════════

enum class KeyboardLayoutId : uint8_t {
    EN = 0,   // US QWERTY (pass-through)
    BG = 1,   // Bulgarian (phonetic Cyrillic)
    RU = 2,   // Russian (phonetic Cyrillic)
    UK = 3,   // Ukrainian (phonetic Cyrillic)
    SR = 4,   // Serbian (phonetic Cyrillic)
    EL = 5,   // Greek (phonetic)
    AR = 6,   // Arabic (RTL)
    FR = 7,   // French (AZERTY remap)
    NL = 8,   // Dutch (QWERTY + IJ)
    DE = 9,   // German (QWERTZ remap)
    ES = 10,  // Spanish (QWERTY + ñ)
    IT = 11,  // Italian (QWERTY + àèì)
};

/// Human-readable 2-letter label for a layout (e.g. "EN", "BG").
const char* keyboardLayoutName(KeyboardLayoutId id);

/// Total number of layouts in the enum.
constexpr int KEYBOARD_LAYOUT_COUNT = 12;

/// Map a physical T-Deck key to a UTF-8 string in the active layout.
/// @param id       Active layout
/// @param key      ASCII code from the C3 keyboard (e.g. 'a', 'A', '1', '!')
/// @param shifted  True if Shift was active (key >= 'A' && key <= 'Z', or symbol)
/// @return UTF-8 string to insert, or nullptr to pass key through unchanged.
const char* keyboardLayoutMapHwKey(KeyboardLayoutId id, int key, bool shifted);

/// Get / set the active keyboard layout (defaults to EN).
KeyboardLayoutId keyboardLayoutsGetActive();
void keyboardLayoutsSetActive(KeyboardLayoutId id);
