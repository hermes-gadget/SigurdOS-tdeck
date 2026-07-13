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

#include "theme.h"
#include <cmath>

namespace sigurdos {
namespace theme {

namespace {

double linear_channel(uint8_t value)
{
    const double channel = value / 255.0;
    return channel <= 0.04045
        ? channel / 12.92
        : std::pow((channel + 0.055) / 1.055, 2.4);
}

double relative_luminance(uint32_t color)
{
    return 0.2126 * linear_channel((color >> 16) & 0xFF) +
           0.7152 * linear_channel((color >> 8) & 0xFF) +
           0.0722 * linear_channel(color & 0xFF);
}

} // namespace

uint32_t contrast_foreground(uint32_t background)
{
    const double luminance = relative_luminance(background);
    const double black_contrast = (luminance + 0.05) / 0.05;
    const double white_contrast = 1.05 / (luminance + 0.05);
    return black_contrast >= white_contrast ? 0x000000 : 0xFFFFFF;
}

uint32_t BG_PRIMARY   = 0x0f0f0f;
uint32_t BG_SECONDARY = 0x181818;
uint32_t BG_TERTIARY  = 0x1e1e1e;
uint32_t BG_INPUT     = 0x252525;

uint32_t ACCENT       = 0x00bfff;
uint32_t ACCENT_HOVER = 0x00a5e0;
uint32_t ACCENT_FOREGROUND = contrast_foreground(ACCENT);

uint32_t CHANNEL_HASH = 0x00bfff;

} // namespace theme
} // namespace sigurdos
