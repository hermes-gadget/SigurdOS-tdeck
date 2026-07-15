#!/usr/bin/env python3
"""Decode T-Deck framebuffer capture from serial hex dump to image."""
import os, re, sys, time, struct, subprocess, tempfile
from PIL import Image

PI_HOST = "hermes-pi"
T_DECK_PORT = "/dev/ttyACM0"
BAUD = 115200

PI_CAPTURE_SCRIPT = r"""import serial, time
s=serial.Serial("/dev/ttyACM0",115200,timeout=3)
time.sleep(0.5)
s.write(b"capture\n")
time.sleep(0.3)
s.reset_input_buffer()
t0=time.time()
while time.time()-t0<15:
  try:
    l=s.readline().decode("utf-8",errors="replace").strip()
    if l: print(l,flush=True)
    if "[capture] END" in l: break
  except: break
s.close()
"""

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
            hex_str = m.group(1).strip()
            if hex_str:
                pixel_data.extend(bytes.fromhex(hex_str))
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
    """SSH to Pi, send capture cmd, pipe output back, decode."""
    print(f"Capturing from {PI_HOST}...", file=sys.stderr)
    # Write capture script to pi temp file to avoid shell quoting issues
    pi_remote_path = "/tmp/tdeck_capture.py"
    ssh_write = subprocess.run(
        ["ssh", PI_HOST, f"cat > {pi_remote_path} << 'ENDOFSCRIPT'\n{PI_CAPTURE_SCRIPT}ENDOFSCRIPT"],
        capture_output=True, text=True, timeout=10
    )
    if ssh_write.returncode != 0:
        print(f"Script write error: {ssh_write.stderr}", file=sys.stderr)
        return None
    # Run the capture script on the Pi
    result = subprocess.run(
        ["ssh", PI_HOST, f"python3 {pi_remote_path}"],
        capture_output=True, text=True, timeout=25
    )
    if result.returncode != 0:
        print(f"Capture error: {result.stderr}", file=sys.stderr)
        # Still try to decode partial output
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
        print(f"Saved {out} ({img.size[0]}x{img.size[1]})")
    return img

if __name__ == '__main__':
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
        out = sys.argv[2] if len(sys.argv) > 2 else None
        decode_from_file(sys.argv[1], out)
    else:
        capture_via_pi(sys.argv[1] if len(sys.argv) > 1 else None)
