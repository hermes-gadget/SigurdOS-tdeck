#!/usr/bin/env bash
set -euo pipefail
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2025 Ben
#
# This script downloads Noto Emoji font (SIL OFL) and generates
# an LVGL-compatible C source with a subset of common emoji.
# Requires: curl and `npm ci --prefix scripts/font-tools`.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FONT_URL="https://raw.githubusercontent.com/google/fonts/b979dba422e445492b0eb9951ac52ee0b4d648c3/ofl/notoemoji/NotoEmoji%5Bwght%5D.ttf"
FONT_SHA256="de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551"
FONT_WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$FONT_WORK_DIR"' EXIT
FONT_TMP="$FONT_WORK_DIR/NotoEmoji.ttf"
OUTPUT_DIR="${FONT_OUTPUT_DIR:-$SCRIPT_DIR/../src/fonts}"
OUTPUT_C="${OUTPUT_DIR}/emoji_font.c"
LV_FONT_CONV="${LV_FONT_CONV:-$SCRIPT_DIR/font-tools/node_modules/.bin/lv_font_conv}"
FONT_SIZE=16
BPP=4
# color emoji fonts (CBDT/CBLC) instead of converting to pure grayscale.
# Combined with 8bpp, this gives 256-color output that looks much closer
# to the original emoji.

# Comprehensive emoji subset (200+ codepoints covering common chat emoji)
# Organized into groups for readability but all combined in one lv_font_conv call

# === Faces & Smileys (U+1F600-U+1F644, plus misc) ===
RANGES="-r 0x1F600-0x1F644"   # 😀😁😂🤣😃😄😅😆😉😊😋😌😍😎😏

# Additional faces
RANGES+=" -r 0x1F913"         # 🤓 nerd face
RANGES+=" -r 0x1F914"         # 🤔
RANGES+=" -r 0x1F923"         # 🤣 rolling on the floor laughing
RANGES+=" -r 0x1F927"         # 🤗
RANGES+=" -r 0x1F970"         # 🥰
RANGES+=" -r 0x1F975"         # 🥵
RANGES+=" -r 0x1F976"         # 🥶
RANGES+=" -r 0x1F978"         # 🥸
RANGES+=" -r 0x1F97A"         # 🥺
RANGES+=" -r 0x1F920"         # 🤠
RANGES+=" -r 0x1F921"         # 🤡
RANGES+=" -r 0x1F929"         # 🤩
RANGES+=" -r 0x1F92A"         # 🤪
RANGES+=" -r 0x1F92B"         # 🤫
RANGES+=" -r 0x1F92C"         # 🤬
RANGES+=" -r 0x1F92D"         # 🤭
RANGES+=" -r 0x1F92E"         # 🤮
RANGES+=" -r 0x1F92F"         # 🤯
RANGES+=" -r 0x1F9D0"         # 🧐 face with monocle
RANGES+=" -r 0x1F47B"         # 👻
RANGES+=" -r 0x1F480"         # 💀
RANGES+=" -r 0x1F47D"         # 👽
RANGES+=" -r 0x1F525"         # 🔥 (also below, but keeping here too for faces context)

# === Hands & Gestures ===
RANGES+=" -r 0x1F44A-0x1F450" # 👊👋👌👍👎👏 range
RANGES+=" -r 0x1F64C"         # 🙌
RANGES+=" -r 0x1F64F"         # 🙏
RANGES+=" -r 0x270A-0x270C"   # ✊✋✌️
RANGES+=" -r 0x270D"          # ✍
RANGES+=" -r 0x1F4AA"         # 💪
RANGES+=" -r 0x1F91D"         # 🤝
RANGES+=" -r 0x1F91E"         # 🤞
RANGES+=" -r 0x1F596"         # 🖖
RANGES+=" -r 0x1F918"         # 🤘
RANGES+=" -r 0x1F919"         # 🤙
RANGES+=" -r 0x1F90C"         # 🤌
RANGES+=" -r 0x1F90F"         # 🤏
RANGES+=" -r 0x1F485"         # 💅
RANGES+=" -r 0x1F933-0x1F939" # 🤳🤴🤵🤶🤷🤸🤹🤺🤻 - includes some selfie/people actions
RANGES+=" -r 0x1F485"         # 💅

