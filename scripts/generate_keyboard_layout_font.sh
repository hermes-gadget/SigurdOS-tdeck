#!/usr/bin/env bash
set -euo pipefail
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ben

# Generate the non-Latin fallback used by the physical keyboard layouts.
# DejaVu Sans is distributed under the Bitstream Vera font license; the notice
# is retained in LICENSES/DejaVu-Fonts.txt.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FONT_URL="https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-sans-ttf-2.37.zip"
FONT_SHA256="5c6e497a2f36552cb5ffb112c413a6af39c0f3c47653662b90b4fa6499822fd7"
FONT_WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$FONT_WORK_DIR"' EXIT
FONT_ARCHIVE="$FONT_WORK_DIR/dejavu-sans-ttf-2.37.zip"
FONT_FILE="$FONT_WORK_DIR/DejaVuSans.ttf"
OUTPUT_DIR="${FONT_OUTPUT_DIR:-$SCRIPT_DIR/../src/fonts}"
OUTPUT="$OUTPUT_DIR/keyboard_layout_font.c"
TEMP_OUTPUT="$FONT_WORK_DIR/keyboard_layout_font.c"
LV_FONT_CONV="${LV_FONT_CONV:-$SCRIPT_DIR/font-tools/node_modules/.bin/lv_font_conv}"

mkdir -p "$OUTPUT_DIR"
curl --fail --location --silent --show-error "$FONT_URL" -o "$FONT_ARCHIVE"
echo "$FONT_SHA256  $FONT_ARCHIVE" | sha256sum --check
unzip -p "$FONT_ARCHIVE" '*/ttf/DejaVuSans.ttf' > "$FONT_FILE"

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
    sed \
        -e "s|--font $FONT_FILE|--font DejaVuSans.ttf|" \
        -e "s|--output $TEMP_OUTPUT|--output keyboard_layout_font.c|" \
        "$TEMP_OUTPUT"
} > "$OUTPUT"

# Keep generated diffs clean while preserving exactly one final newline.
perl -0pi -e 's/\n+\z/\n/' "$OUTPUT"

echo "Generated $OUTPUT"
