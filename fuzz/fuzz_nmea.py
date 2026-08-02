#!/usr/bin/env python3
"""GPS NMEA sentence fuzzer — generates random/malformed NMEA sentences and feeds them
to the native_sanitize GPS test binary. Any crash/ASan/UBSan hit is reported."""

import subprocess, random, sys, os, time

REPO = "/home/ben/SigurdOS-tdeck"
PROGRAM = f"{REPO}/.pio/build/native_sanitize/program"

VALID_TALKERS = ["GP", "GL", "GA", "GB", "BD", "GN"]
VALID_SENTENCES = ["GGA", "RMC", "GSA", "GSV", "GLL", "VTG", "ZDA"]
CHARSET = [ord(c) for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,-*$ \r\n\x00\xff"]

def checksum(s: str) -> str:
    """NMEA checksum: XOR of chars between $ and *"""
    c = 0
    for ch in s:
        c ^= ord(ch)
    return f"{c:02X}"

def generate_nmea() -> bytes:
    """Generate a random/mutated NMEA sentence"""
    tactic = random.choice(["valid", "malformed", "overflow", "binary", "fuzz"])

    if tactic == "valid":
        talker = random.choice(VALID_TALKERS)
        sentence = random.choice(VALID_SENTENCES)
        fields = [f"{random.uniform(-90,90):.4f}" for _ in range(random.randint(1, 12))]
        body = f"{talker}{sentence}," + ",".join(fields)
        nmea = f"${body}*{checksum(body)}\r\n"
        return nmea.encode()

    elif tactic == "malformed":
        # Valid-ish structure with bad data
        talker = random.choice(VALID_TALKERS + ["XX", "??", "ZZ"])
        sentence = random.choice(VALID_SENTENCES + ["XXX", "", "?????"])
        fields = []
        for _ in range(random.randint(0, 20)):
            t = random.choice(["float", "int", "empty", "long_str", "binary_chunk"])
            if t == "float":
                fields.append(f"{random.uniform(-999, 999):.6f}")
            elif t == "int":
                fields.append(str(random.randint(-999999, 999999)))
            elif t == "empty":
                fields.append("")
            elif t == "long_str":
                fields.append("A" * random.randint(1, 256))
            elif t == "binary_chunk":
                fields.append("".join(chr(random.choice(CHARSET)) for _ in range(random.randint(1, 64))))
        body = f"{talker}{sentence}," + ",".join(fields)
        # Sometimes omit $ or * or use bad checksum
        prefix = random.choice(["$", "", "!", "@"])
        suffix = random.choice([f"*{checksum(body)}\r\n", f"*XX\r\n", "*\r\n", "\r\n", "\n", "", "*" + "F" * random.randint(0, 10)])
        return (prefix + body + suffix).encode()

    elif tactic == "overflow":
        # Very long fields, buffer overflow attempts
        length = random.randint(128, 4096)
        return b"A" * length + b"\r\n"

    elif tactic == "binary":
        # Raw binary garbage
        length = random.randint(1, 8192)
        return bytes(random.randint(0, 255) for _ in range(length))

    elif tactic == "fuzz":
        # Mix of valid NMEA and random bytes
        parts = []
        for _ in range(random.randint(1, 20)):
            if random.random() < 0.3:
                talker = random.choice(VALID_TALKERS)
                sentence = random.choice(VALID_SENTENCES)
                body = f"{talker}{sentence}," + ",".join("0" for _ in range(random.randint(1, 8)))
                parts.append(f"${body}*{checksum(body)}\r\n".encode())
            else:
                parts.append(bytes(random.randint(0, 255) for _ in range(random.randint(1, 128))))
        return b"".join(parts)

    return b""


def run_one(input_data: bytes) -> bool:
    """Run the GPS test with given input. Returns True if no crash."""
    try:
        # Use a specific GPS test that parses NMEA
        result = subprocess.run(
            [PROGRAM, "--gtest_filter=GPS*"],
            input=input_data,
            capture_output=True,
            timeout=10,
            cwd=REPO,
        )
        if result.returncode != 0 and result.returncode != 1:  # 1 = test failure, not crash
            stderr = result.stderr.decode(errors="replace")
            if "Sanitizer" in stderr or "SEGV" in stderr or "ABRT" in stderr or "assertion" in stderr.lower():
                print(f"\n!!! CRASH DETECTED (exit={result.returncode}) !!!")
                print(f"Input size: {len(input_data)} bytes")
                print(f"First 200 bytes: {input_data[:200]}")
                print(f"Stderr: {stderr[:2000]}")
                # Save crashing input
                crash_file = f"/tmp/nmea_crash_{int(time.time())}_{os.getpid()}.bin"
                with open(crash_file, "wb") as f:
                    f.write(input_data)
                print(f"Saved crashing input to {crash_file}")
                return False
        return True
    except subprocess.TimeoutExpired:
        print(f"\n!!! TIMEOUT (infinite loop?) !!!")
        print(f"Input size: {len(input_data)} bytes")
        return False
    except Exception as e:
        print(f"\n!!! EXCEPTION: {e}")
        return False


def main():
    print("=== SigurdOS GPS NMEA Fuzzer ===")
    print(f"Target: {PROGRAM}")
    print(f"Filter: --gtest_filter=GPS*")

    # Check the binary has GPS tests
    result = subprocess.run(
        [PROGRAM, "--gtest_list_tests", "--gtest_filter=GPS*"],
        capture_output=True, text=True, timeout=10, cwd=REPO
    )
    tests = result.stdout.strip()
    if not tests:
        print("WARNING: No GPS tests found in current binary. The sanitizer build may have rebuilt for a different target.")
        print("This fuzzer works best when the GPS test binary is built. Running anyway...")

    iterations = 0
    crashes = 0
    start = time.time()

    try:
        while True:
            iterations += 1
            data = generate_nmea()
            if not run_one(data):
                crashes += 1

            if iterations % 100 == 0:
                elapsed = time.time() - start
                rate = iterations / elapsed if elapsed > 0 else 0
                print(f"  [{iterations} iters, {crashes} crashes, {elapsed:.0f}s, {rate:.0f} iters/s]")

            if crashes >= 10:
                print(f"\nReached {crashes} crashes — stopping.")
                break

    except KeyboardInterrupt:
        pass

    elapsed = time.time() - start
    print(f"\n=== DONE: {iterations} iterations, {crashes} crashes in {elapsed:.0f}s ===")


if __name__ == "__main__":
    main()