# === Hearts & Love ===
RANGES+=" -r 0x2764"          # ❤
RANGES+=" -r 0x1F9E1"         # 🧡
RANGES+=" -r 0x1F49B"         # 💛
RANGES+=" -r 0x1F49A"         # 💚
RANGES+=" -r 0x1F499"         # 💙
RANGES+=" -r 0x1F49C"         # 💜
RANGES+=" -r 0x1F5A4"         # 🖤
RANGES+=" -r 0x1F495"         # 💕
RANGES+=" -r 0x1F49E"         # 💞
RANGES+=" -r 0x1F493"         # 💓
RANGES+=" -r 0x1F497"         # 💗
RANGES+=" -r 0x1F498"         # 💘
RANGES+=" -r 0x1F496"         # 💖
RANGES+=" -r 0x1F49D"         # 💝
RANGES+=" -r 0x2665"          # ♥
RANGES+=" -r 0x1F494"         # 💔
RANGES+=" -r 0x1F90E"         # 🤎
RANGES+=" -r 0x1F90D"         # 🤍

# === Reactions & Celebrations ===
RANGES+=" -r 0x1F389"         # 🎉
RANGES+=" -r 0x1F38A"         # 🎊
RANGES+=" -r 0x1F388"         # 🎈
RANGES+=" -r 0x1F381"         # 🎁
RANGES+=" -r 0x2705"          # ✅
RANGES+=" -r 0x274C"          # ❌
RANGES+=" -r 0x274E"          # ❎
RANGES+=" -r 0x1F4AF"         # 💯
RANGES+=" -r 0x2B50"          # ⭐
RANGES+=" -r 0x1F31F"         # 🌟
RANGES+=" -r 0x2728"          # ✨
RANGES+=" -r 0x1F680"         # 🚀
RANGES+=" -r 0x1F525"         # 🔥
RANGES+=" -r 0x1F4A1"         # 💡
RANGES+=" -r 0x1F514"         # 🔔
RANGES+=" -r 0x1F3AF"         # 🎯
RANGES+=" -r 0x1F50B"         # 🔋
RANGES+=" -r 0x2699"          # ⚙
RANGES+=" -r 0x1F4E1"         # 📡
RANGES+=" -r 0x1F30D"         # 🌍
RANGES+=" -r 0x1F30E"         # 🌎
RANGES+=" -r 0x1F30F"         # 🌏
RANGES+=" -r 0x1F4AA"         # 💪
RANGES+=" -r 0x1F4B0"         # 💰
RANGES+=" -r 0x1F51D"         # 🔝
RANGES+=" -r 0x1F504"         # 🔄
RANGES+=" -r 0x1F3C6"         # 🏆
RANGES+=" -r 0x1F3C5"         # 🏅
RANGES+=" -r 0x1F396"         # 🎖
RANGES+=" -r 0x1F451"         # 👑
RANGES+=" -r 0x1F44D"         # 👍
RANGES+=" -r 0x1F44E"         # 👎
RANGES+=" -r 0x1F44C"         # 👌

