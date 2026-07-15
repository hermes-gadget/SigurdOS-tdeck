#!/usr/bin/env python3
"""
SigurdOS T-Deck Screenshot Capture

Connects to the T-Deck over USB serial (via the Pi), sends the SCREENSHOT
command, captures hex-encoded RGB565 framebuffer data, and converts to PNG.

Usage:
  # Via Pi gateway (default)
  python3 scripts/screenshot.py --pi hermes-pi --output /tmp/screen.png

  # Direct serial (T-Deck connected locally)
  python3 scripts/screenshot.py --port /dev/ttyACM0 --output /tmp/screen.png

  # Use remote_test 'capture' command (for remote_test builds)
  python3 scripts/screenshot.py --pi hermes-pi --remote-test

Output: Writes PNG to the specified path. Also prints the path to stdout
so the agent can include MEDIA:path in Discord messages.
"""

import argparse
import struct
import sys
import time

try:
    from PIL import Image
except ImportError:
    Image = None


def hex_to_rgb565_png(hex_data, width, height, output_path):
    """Convert hex-encoded RGB565 data to a PNG file."""
    raw = bytes.fromhex(hex_data)
    expected = width * height * 2
    if len(raw) < expected:
        print(f"[screenshot] WARNING: expected {expected} bytes, got {len(raw)}", file=sys.stderr)
        # Pad with black
        raw = raw + b'\x00' * (expected - len(raw))
    elif len(raw) > expected:
        raw = raw[:expected]

    img = Image.new("RGB", (width, height))
    pixels = img.load()

    for y in range(height):
        row_offset = y * width * 2
        for x in range(width):
            offset = row_offset + x * 2
            rgb565 = struct.unpack_from('<H', raw, offset)[0]
            r = ((rgb565 >> 11) & 0x1F) << 3
            g = ((rgb565 >> 5) & 0x3F) << 2
            b = (rgb565 & 0x1F) << 3
            pixels[x, y] = (r, g, b)

    img.save(output_path)
    print(f"[screenshot] saved: {output_path}")
    return output_path


def capture_via_serial(port, baud, output_path, use_remote_test_cmd=False):
    """Connect to T-Deck serial, trigger capture, parse hex data."""
    import serial

    cmd = b"capture\n" if use_remote_test_cmd else b"SCREENSHOT\n"

    with serial.Serial(port, baud, timeout=10) as ser:
        time.sleep(0.5)
        ser.reset_input_buffer()
        ser.write(cmd)
        time.sleep(0.2)

        width = 320
        height = 240
        stride = width * 2
        hex_data = ""
        header_found = False
        data_done = False
        timeout_s = 30
        start = time.time()

        while time.time() - start < timeout_s and not data_done:
            line = ser.readline().decode('utf-8', errors='replace').strip()

            if line.startswith("[capture] W="):
                # Parse header: [capture] W=320 H=240 S=640
                parts = line.replace("[capture] ", "").split()
                for p in parts:
                    if p.startswith("W="):
                        width = int(p[2:])
                    elif p.startswith("H="):
                        height = int(p[2:])
                    elif p.startswith("S="):
                        stride = int(p[2:])
                header_found = True
                print(f"[screenshot] dimensions: {width}x{height} stride={stride}", file=sys.stderr)

            elif line.startswith("[cdata] "):
                hex_data += line[8:]  # strip [cdata] prefix

            elif line == "[capture] END":
                data_done = True

        if not header_found:
            print("[screenshot] ERROR: no capture header received", file=sys.stderr)
            return None

        if not data_done:
            print("[screenshot] ERROR: capture timed out", file=sys.stderr)
            return None

        return hex_to_rgb565_png(hex_data, width, height, output_path)


def capture_via_ssh(pi_host, output_path, use_remote_test_cmd=False):
    """Capture via SSH to the Pi which has the T-Deck attached."""
    import subprocess

    # The Pi runs esptool serial passthrough or screen
    # Simple approach: echo SCREENSHOT to the serial port
    # Using 'stty' to configure the serial port, then cat output after sending command

    cmd = f"""
    ssh -o ConnectTimeout=10 -i ~/.ssh/id_ed25519 {pi_host} bash -c '
        # Configure serial
        stty -F /dev/ttyACM0 115200 raw -echo 2>/dev/null
        # Send trigger
        echo '{"capture" if use_remote_test_cmd else "SCREENSHOT"}' > /dev/ttyACM0
        # Read output for 30 seconds
        cat /dev/ttyACM0 &
        CAT_PID=$!
        sleep 30
        kill $CAT_PID 2>/dev/null
    ' 2>/dev/null
    """

    result = subprocess.run(
        ["bash", "-c", cmd],
        capture_output=True,
        text=True,
        timeout=60
    )

    output = result.stdout
    if not output:
        print("[screenshot] ERROR: no output from Pi", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return None

    width = 320
    height = 240
    hex_data = ""
    header_found = False
    data_done = False

    for line in output.splitlines():
        line = line.strip()
        if line.startswith("[capture] W="):
            parts = line.replace("[capture] ", "").split()
            for p in parts:
                if p.startswith("W="):
                    width = int(p[2:])
                elif p.startswith("H="):
                    height = int(p[2:])
            header_found = True
        elif line.startswith("[cdata] "):
            hex_data += line[8:]
        elif line == "[capture] END":
            data_done = True

    if not header_found:
        print("[screenshot] ERROR: no capture header", file=sys.stderr)
        print("Raw output:", output[:500], file=sys.stderr)
        return None

    return hex_to_rgb565_png(hex_data, width, height, output_path)


def main():
    parser = argparse.ArgumentParser(description="Capture screenshot from SigurdOS T-Deck")
    parser.add_argument("--port", help="Serial port (e.g., /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--pi", help="Pi hostname (e.g., hermes-pi)")
    parser.add_argument("--output", "-o", default="/tmp/sigurdos_screen.png", help="Output PNG path")
    parser.add_argument("--remote-test", action="store_true",
                        help="Use 'capture' command instead of 'SCREENSHOT' (for remote_test builds)")
    args = parser.parse_args()

    if not Image:
        print("[screenshot] ERROR: Pillow not installed. Install with: pip install Pillow", file=sys.stderr)
        sys.exit(1)

    if args.port:
        path = capture_via_serial(args.port, args.baud, args.output, args.remote_test)
    elif args.pi:
        path = capture_via_ssh(args.pi, args.output, args.remote_test)
    else:
        # Try direct serial first, then Pi
        try:
            path = capture_via_serial("/dev/ttyACM0", args.baud, args.output, args.remote_test)
        except Exception:
            path = capture_via_ssh("hermes-pi", args.output, args.remote_test)

    if path:
        print(path)  # For agent consumption: MEDIA:path

if __name__ == "__main__":
    main()
