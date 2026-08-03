#!/usr/bin/env bash
set -euo pipefail
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ben
#
# Generates the 8px compact Latin font used as the smallest rung of the UI
# text-fit ladder (src/ui/text_fit_lvgl.cpp). SemiBold is used instead of
# Regular so the 1px strokes at 8px keep solid white cores instead of
# anti-aliased grey — the downscaled labels must look identical to their
# larger siblings, only smaller.
#
# Requires: curl and `npm ci --prefix scripts/font-tools`.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FONT_URL="https://raw.githubusercontent.com/JulietaUla/Montserrat/5dae7a4ef9c0bf9fe48dc54fd1076eefaa0a8c7e/fonts/ttf/Montserrat-SemiBold.ttf"
FONT_SHA256="49fbfce003ad1692d7c9a6502791577088c12c50088d4caa27dbbfe540ad9d13"
FONT_WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$FONT_WORK_DIR"' EXIT
FONT_TMP="$FONT_WORK_DIR/Montserrat-SemiBold.ttf"
OUTPUT_DIR="${FONT_OUTPUT_DIR:-$SCRIPT_DIR/../src/fonts}"
OUTPUT_C="${OUTPUT_DIR}/montserrat_8.c"
LV_FONT_CONV="${LV_FONT_CONV:-$SCRIPT_DIR/font-tools/node_modules/.bin/lv_font_conv}"
FONT_SIZE=8
BPP=4

# Compact Latin ranges: ASCII, Latin-1 Supplement + Latin Extended-A
# (German/French/Spanish/Italian/Portuguese accents), Euro sign.

mkdir -p "$OUTPUT_DIR"
curl --fail --location --silent --show-error "$FONT_URL" -o "$FONT_TMP"
echo "$FONT_SHA256  $FONT_TMP" | sha256sum --check
echo "Generating 8px compact Latin font (size=${FONT_SIZE}, bpp=${BPP}, SemiBold)..."
"$LV_FONT_CONV" \
    --font "$FONT_TMP" \
    --size $FONT_SIZE \
    --bpp $BPP \
    --format lvgl \
    --no-compress \
    --output "$OUTPUT_C" \
    --lv-include 'lvgl.h' \
    --lv-font-name sigurdos_montserrat_8 \
    -r 0x20-0x7E \
    -r 0xA0-0x17F \
    -r 0x20AC
sed -i "s|--font $FONT_TMP|--font Montserrat-SemiBold.ttf|; s|--output $OUTPUT_C|--output montserrat_8.c|" "$OUTPUT_C"

echo "Done! Generated $OUTPUT_C"
ls -lh "$OUTPUT_C"
