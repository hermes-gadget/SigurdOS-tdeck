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
# Requires: curl and `npm ci --prefix scripts/font-tools`.

# Use the official project's immutable static Regular release asset.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FONT_URL="https://raw.githubusercontent.com/JulietaUla/Montserrat/5dae7a4ef9c0bf9fe48dc54fd1076eefaa0a8c7e/fonts/ttf/Montserrat-Regular.ttf"
FONT_SHA256="6eabd0fb8e3066b6155bac338ef5a6c35b9963ab3f39be7c67edc4a99d870699"
FONT_WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$FONT_WORK_DIR"' EXIT
FONT_TMP="$FONT_WORK_DIR/Montserrat-Regular.ttf"
OUTPUT_DIR="${FONT_OUTPUT_DIR:-$SCRIPT_DIR/../src/fonts}"
OUTPUT_C="${OUTPUT_DIR}/latin_ext_font.c"
LV_FONT_CONV="${LV_FONT_CONV:-$SCRIPT_DIR/font-tools/node_modules/.bin/lv_font_conv}"
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

mkdir -p "$OUTPUT_DIR"
curl --fail --location --silent --show-error "$FONT_URL" -o "$FONT_TMP"
echo "$FONT_SHA256  $FONT_TMP" | sha256sum --check
echo "Generating Latin-extended font (size=${FONT_SIZE}, bpp=${BPP})..."
"$LV_FONT_CONV" \
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
sed -i "s|--font $FONT_TMP|--font Montserrat-Regular.ttf|; s|--output $OUTPUT_C|--output latin_ext_font.c|" "$OUTPUT_C"

echo "Done! Generated $OUTPUT_C"
ls -lh "$OUTPUT_C"
