#!/usr/bin/env python3
"""
Generate color emoji images for the LVGL emoji picker.
Renders each emoji from Noto Color Emoji as a small RGBA image,
converts to LVGL raw C array format (ARGB8888).
"""

import struct
import sys
import subprocess
import tempfile
import os
from pathlib import Path

# The 52 emoji from the picker, in order
PICKER_EMOJI = [
    # Faces
    "\U0001F600", "\U0001F601", "\U0001F602", "\U0001F603",
    "\U0001F923", "\U0001F60A", "\U0001F60D", "\U0001F60E",
    "\U0001F914", "\U0001F60F", "\U0001F62E", "\U0001F622",
    "\U0001F62D", "\U0001F624", "\U0001F621", "\U0001F970",
    # Hands
    "\U0001F44D", "\U0001F44E", "\U0001F44C", "\u270C",
    "\U0001F44F", "\U0001F64C", "\U0001F64F", "\U0001F4AA",
    "\U0001F91D", "\U0001F44B",
    # Hearts
    "\u2764", "\U0001F9E1", "\U0001F49B", "\U0001F49A",
    "\U0001F499", "\U0001F49C", "\U0001F5A4", "\U0001F495",
    "\U0001F49E", "\U0001F493",
    # Objects/Symbols
    "\U0001F525", "\U0001F389", "\U0001F38A", "\u2705",
    "\u274C", "\U0001F4AF", "\u2B50", "\U0001F680",
    "\U0001F388", "\U0001F4A1", "\U0001F514", "\U0001F3AF",
    "\U0001F50B", "\u2699", "\U0001F4E1", "\U0001F30D",
]

def codepoint_from_emoji(emoji_char):
    """Get Unicode codepoint from emoji character."""
    return ord(emoji_char)