# === Objects & Tech ===
RANGES+=" -r 0x1F4F1"         # 📱
RANGES+=" -r 0x1F4BB"         # 💻
RANGES+=" -r 0x1F5A5"         # 🖥
RANGES+=" -r 0x231A"          # ⌚
RANGES+=" -r 0x1F4F7"         # 📷
RANGES+=" -r 0x1F4F8"         # 📸
RANGES+=" -r 0x1F3AC"         # 🎬
RANGES+=" -r 0x1F3A5"         # 🎥
RANGES+=" -r 0x1F50A"         # 🔊
RANGES+=" -r 0x1F507"         # 🔇
RANGES+=" -r 0x1F50D"         # 🔍
RANGES+=" -r 0x1F50E"         # 🔎
RANGES+=" -r 0x1F511"         # 🔑
RANGES+=" -r 0x1F512"         # 🔒
RANGES+=" -r 0x1F513"         # 🔓
RANGES+=" -r 0x1F6E0"         # 🛠
RANGES+=" -r 0x1F527"         # 🔧
RANGES+=" -r 0x1F528"         # 🔨
RANGES+=" -r 0x1F529"         # 🔩
RANGES+=" -r 0x1F4D6"         # 📖
RANGES+=" -r 0x1F4DA"         # 📚
RANGES+=" -r 0x270F"          # ✏
RANGES+=" -r 0x1F58B"         # 🖋
RANGES+=" -r 0x1F4C5"         # 📅
RANGES+=" -r 0x1F4C8"         # 📈
RANGES+=" -r 0x1F4C9"         # 📉
RANGES+=" -r 0x1F4CA"         # 📊
RANGES+=" -r 0x1F4E2"         # 📢
RANGES+=" -r 0x1F4E3"         # 📣
RANGES+=" -r 0x1F3B5"         # 🎵
RANGES+=" -r 0x1F3B6"         # 🎶
RANGES+=" -r 0x1F3A7"         # 🎧
RANGES+=" -r 0x1F3B8"         # 🎸
RANGES+=" -r 0x1F3BA"         # 🎺
RANGES+=" -r 0x1F3B9"         # 🎹
RANGES+=" -r 0x1F3BB"         # 🎻
RANGES+=" -r 0x1F941"         # 🥁

# === Symbols ===
RANGES+=" -r 0x2714"          # ✔
RANGES+=" -r 0x2716"          # ✖
RANGES+=" -r 0x2795"          # ➕
RANGES+=" -r 0x2796"          # ➖
RANGES+=" -r 0x2797"          # ➗
RANGES+=" -r 0x2753"          # ❓
RANGES+=" -r 0x2754"          # ❔
RANGES+=" -r 0x2757"          # ❗
RANGES+=" -r 0x2755"          # ❕
RANGES+=" -r 0x203C"          # ‼
RANGES+=" -r 0x2049"          # ⁉
RANGES+=" -r 0x2139"          # ℹ
RANGES+=" -r 0x23F0"          # ⏰
RANGES+=" -r 0x23F3"          # ⏳
RANGES+=" -r 0x26A0"          # ⚠
RANGES+=" -r 0x26A1"          # ⚡
RANGES+=" -r 0x26D4"          # 🚫
RANGES+=" -r 0x267B"          # ♻
RANGES+=" -r 0x1F6AB"         # 🚫
RANGES+=" -r 0x1F6A9"         # 🚩
RANGES+=" -r 0x1F3C1"         # 🏁
RANGES+=" -r 0x1F6A8"         # 🚨

# === Nature & Weather ===
RANGES+=" -r 0x2600"          # ☀
RANGES+=" -r 0x2601"          # ☁
RANGES+=" -r 0x2614"          # ☔
RANGES+=" -r 0x26C5"          # ⛅
RANGES+=" -r 0x2744"          # ❄
RANGES+=" -r 0x1F308"         # 🌈
RANGES+=" -r 0x1F319"         # 🌙
RANGES+=" -r 0x1F31A"         # 🌚
RANGES+=" -r 0x1F31B"         # 🌛
RANGES+=" -r 0x1F31C"         # 🌜
RANGES+=" -r 0x1F31D"         # 🌝
RANGES+=" -r 0x1F31E"         # 🌞
RANGES+=" -r 0x1F30C"         # 🌌
RANGES+=" -r 0x1F33A"         # 🌺
RANGES+=" -r 0x1F33B"         # 🌻
RANGES+=" -r 0x1F339"         # 🌹
RANGES+=" -r 0x1F33C"         # 🌼
RANGES+=" -r 0x1F337"         # 🌷
RANGES+=" -r 0x1F33E"         # 🌾
RANGES+=" -r 0x1F33F"         # 🌿
RANGES+=" -r 0x1F340"         # 🍀
RANGES+=" -r 0x1F341"         # 🍁
RANGES+=" -r 0x1F342"         # 🍂
RANGES+=" -r 0x1F343"         # 🍃
RANGES+=" -r 0x2602"          # ☂
RANGES+=" -r 0x2603"          # ☃
RANGES+=" -r 0x26C4"          # ⛄
RANGES+=" -r 0x26F0"          # ⛰
RANGES+=" -r 0x26F1"          # ⛱
RANGES+=" -r 0x1F30B"         # 🌋
RANGES+=" -r 0x1F30A"         # 🌊

