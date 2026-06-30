#!/usr/bin/env bash
set -euo pipefail
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ben

# Generate the non-Latin fallback used by the physical keyboard layouts.
# DejaVu Sans is distributed under the Bitstream Vera font license; the notice
# is retained in LICENSES/DejaVu-Fonts.txt.

FONT_FILE="${1:-/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf}"
LV_FONT_CONV="${LV_FONT_CONV:-lv_font_conv}"
OUTPUT="$(dirname "$0")/../src/fonts/keyboard_layout_font.c"
TEMP_OUTPUT="$(mktemp)"
trap 'rm -f "$TEMP_OUTPUT"' EXIT

if [[ ! -r "$FONT_FILE" ]]; then
    echo "Font not readable: $FONT_FILE" >&2
    exit 1
fi

"$LV_FONT_CONV" \
    --font "$FONT_FILE" \
    --size 16 \
    --bpp 4 \
    --format lvgl \
    --no-compress \
    --output "$TEMP_OUTPUT" \
    --lv-include 'lvgl.h' \
    --lv-font-name keyboard_layout_font \
    -r 0x0370-0x03FF \
    -r 0x0400-0x04FF \
    -r 0x0600-0x06FF \
    -r 0xFE70-0xFEFF

{
    printf '%s\n' \
        '// SPDX-License-Identifier: Bitstream-Vera' \
        '// Generated from DejaVu Sans. Copyright (c) 2003 Bitstream, Inc.' \
        '// DejaVu changes are in the public domain.' \
        '// Full font notice: LICENSES/DejaVu-Fonts.txt'
    sed "s|--output $TEMP_OUTPUT|--output $OUTPUT|" "$TEMP_OUTPUT"
} > "$OUTPUT"

# Keep generated diffs clean while preserving exactly one final newline.
perl -0pi -e 's/\n+\z/\n/' "$OUTPUT"

echo "Generated $OUTPUT"