def render_emoji_png(emoji_char, font_path, size=24):
    """Render an emoji character as RGBA PNG using PIL via subprocess."""
    cp = codepoint_from_emoji(emoji_char)
    hex_cp = f"U+{cp:04X}" if cp <= 0xFFFF else f"U+{cp:06X}"

    with tempfile.NamedTemporaryFile(suffix='.png', delete=False) as tmp:
        tmp_path = tmp.name

    try:
        # Use Python with PIL to render
        code = f'''
from PIL import Image, ImageDraw, ImageFont
import struct

cp = {cp}
font_path = "{font_path}"
out_path = "{tmp_path}"

try:
    font = ImageFont.truetype(font_path, 24)
except Exception:
    # Fallback - try different approach
    font = ImageFont.truetype(font_path, 24, encoding='unic')

img = Image.new('RGBA', (24, 24), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)
draw.text((2, 2), chr(cp), font=font, fill=(255, 255, 255, 255))
img.save(out_path, 'PNG')
print(f"Rendered U+{cp:04X}")
'''
        result = subprocess.run(
            [sys.executable, '-c', code],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode != 0:
            print(f"PIL error for U+{cp:04X}: {result.stderr}", file=sys.stderr)
            return None

        with open(tmp_path, 'rb') as f:
            png_data = f.read()

        return png_data
    finally:
        try:
            os.unlink(tmp_path)
        except: pass

def render_emoji_cairo(emoji_char, font_path, size=24):
    """Render emoji using cairo/pycairo for proper color emoji."""
    cp = codepoint_from_emoji(emoji_char)

    code = f'''
import cairo
import struct

cp = {cp}
font_path = "{font_path}"
size = {size}

surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
cr = cairo.Context(surface)
cr.set_source_rgba(0, 0, 0, 0)
cr.paint()
cr.set_source_rgba(1, 1, 1, 1)
cr.select_font_face("Noto Color Emoji", cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_NORMAL)
cr.set_font_size(size - 4)
cr.text_extents(chr(cp))
cr.move_to(2, size - 4)
cr.show_text(chr(cp))

# Convert ARGB32 to RGBA raw bytes
buf = surface.get_data()
out_path = "/tmp/emoji_raw_U{cp:04X}.bin"
with open(out_path, 'wb') as f:
    f.write(bytes(buf))
print(f"Rendered U+{cp:04X} to {{out_path}}")
'''
    try:
        result = subprocess.run(
            [sys.executable, '-c', code],
            capture_output=True, text=True, timeout=30
        )
        if result.returncode != 0:
            return None
        # Read the raw data back
        out_path = f"/tmp/emoji_raw_U{cp:04X}.bin"
        if os.path.exists(out_path):
            with open(out_path, 'rb') as f:
                data = f.read()
            os.unlink(out_path)
            return data
        return None
    except:
        return None

def png_to_lvgl_rgba8888(png_data, width=24, height=24):
    """Convert PNG bytes to LVGL RGBA8888 raw image C array.
    Uses png.py or pypng to decode."""
    import struct

    # We'll use PIL if available
    code = f'''
import sys
import struct
from PIL import Image
from io import BytesIO

png_data = {repr(png_data)}
img = Image.open(BytesIO(png_data))
img = img.convert('RGBA')
img = img.resize(({width}, {height}))
pixels = list(img.getdata())
# LVGL RGBA8888 = R,G,B,A bytes
out = b''
for r, g, b, a in pixels:
    out += struct.pack('BBBB', r, g, b, a)
sys.stdout.buffer.write(out)
'''
    try:
        result = subprocess.run(
            [sys.executable, '-c', code],
            capture_output=True, timeout=30
        )
        if result.returncode == 0:
            return result.stdout
        return None
    except:
        return None


def generate_lvgl_image_c(name, rgba_data, width, height):
    """Generate C source for an LVGL image resource."""
    assert len(rgba_data) == width * height * 4

    lines = [
        f'// LVGL image: {name} ({width}x{height} RGBA8888)',
        f'static const uint8_t {name}_data[] = {{',
    ]

    # Format as hex bytes, 16 per line
    bytes_list = [rgba_data[i:i+16] for i in range(0, len(rgba_data), 16)]
    for chunk in bytes_list:
        hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
        lines.append(f'    {hex_str},')

    lines.extend([
        '};',
        '',
        f'static const lv_image_dsc_t {name} = {{',
        f'    .header = {{',
        f'        .magic = LV_IMAGE_HEADER_MAGIC,',
        f'        .cf = LV_COLOR_FORMAT_ARGB8888,',
        f'        .flags = 0,',
        f'        .w = {width},',
        f'        .h = {height},',
        f'        .stride = {width} * 4,',
        f'        .reserved_2 = 0,',
        f'    }},',
        f'    .data_size = sizeof({name}_data),',
        f'    .data = {name}_data,',
        f'    .reserved = NULL,',
        f'    .reserved_2 = NULL,',
        f'}};',
        '',
    ])

    return '\n'.join(lines)


def main():
    # Check available rendering methods
    font_paths = [
        '/tmp/NotoEmoji.ttf',  # Variable weight font with TrueType outlines
        '/usr/share/fonts/truetype/noto/NotoEmoji.ttf',
        '/usr/share/fonts/truetype/noto/NotoEmoji-VariableFont.ttf',
    ]

    font_path = None
    for fp in font_paths:
        if os.path.exists(fp):
            font_path = fp
            break

    if not font_path:
        print("Noto Emoji font not found, using /tmp/NotoEmoji.ttf")
        if not os.path.exists('/tmp/NotoEmoji.ttf'):
            print("ERROR: /tmp/NotoEmoji.ttf not found. Run generate_emoji_font.sh first or download it.")
            sys.exit(1)
        font_path = '/tmp/NotoEmoji.ttf'

    # Check for rendering libraries
    has_pil = False
    has_cairo = False

    try:
        import PIL
        has_pil = True
        print("Using PIL for rendering", file=sys.stderr)
    except ImportError:
        pass

    if not has_pil:
        try:
            import cairo
            has_cairo = True
            print("Using cairo for rendering", file=sys.stderr)
        except ImportError:
            pass

    if not has_pil and not has_cairo:
        print("ERROR: Need PIL (Pillow) or pycairo installed")
        print("pip install Pillow")
        sys.exit(1)

    SIZE = 24  # render at 24x24
    out_dir = Path(__file__).parent.parent / 'src' / 'fonts' / 'emoji_images'
    out_dir.mkdir(parents=True, exist_ok=True)

    output_lines = [
        '// Auto-generated emoji images for LVGL picker',
        '// Generated by scripts/gen_emoji_images.py',
        '#include <lvgl.h>',
        '',
    ]

    for i, emoji in enumerate(PICKER_EMOJI):
        name = f"emoji_img_{i}"
        cp = codepoint_from_emoji(emoji)

        if has_pil:
            rgba = render_with_pil(emoji, font_path, SIZE)
        else:
            rgba = render_with_cairo(emoji, font_path, SIZE)

        if rgba is None:
            print(f"Failed to render {emoji} (U+{cp:04X})", file=sys.stderr)
            # Create a placeholder
            rgba = b'\x00' * (SIZE * SIZE * 4)

        c_code = generate_lvgl_image_c(name, rgba, SIZE, SIZE)
        output_lines.append(c_code)
        print(f"  [{i+1}/{len(PICKER_EMOJI)}] {emoji} U+{cp:04X}", file=sys.stderr)

    # Write output file
    out_file = out_dir / 'emoji_picker_images.h'
    with open(out_file, 'w') as f:
        f.write('\n'.join(output_lines))
    print(f"\nWrote {out_file}", file=sys.stderr)

    # Generate index header
    idx_lines = [
        '#ifndef EMOJI_PICKER_INDEX_H',
        '#define EMOJI_PICKER_INDEX_H',
        '#include <lvgl.h>',
        '#include "emoji_picker_images.h"',
        '',
        f'static constexpr int EMOJI_PICKER_IMG_COUNT = {len(PICKER_EMOJI)};',
        '',
        '// Extern declarations for all emoji images',
    ]
    for i in range(len(PICKER_EMOJI)):
        idx_lines.append(f'extern const lv_image_dsc_t emoji_img_{i};')

    idx_lines.extend([
        '',
        '// Array of all emoji image descriptors',
        'static const lv_image_dsc_t* emoji_picker_images[] = {',
    ])
    for i in range(len(PICKER_EMOJI)):
        idx_lines.append(f'    &emoji_img_{i},')
    idx_lines.extend([
        '};',
        '',
        '#endif',
    ])

    idx_file = out_dir / 'emoji_picker_index.h'
    with open(idx_file, 'w') as f:
        f.write('\n'.join(idx_lines))
    print(f"Wrote {idx_file}", file=sys.stderr)


def render_with_pil(emoji_char, font_path, size=24):
    """Render emoji with PIL."""
    from PIL import Image, ImageDraw, ImageFont
    import io

    cp = ord(emoji_char)

    try:
        font = ImageFont.truetype(font_path, size - 2)
        img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        draw.text((2, 1), chr(cp), font=font, fill=(255, 255, 255, 255))
        pixels = list(img.getdata())

        # Convert to RGBA8888 bytes
        rgba = b''
        for r, g, b, a in pixels:
            rgba += struct.pack('BBBB', r, g, b, a)
        return rgba
    except Exception as e:
        print(f"PIL error: {e}", file=sys.stderr)
        return None


def render_with_cairo(emoji_char, font_path, size=24):
    """Render emoji with cairo."""
    import cairo
    import struct

    cp = ord(emoji_char)

    try:
        surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
        cr = cairo.Context(surface)
        cr.set_source_rgba(0, 0, 0, 0)
        cr.paint()
        cr.set_source_rgba(1, 1, 1, 1)
        cr.select_font_face("Noto Color Emoji",
                            cairo.FONT_SLANT_NORMAL,
                            cairo.FONT_WEIGHT_NORMAL)
        cr.set_font_size(size - 4)
        cr.move_to(2, size - 4)
        cr.show_text(chr(cp))

        buf = surface.get_data()
        # Cairo ARGB32 is native-endian ARGB, need to convert to RGBA
        rgba = b''
        for i in range(0, len(buf), 4):
            b, g, r, a = buf[i:i+4]  # Cairo is BGRA on little-endian
            rgba += struct.pack('BBBB', r, g, b, a)
        return rgba
    except Exception as e:
        print(f"Cairo error: {e}", file=sys.stderr)
        return None


if __name__ == '__main__':
    main()