# === Food & Drink ===
RANGES+=" -r 0x2615"          # ☕
RANGES+=" -r 0x1F37A"         # 🍺
RANGES+=" -r 0x1F37B"         # 🍻
RANGES+=" -r 0x1F37E"         # 🍾
RANGES+=" -r 0x1F377"         # 🍷
RANGES+=" -r 0x1F378"         # 🍸
RANGES+=" -r 0x1F379"         # 🍹
RANGES+=" -r 0x1F37C"         # 🍼
RANGES+=" -r 0x1F354"         # 🍔
RANGES+=" -r 0x1F355"         # 🍕
RANGES+=" -r 0x1F356"         # 🍖
RANGES+=" -r 0x1F357"         # 🍗
RANGES+=" -r 0x1F32D"         # 🌭
RANGES+=" -r 0x1F32E"         # 🌮
RANGES+=" -r 0x1F32F"         # 🌯
RANGES+=" -r 0x1F35F"         # 🍟
RANGES+=" -r 0x1F363"         # 🍣
RANGES+=" -r 0x1F364"         # 🍤
RANGES+=" -r 0x1F345"         # 🍅
RANGES+=" -r 0x1F346"         # 🍆
RANGES+=" -r 0x1F34C"         # 🍌
RANGES+=" -r 0x1F34E"         # 🍎
RANGES+=" -r 0x1F34F"         # 🍏
RANGES+=" -r 0x1F351"         # 🍑
RANGES+=" -r 0x1F352"         # 🍒
RANGES+=" -r 0x1F353"         # 🍓
RANGES+=" -r 0x1F36B"         # 🍫
RANGES+=" -r 0x1F36D"         # 🍭
RANGES+=" -r 0x1F36E"         # 🍮
RANGES+=" -r 0x1F36F"         # 🍯
RANGES+=" -r 0x1F370"         # 🍰
RANGES+=" -r 0x1F371"         # 🍱
RANGES+=" -r 0x1F372"         # 🍲
RANGES+=" -r 0x1F373"         # 🍳
RANGES+=" -r 0x1F381"         # 🎁
RANGES+=" -r 0x1F382"         # 🎂

# === Animals ===
RANGES+=" -r 0x1F436"         # 🐶
RANGES+=" -r 0x1F431"         # 🐱
RANGES+=" -r 0x1F42D"         # 🐭
RANGES+=" -r 0x1F439"         # 🐹
RANGES+=" -r 0x1F430"         # 🐰
RANGES+=" -r 0x1F98A"         # 🦊
RANGES+=" -r 0x1F43B"         # 🐻
RANGES+=" -r 0x1F43C"         # 🐼
RANGES+=" -r 0x1F438"         # 🐸
RANGES+=" -r 0x1F412"         # 🐒
RANGES+=" -r 0x1F414"         # 🐔
RANGES+=" -r 0x1F427"         # 🐧
RANGES+=" -r 0x1F426"         # 🐦
RANGES+=" -r 0x1F425"         # 🐥
RANGES+=" -r 0x1F98C"         # 🦌
RANGES+=" -r 0x1F99D"         # 🦝
RANGES+=" -r 0x1F40E"         # 🐎
RANGES+=" -r 0x1F434"         # 🐴
RANGES+=" -r 0x1F984"         # 🦄
RANGES+=" -r 0x1F99E"         # 🦞
RANGES+=" -r 0x1F9AC"         # 🦬
RANGES+=" -r 0x1F40D"         # 🐍
RANGES+=" -r 0x1F422"         # 🐢
RANGES+=" -r 0x1F420"         # 🐠
RANGES+=" -r 0x1F41F"         # 🐟
RANGES+=" -r 0x1F433"         # 🐳
RANGES+=" -r 0x1F42B"         # 🐫
RANGES+=" -r 0x1F981"         # 🦁
RANGES+=" -r 0x1F99B"         # 🦛

