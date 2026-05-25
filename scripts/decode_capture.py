#!/usr/bin/env python3
"""Decode T-Deck framebuffer capture from serial hex dump to image."""
import os, re, sys, time, struct, subprocess
from PIL import Image

PI_HOST = "hermes-pi"
T_DECK_PORT = "/dev/ttyACM0"
BAUD = 115200

def decode_capture(lines):
    width = height = stride = None
    pixel_data = bytearray()
    for line in lines:
        line = line.strip()
        m = re.match(r'\[capture\]\s+W=(\d+)\s+H=(\d+)\s+S=(\d+)', line)
        if m:
            width, height, stride = int(m.group(1)), int(m.group(2)), int(m.group(3))
            continue
        m = re.match(r'\[cdata\]\s+([\da-fA-F\s]+)', line)
        if m:
            pixel_data.extend(bytes.fromhex(m.group(1).strip()))
            continue
        if '[capture] END' in line:
            break
    if not width:
        print("ERROR: No capture header found", file=sys.stderr)
        return None
    expected = stride * height if stride else width * height * 2
    if len(pixel_data) < expected:
        pixel_data.extend(b'\x00' * (expected - len(pixel_data)))
    pixel_data = pixel_data[:expected]
    img = Image.new('RGB', (width, height))
    pixels = img.load()
    for y in range(height):
        row_start = y * (stride if stride else width * 2)
        for x in range(width):
            offset = row_start + x * 2
            if offset + 1 < len(pixel_data):
                rgb565 = struct.unpack_from('<H', pixel_data, offset)[0]
                r = ((rgb565 >> 11) & 0x1F) << 3
                g = ((rgb565 >> 5) & 0x3F) << 2
                b = (rgb565 & 0x1F) << 3
                pixels[x, y] = (r, g, b)
    return img

def capture_via_pi(output_path=None):
    print(f"Connecting to {PI_HOST}...", file=sys.stderr)
    reader_py = f"""
import serial, time
s = serial.Serial('{T_DECK_PORT}', {BAUD}, timeout=3)
time.sleep(0.5)
s.write(b'capture\\n')
time.sleep(0.2)
s.reset_input_buffer()
t0 = time.time()
while time.time() - t0 < 10:
    try:
        line = s.readline().decode('utf-8', errors='replace').strip()
        if line:
            print(line, flush=True)
            if '[capture] END' in line:
                break
    except:
        break
s.close()
"""
    ssh_cmd = [
        "ssh", PI_HOST,
        "source ~/hermes-venv/bin/activate 2>/dev/null; python3 -c " + repr(reader_py.strip())
    ]
    result = subprocess.run(' '.join(ssh_cmd), shell=True, capture_output=True, text=True, timeout=30)
    if result.returncode != 0:
        print(f"SSH error: {result.stderr}", file=sys.stderr)
        return None
    img = decode_capture(result.stdout.strip().split('\n'))
    if img:
        path = output_path or f"tdeck_capture_{time.strftime('%Y%m%d_%H%M%S')}.png"
        img.save(path)
        print(f"Saved {path} ({img.size[0]}x{img.size[1]})", file=sys.stderr)
    else:
        with open('/tmp/capture_raw.txt', 'w') as f:
            f.write(result.stdout)
        print("Raw output saved to /tmp/capture_raw.txt", file=sys.stderr)
    return img

def decode_from_file(filepath, output_path=None):
    with open(filepath) as f:
        img = decode_capture(f.readlines())
    if img:
        out = output_path or filepath.replace('.txt', '.png')
        img.save(out)
        print(f"Saved {out} ({img.size[0]}x{img.size[1]})", file=sys.stderr)
    return img

if __name__ == '__main__':
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
        out = sys.argv[2] if len(sys.argv) > 2 else None
        decode_from_file(sys.argv[1], out)
    else:
        capture_via_pi(sys.argv[1] if len(sys.argv) > 1 else None)
