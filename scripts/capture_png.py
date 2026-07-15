#!/usr/bin/env python3
"""Capture screenshot from T-Deck via Pi and save as PNG."""
import struct
import sys
from PIL import Image

import subprocess

code = '''
import serial, time
ser = serial.Serial("/dev/ttyACM0", 115200, timeout=10)
time.sleep(0.3)
ser.reset_input_buffer()
ser.write(b"capture\\n")
time.sleep(0.1)
hex_data = ""
header = False
done = False
start = time.time()
while time.time() - start < 25 and not done:
    try:
        line = ser.readline().decode("utf-8", errors="replace").strip()
    except:
        continue
    if line.startswith("[capture] W="):
        header = True
    elif line.startswith("[cdata] "):
        hex_data += line[8:]
    elif line == "[capture] END":
        done = True
ser.close()
print(hex_data)
'''

cmd = ["ssh", "hermes-pi", f"python3 -c '{code}'"]

result = subprocess.run(cmd, capture_output=True, text=True, timeout=40)
hex_data = result.stdout.strip()

if len(hex_data) < 1000:
    print(f"ERROR: too little data: {len(hex_data)} chars", file=sys.stderr)
    print(f"STDERR: {result.stderr[:500]}", file=sys.stderr)
    sys.exit(1)

# Convert to PNG
raw = bytes.fromhex(hex_data)
w, h = 320, 240
expected = w * h * 2
if len(raw) < expected:
    raw += b'\x00' * (expected - len(raw))

img = Image.new('RGB', (w, h))
pixels = img.load()
for y in range(h):
    row_off = y * w * 2
    for x in range(w):
        off = row_off + x * 2
        rgb565 = struct.unpack_from('<H', raw, off)[0]
        r = ((rgb565 >> 11) & 0x1F) << 3
        g = ((rgb565 >> 5) & 0x3F) << 2
        b = (rgb565 & 0x1F) << 3
        pixels[x, y] = (r, g, b)

out = sys.argv[1] if len(sys.argv) > 1 else '/tmp/sigurdos_screen.png'
img.save(out)
print(f"Saved: {out}")
