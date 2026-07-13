#!/usr/bin/env bash
set -euo pipefail
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Ben
#
# This script downloads Montserrat Latin-extended font (SIL OFL) and generates
# an LVGL-compatible C source with Latin Supplement + Latin Extended-A ranges.
# These glyphs cover Western/Central European accented characters that are
# missing from the built-in lv_font_montserrat_* (ASCII-only) fonts.
#
# Requires: curl, lv_font_conv (npm install -g lv_font_conv)

# Use a pre-instantiated static Regular font.
# The Google Fonts variable font is converted to static Regular via fonttools.
FONT_TMP="/tmp/Montserrat-Regular.ttf"
OUTPUT_DIR="$(dirname "$0")/../src/fonts"
OUTPUT_C="${OUTPUT_DIR}/latin_ext_font.c"
OUTPUT_H="${OUTPUT_DIR}/latin_ext_font.h"
FONT_SIZE=16
BPP=4

# Character ranges for European accented Latin characters
#
# Latin-1 Supplement (U+00C0-U+00FF): ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖ×ØÙÚÛÜÝÞß
#   àáâãäåæçèéêëìíîïðñòóôõö÷øùúûüýþÿ
#   Covers: German umlauts (äöüÄÖÜß), French (éèêëàâçùûôîïœ),
#   Spanish (áéíóúñü), Italian (àèéìòù), Portuguese (áàâãçéêíóôõú)
#   Nordic (æøå), and all other Western/Central European Latin characters.
#
# Latin Extended-A (U+0100-U+017F): ĀāĂăĄąĆćĈĉĊċČčĎďĐđĒēĔĕĖėĘęĚě
#   ĜĝĞğĠġĢģĤĥĦħĨĩĪīĬĭĮįİıĲĳĴĵĶķĸĹĺĻļĽľĿŀŁłŃńŅņŇňŉŊŋŌōŎŏŐőŒœ
#   ŔŕŖŗŘřŚśŜŝŞşŠšŢţŤťŦŧŨũŪūŬŭŮůŰűŲųŴŵŶŷŸŹźŻżŽžſ
#   Covers: Œœ (French), ČčŠšŽž (Eastern Euro), Łł (Polish), etc.
#
# Euro sign (U+20AC): €

echo "Using pre-prepared Montserrat Regular font at $FONT_TMP"
echo "Generating Latin-extended font (size=${FONT_SIZE}, bpp=${BPP})..."
lv_font_conv \
    --font "$FONT_TMP" \
    --size $FONT_SIZE \
    --bpp $BPP \
    --format lvgl \
    --no-compress \
    --output "$OUTPUT_C" \
    --lv-include 'lvgl.h' \
    --lv-font-name latin_ext_font \
    -r 0x00C0-0x017F \
    -r 0x20AC

echo "Generating header..."
cat > "$OUTPUT_H" << 'HEADER'
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 Ben
#pragma once
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Latin-extended font (Montserrat Regular) providing Latin-1 Supplement +
// Latin Extended-A glyphs for Western/Central European accented characters.
// Used as a fallback between the ASCII-only Montserrat fonts and the emoji font.
// Size: 16px, Bpp: 4 (grayscale anti-aliasing)
extern const lv_font_t latin_ext_font;

#ifdef __cplusplus
}
#endif
HEADER

echo "Done! Generated:"
echo "  $OUTPUT_C"
echo "  $OUTPUT_H"
ls -lh "$OUTPUT_C" "$OUTPUT_H"