# === Transport ===
RANGES+=" -r 0x1F697"         # 🚗
RANGES+=" -r 0x1F698"         # 🚘
RANGES+=" -r 0x1F695"         # 🚕
RANGES+=" -r 0x1F699"         # 🚙
RANGES+=" -r 0x1F68C"         # 🚌
RANGES+=" -r 0x1F68E"         # 🚎
RANGES+=" -r 0x1F693"         # 🚓
RANGES+=" -r 0x1F692"         # 🚒
RANGES+=" -r 0x1F691"         # 🚑
RANGES+=" -r 0x1F690"         # 🚐
RANGES+=" -r 0x1F6B2"         # 🚲
RANGES+=" -r 0x1F6F4"         # 🛴
RANGES+=" -r 0x1F6F5"         # 🛵
RANGES+=" -r 0x1F3CD"         # 🏍
RANGES+=" -r 0x2708"          # ✈
RANGES+=" -r 0x1F681"         # 🚁
RANGES+=" -r 0x1F6F8"         # 🛸
RANGES+=" -r 0x1F680"         # 🚀
RANGES+=" -r 0x1F4BA"         # 💺
RANGES+=" -r 0x26F5"          # ⛵
RANGES+=" -r 0x1F6A2"         # 🚢
RANGES+=" -r 0x1F6E5"         # 🛥
RANGES+=" -r 0x1F3A2"         # 🎢
RANGES+=" -r 0x1F3A0"         # 🎠

# === Gaming & Activities ===
RANGES+=" -r 0x26BD"          # ⚽
RANGES+=" -r 0x26BE"          # ⚾
RANGES+=" -r 0x1F3C0"         # 🏀
RANGES+=" -r 0x1F3C8"         # 🏈
RANGES+=" -r 0x1F3BE"         # 🎾
RANGES+=" -r 0x1F3B1"         # 🎱
RANGES+=" -r 0x1F3AE"         # 🎮
RANGES+=" -r 0x1F3B0"         # 🎰
RANGES+=" -r 0x1F579"         # 🕹
RANGES+=" -r 0x1F47E"         # 👾
RANGES+=" -r 0x1F3B2"         # 🎲
RANGES+=" -r 0x265F"          # ♟
RANGES+=" -r 0x1F3A3"         # 🎣
RANGES+=" -r 0x26F3"          # ⛳
RANGES+=" -r 0x1F3C4"         # 🏄
RANGES+=" -r 0x1F3CA"         # 🏊
RANGES+=" -r 0x1F6B4"         # 🚴
RANGES+=" -r 0x1F3C7"         # 🏇
RANGES+=" -r 0x1F3C2"         # 🏂
RANGES+=" -r 0x1F3CC"         # 🏌
RANGES+=" -r 0x1F3CB"         # 🏋

echo "Downloading Noto Emoji..."
mkdir -p "$OUTPUT_DIR"
curl --fail --location --silent --show-error "$FONT_URL" -o "$FONT_TMP"
echo "$FONT_SHA256  $FONT_TMP" | sha256sum --check
echo "Generating emoji font (size=${FONT_SIZE}, bpp=${BPP}, $(echo $RANGES | grep -o '0x[0-9A-Fa-f]\+' | wc -l) codepoints)..."
"$LV_FONT_CONV" \
    --font "$FONT_TMP" \
    --size $FONT_SIZE \
    --bpp $BPP \
    --format lvgl \
    --no-compress \
    --output "$OUTPUT_C" \
    --lv-include 'lvgl.h' \
    --lv-font-name emoji_font \
    $RANGES
sed -i "s|--font $FONT_TMP|--font NotoEmoji.ttf|; s|--output $OUTPUT_C|--output emoji_font.c|" "$OUTPUT_C"

echo "Done! Generated $OUTPUT_C"
ls -lh "$OUTPUT_C"
