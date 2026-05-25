#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
//
// This file is part of SlopOS-TDeck.
//
// SlopOS-TDeck is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SlopOS-TDeck is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with SlopOS-TDeck.  If not, see <https://www.gnu.org/licenses/>.

#include <cstdint>

// ── Display lifecycle ───────────────────────────────────
bool slopos_display_init();
void slopos_display_loop();
uint32_t slopos_display_millis();

// ── Auto-off (power saving) ─────────────────────────────
// After AUTO_OFF_MS of no user input, turns off backlight.
// Touch or keyboard input automatically wakes the display.
// Call slopos_display_wake() from input handlers to reset timer.
void slopos_display_wake();
bool slopos_display_is_on();
void slopos_display_set_brightness(uint8_t brightness);

#if defined(SLOPOS_REMOTE_TEST)
void slopos_test_set_touch(int x, int y);
#endif
