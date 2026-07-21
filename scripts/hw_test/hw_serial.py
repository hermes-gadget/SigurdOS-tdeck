"""Persistent, recoverable serial communication for SigurdOS hardware tests."""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Generic, TypeVar

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from hw_test.hw_constants import (  # type: ignore[import-not-found]
        CRASH_MARKERS,
        ESP_CHIP,
        LOCAL_ESPTOOL_CANDIDATES,
        SCREENSHOT_TIMEOUT_S,
        SERIAL_BAUD,
        SERIAL_RECOVERY_WAIT_S,
        CommandProtocol,
    )
    from hw_test.hw_report import HeapSample, ScreenshotArtifact, utc_now  # type: ignore[import-not-found]
else:
    from .hw_constants import (
        CRASH_MARKERS,
        ESP_CHIP,
        LOCAL_ESPTOOL_CANDIDATES,
        SCREENSHOT_TIMEOUT_S,
        SERIAL_BAUD,
        SERIAL_RECOVERY_WAIT_S,
        CommandProtocol,
    )
    from .hw_report import HeapSample, ScreenshotArtifact, utc_now

ParsedT = TypeVar("ParsedT")
ResponseParser = Callable[[str], ParsedT]

STAT_RE = re.compile(
    r"\[stat\].*?\bt=(\d+).*?\bheap=(\d+)/(\d+)\s+psram=(\d+)"
    r"(?:\s+batt=(\d+)%)?",
    re.IGNORECASE,
)
CAPTURE_HEADER_RE = re.compile(r"\[capture\]\s+W=(\d+)\s+H=(\d+)\s+S=(\d+)")
BUILD_ENV_RE = re.compile(r"(?:^|\|)env=([^|\r\n]+)")
RADIO_PROFILE_RE = re.compile(r"\bprofile=(remote_radio|remote_usca_rxonly|remote_no_radio)\b")
CAPTURE_HEX_CHARS_PER_LINE = 64


class HardwareSerialError(RuntimeError):
    """Base error for serial communication failures."""


class FirmwareDetectionError(HardwareSerialError):
    """Raised when neither supported command protocol responds."""


class ScreenshotError(HardwareSerialError):
    """Raised when framebuffer capture is absent, truncated, or invalid."""


@dataclass(slots=True)
class DeviceInfo:
    protocol: CommandProtocol
    build_environment: str | None = None
    radio_available: bool | None = None
    test_controller: bool = False
    evidence: str = ""
    recovered: bool = False


@dataclass(slots=True)
class CommandResponse(Generic[ParsedT]):
    command: str
    wire_command: str
    output: str
    started_at: str
    finished_at: str
    duration_s: float
    attempts: int
    recovered: bool
    parsed: ParsedT | None = None


def parse_stat_line(line: str, *, elapsed_s: float = 0.0) -> HeapSample | None:
    """Parse one complete firmware [stat] line."""

    match = STAT_RE.search(line)
    if not match:
        return None
    return HeapSample(
        timestamp=utc_now(),
        elapsed_s=elapsed_s,
        heap_free=int(match.group(2)),
        heap_min_free=int(match.group(3)),
        psram_free=int(match.group(4)),
        battery_percent=int(match.group(5)) if match.group(5) else None,
    )


def contains_crash(text: str) -> str | None:
    """Return the first explicit crash marker present in text."""

    folded = text.casefold()
    for marker in CRASH_MARKERS:
        if marker.casefold() in folded:
            return marker
    return None


def infer_radio_availability(getrf_output: str, boot_output: str = "") -> bool | None:
    """Infer whether a remote-test image compiled and initialized the radio."""

    profile = RADIO_PROFILE_RE.search(getrf_output)
    if profile:
        return profile.group(1) != "remote_no_radio"
    folded_boot = boot_output.casefold()
    if "lora radio enabled" in folded_boot or "[mesh] radio:" in folded_boot:
        return True
    if "lora radio disabled" in folded_boot or "radio disabled" in folded_boot:
        return False
    return None


def _png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def _rgb565_to_rgb(raw: bytes, width: int, height: int, stride: int) -> bytes:
    pixels = bytearray(width * height * 3)
    out = 0
    for y in range(height):
        row = y * stride
        for x in range(width):
            offset = row + x * 2
            value = raw[offset] | (raw[offset + 1] << 8)
            pixels[out] = ((value >> 11) & 0x1F) * 255 // 31
            pixels[out + 1] = ((value >> 5) & 0x3F) * 255 // 63
            pixels[out + 2] = (value & 0x1F) * 255 // 31
            out += 3
    return bytes(pixels)


def _raw_to_rgb(raw: bytes, width: int, height: int, stride: int) -> bytes:
    if width <= 0 or height <= 0 or stride <= 0 or stride % width:
        raise ScreenshotError(f"invalid framebuffer geometry: {width}x{height} stride={stride}")
    bytes_per_pixel = stride // width
    if bytes_per_pixel == 2:
        return _rgb565_to_rgb(raw, width, height, stride)
    if bytes_per_pixel == 3:
        rows = [raw[y * stride : y * stride + width * 3] for y in range(height)]
        return b"".join(rows)
    raise ScreenshotError(f"unsupported framebuffer format: {bytes_per_pixel} bytes/pixel")


def write_rgb_png(rgb: bytes, width: int, height: int, output: Path) -> None:
    """Write an RGB PNG using only the Python standard library."""

    expected = width * height * 3
    if len(rgb) != expected:
        raise ScreenshotError(f"RGB payload is {len(rgb)} bytes; expected {expected}")
    scanlines = b"".join(
        b"\x00" + rgb[y * width * 3 : (y + 1) * width * 3] for y in range(height)
    )
    png = (
        b"\x89PNG\r\n\x1a\n"
        + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + _png_chunk(b"IDAT", zlib.compress(scanlines, level=6))
        + _png_chunk(b"IEND", b"")
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(png)


def _extract_cdata_hex(text: str, start: int, expected_bytes: int) -> str:
    """Recover fixed-size CDATA chunks around interleaved bracketed log lines."""

    expected_chars = expected_bytes * 2
    captured: list[str] = []
    index = start
    in_chunk = False
    chunk_chars = 0
    chunk_target = 0
    while index < len(text) and len(captured) < expected_chars:
        if text.startswith("[capture] END", index):
            break
        if text.startswith("[cdata]", index):
            index += len("[cdata]")
            while index < len(text) and text[index] in " \t":
                index += 1
            in_chunk = True
            chunk_chars = 0
            chunk_target = min(CAPTURE_HEX_CHARS_PER_LINE, expected_chars - len(captured))
            continue
        if not in_chunk:
            index += 1
            continue
        if chunk_chars >= chunk_target:
            in_chunk = False
            continue

        character = text[index]
        if character in "0123456789abcdefABCDEF":
            captured.append(character)
            chunk_chars += 1
            index += 1
            continue
        if character == "[":
            # Other firmware tasks can write a complete diagnostic record while
            # the controller is part-way through a CDATA line. Skip that record,
            # including hex-looking text, then resume the interrupted chunk.
            line_end = text.find("\n", index)
            if line_end < 0:
                break
            index = line_end + 1
            continue
        index += 1
    return "".join(captured)


def decode_capture_text(text: str, output: Path, *, screen: str | None = None) -> ScreenshotArtifact:
    """Decode a complete capture transcript into PNG with strict hex filtering."""

    header = CAPTURE_HEADER_RE.search(text)
    if not header:
        abort = re.search(r"\[capture\]\s+ABORT:\s*(.+)", text)
        reason = abort.group(1).strip() if abort else "capture header not found"
        raise ScreenshotError(reason)
    width, height, stride = (int(value) for value in header.groups())
    expected = stride * height
    filtered_hex = _extract_cdata_hex(text, header.end(), expected)
    if len(filtered_hex) % 2:
        filtered_hex = filtered_hex[:-1]
    if not filtered_hex:
        raise ScreenshotError("capture contained no valid CDATA hex")
    try:
        raw = bytes.fromhex(filtered_hex)
    except ValueError as exc:
        raise ScreenshotError(f"invalid CDATA hex: {exc}") from exc
    if len(raw) < expected:
        raise ScreenshotError(f"truncated framebuffer: got {len(raw)} of {expected} bytes")
    raw = raw[:expected]
    write_rgb_png(_raw_to_rgb(raw, width, height, stride), width, height, output)
    return ScreenshotArtifact(
        path=str(output),
        timestamp=utc_now(),
        width=width,
        height=height,
        stride=stride,
        raw_bytes=len(raw),
        sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
        screen=screen,
    )


class PersistentSerial:
    """One long-lived serial connection with bounded recovery.

    The class deliberately never calls setDTR(), setRTS(), or
    reset_input_buffer(). Opening is the only operation that may reset USB CDC.
    """

    def __init__(
        self,
        port: str,
        *,
        baud: int = SERIAL_BAUD,
        boot_wait_s: float = 0.0,
        read_timeout_s: float = 0.1,
        recovery_wait_s: float = SERIAL_RECOVERY_WAIT_S,
        esptool: str | None = None,
        max_retries: int = 2,
    ) -> None:
        self.port = port
        self.baud = baud
        self.boot_wait_s = boot_wait_s
        self.read_timeout_s = read_timeout_s
        self.recovery_wait_s = recovery_wait_s
        self.esptool = esptool
        self.max_retries = max_retries
        self.protocol = CommandProtocol.UNKNOWN
        self.device_info: DeviceInfo | None = None
        self._serial: Any | None = None
        self._boot_output = bytearray()

    @staticmethod
    def _serial_module() -> Any:
        try:
            import serial  # type: ignore[import-not-found]
        except ImportError as exc:
            raise HardwareSerialError(
                "pyserial is required; install it with: python3 -m pip install pyserial"
            ) from exc
        return serial

    @property
    def connected(self) -> bool:
        return self._serial is not None and bool(getattr(self._serial, "is_open", False))

    def connect(self) -> "PersistentSerial":
        if self.connected:
            return self
        serial = self._serial_module()
        last_error: Exception | None = None
        for attempt in range(1, self.max_retries + 2):
            try:
                # No DTR/RTS keyword arguments: pySerial defaults are the known
                # good state for ESP32-S3 USB CDC on the Pi.
                self._serial = serial.Serial(
                    self.port,
                    self.baud,
                    timeout=self.read_timeout_s,
                    write_timeout=5,
                )
                if self.boot_wait_s > 0:
                    self._collect_boot_output(self.boot_wait_s)
                return self
            except Exception as exc:  # pySerial exposes platform-specific subclasses
                last_error = exc
                self.close()
                if attempt <= self.max_retries:
                    time.sleep(min(1.0 * attempt, 3.0))
        raise HardwareSerialError(f"cannot open {self.port}: {last_error}")

    def close(self) -> None:
        serial_obj, self._serial = self._serial, None
        if serial_obj is not None:
            try:
                serial_obj.close()
            except Exception:
                pass

    def __enter__(self) -> "PersistentSerial":
        return self.connect()

    def __exit__(self, *_: object) -> None:
        self.close()

    def _collect_boot_output(self, duration_s: float) -> None:
        deadline = time.monotonic() + duration_s
        while time.monotonic() < deadline:
            chunk = self.read_available(read_one=True)
            if chunk:
                self._boot_output.extend(chunk)
            else:
                time.sleep(0.02)

    def read(self, size: int = 1) -> bytes:
        if not self.connected:
            self.connect()
        assert self._serial is not None
        return bytes(self._serial.read(size))

    def read_available(self, *, read_one: bool = False) -> bytes:
        if not self.connected:
            self.connect()
        assert self._serial is not None
        waiting = int(getattr(self._serial, "in_waiting", 0))
        if waiting:
            return bytes(self._serial.read(waiting))
        if read_one:
            return bytes(self._serial.read(1))
        return b""

    def drain(self, duration_s: float = 0.25) -> bytes:
        """Read pending bytes without discarding them through driver APIs."""

        output = bytearray()
        deadline = time.monotonic() + duration_s
        while time.monotonic() < deadline:
            chunk = self.read_available()
            if chunk:
                output.extend(chunk)
                deadline = time.monotonic() + min(duration_s, 0.1)
            else:
                time.sleep(0.01)
        return bytes(output)

    def write(self, data: bytes) -> None:
        if not self.connected:
            self.connect()
        assert self._serial is not None
        self._serial.write(data)
        self._serial.flush()

    def _wire_command(self, command: str) -> str:
        stripped = command.strip()
        lower = stripped.lower()
        if lower.startswith("nav "):
            target = stripped.split(maxsplit=1)[1]
            return f"NAV {target}" if self.protocol == CommandProtocol.RELEASE else f"nav {target}"
        if lower in {"capture", "screenshot"}:
            return "SCREENSHOT" if self.protocol == CommandProtocol.RELEASE else "capture"
        return stripped

    def _read_response(self, timeout_s: float, expected: tuple[bytes, ...]) -> bytes:
        output = bytearray()
        deadline = time.monotonic() + timeout_s
        last_data: float | None = None
        while time.monotonic() < deadline:
            chunk = self.read_available(read_one=True)
            if chunk:
                output.extend(chunk)
                last_data = time.monotonic()
                if expected and any(marker in output for marker in expected):
                    break
            elif output and last_data is not None and time.monotonic() - last_data >= 0.45:
                break
            else:
                time.sleep(0.01)
        return bytes(output)

    def send_command(
        self,
        command: str,
        *,
        timeout_s: float = 5.0,
        expected: tuple[str, ...] = (),
        parser: ResponseParser[ParsedT] | None = None,
        recover_on_silence: bool = True,
    ) -> CommandResponse[ParsedT]:
        """Send a command, parse its response, and recover once if silent."""

        started_iso = utc_now()
        started = time.monotonic()
        recovered = False
        attempts = 0
        wire_command = self._wire_command(command)
        output = b""
        max_attempts = 2 if recover_on_silence else 1
        for attempts in range(1, max_attempts + 1):
            self.drain(0.1)
            self.write((wire_command + "\r\n").encode("utf-8"))
            output = self._read_response(
                timeout_s,
                tuple(marker.encode("utf-8") for marker in expected),
            )
            if output or attempts == max_attempts:
                break
            self.recover_usb_cdc()
            recovered = True
        text = output.decode("utf-8", errors="replace")
        parsed = parser(text) if parser is not None else None
        return CommandResponse(
            command=command,
            wire_command=wire_command,
            output=text,
            started_at=started_iso,
            finished_at=utc_now(),
            duration_s=time.monotonic() - started,
            attempts=attempts,
            recovered=recovered,
            parsed=parsed,
        )

    def _resolve_esptool(self) -> str:
        candidates = (self.esptool,) if self.esptool else LOCAL_ESPTOOL_CANDIDATES
        for raw in candidates:
            if not raw:
                continue
            expanded = str(Path(raw).expanduser())
            if "/" in expanded and Path(expanded).is_file():
                return expanded
            resolved = shutil.which(expanded)
            if resolved:
                return resolved
        raise HardwareSerialError("esptool not found; checked configured local candidates")

    def recover_usb_cdc(self) -> None:
        """Recover a silent CDC endpoint through esptool's reset handshake."""

        self.close()
        command = [
            self._resolve_esptool(),
            "--chip",
            ESP_CHIP,
            "--port",
            self.port,
            "--before",
            "default-reset",
            "chip-id",
        ]
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=25, check=False)
        except (OSError, subprocess.TimeoutExpired) as exc:
            raise HardwareSerialError(f"USB CDC recovery failed: {exc}") from exc
        if result.returncode != 0:
            detail = (result.stdout + result.stderr).strip()[-600:]
            raise HardwareSerialError(f"esptool recovery failed: {detail}")
        time.sleep(self.recovery_wait_s)
        self.connect()

    def _probe_firmware(self) -> DeviceInfo:
        boot_text = self._boot_output.decode("utf-8", errors="replace")
        self._boot_output.clear()
        evidence: list[str] = [boot_text] if boot_text else []
        help_response = self.send_command(
            "help",
            timeout_s=3,
            recover_on_silence=False,
        )
        evidence.append(help_response.output)
        combined_initial = boot_text + help_response.output
        remote = "SigurdOS Remote Test Controller" in combined_initial or "[test]" in help_response.output
        if not remote:
            screen_response = self.send_command(
                "screen",
                timeout_s=2,
                recover_on_silence=False,
            )
            evidence.append(screen_response.output)
            remote = "[test] current screen:" in screen_response.output
        if remote:
            self.protocol = CommandProtocol.REMOTE_TEST
            build = self.send_command(
                "query build",
                timeout_s=4,
                recover_on_silence=False,
            )
            evidence.append(build.output)
            match = BUILD_ENV_RE.search(build.output)
            environment = match.group(1).strip() if match else None
            radio_response = self.send_command(
                "getrf",
                timeout_s=4,
                recover_on_silence=False,
            )
            evidence.append(radio_response.output)
            radio_available = infer_radio_availability(radio_response.output, boot_text)
            return DeviceInfo(
                protocol=self.protocol,
                build_environment=environment,
                radio_available=radio_available,
                test_controller=True,
                evidence="\n".join(item for item in evidence if item)[-4000:],
            )

        self.protocol = CommandProtocol.RELEASE
        release = self.send_command(
            "nav home",
            timeout_s=3,
            recover_on_silence=False,
        )
        evidence.append(release.output)
        if "[serial] NAV" in release.output:
            return DeviceInfo(
                protocol=self.protocol,
                radio_available=None,
                test_controller=False,
                evidence="\n".join(item for item in evidence if item)[-4000:],
            )
        if "SigurdOS T-Deck ready" in boot_text:
            return DeviceInfo(
                protocol=self.protocol,
                radio_available=None,
                test_controller=False,
                evidence="\n".join(item for item in evidence if item)[-4000:],
            )
        self.protocol = CommandProtocol.UNKNOWN
        return DeviceInfo(
            protocol=self.protocol,
            evidence="\n".join(item for item in evidence if item)[-4000:],
        )

    def detect_firmware(self, *, recover: bool = True) -> DeviceInfo:
        """Detect remote-test versus release command protocols."""

        self.connect()
        info = self._probe_firmware()
        if info.protocol == CommandProtocol.UNKNOWN:
            # Radio-enabled test builds can still be completing mesh startup at
            # the standardized 10-second wait. Probe once more on the same open
            # handle before escalating to an esptool reset.
            time.sleep(3)
            info = self._probe_firmware()
        if info.protocol == CommandProtocol.UNKNOWN and recover:
            self.recover_usb_cdc()
            info = self._probe_firmware()
            info.recovered = True
        self.device_info = info
        self.protocol = info.protocol
        return info

    def capture_screenshot(
        self,
        output: Path,
        *,
        screen: str | None = None,
        timeout_s: float = SCREENSHOT_TIMEOUT_S,
    ) -> ScreenshotArtifact:
        """Capture the framebuffer with a bulk read loop, never readline()."""

        if self.protocol == CommandProtocol.UNKNOWN:
            info = self.detect_firmware()
            if info.protocol == CommandProtocol.UNKNOWN:
                raise FirmwareDetectionError("cannot capture: firmware command protocol is unknown")
        self.drain(0.2)
        command = "SCREENSHOT" if self.protocol == CommandProtocol.RELEASE else "capture"
        self.write((command + "\r\n").encode("ascii"))
        payload = bytearray()
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            chunk = self.read_available(read_one=True)
            if chunk:
                payload.extend(chunk)
                if any(
                    marker in payload
                    for marker in (b"[capture] END", b"[capture] ABORT:", b"[capture] ERROR:")
                ):
                    break
            else:
                time.sleep(0.005)
        text = payload.decode("utf-8", errors="replace")
        if "[capture] END" not in text:
            error = re.search(r"\[capture\]\s+(?:ABORT|ERROR):\s*(.+)", text)
            reason = error.group(1).strip() if error else f"capture timed out after {timeout_s:.0f}s"
            raise ScreenshotError(reason)
        return decode_capture_text(text, output, screen=screen)

    def read_stat_lines(self, duration_s: float, *, max_samples: int | None = None) -> list[HeapSample]:
        """Collect complete [stat] lines with byte-by-byte framing."""

        samples: list[HeapSample] = []
        line = bytearray()
        started = time.monotonic()
        deadline = started + duration_s
        while time.monotonic() < deadline:
            byte = self.read(1)
            if not byte:
                continue
            if byte == b"\n":
                parsed = parse_stat_line(
                    line.decode("utf-8", errors="replace"),
                    elapsed_s=time.monotonic() - started,
                )
                line.clear()
                if parsed:
                    samples.append(parsed)
                    if max_samples is not None and len(samples) >= max_samples:
                        break
            elif len(line) < 8192:
                line.extend(byte)
            else:
                line.clear()
        return samples


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=SERIAL_BAUD)
    parser.add_argument("--boot-wait", type=float, default=0.0)
    parser.add_argument("--command")
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--stats", type=float, metavar="SECONDS")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        with PersistentSerial(args.port, baud=args.baud, boot_wait_s=args.boot_wait) as connection:
            info = connection.detect_firmware()
            print(
                f"protocol={info.protocol.value} env={info.build_environment or 'unknown'} "
                f"radio={info.radio_available} recovered={info.recovered}"
            )
            if args.command:
                response = connection.send_command(args.command)
                print(response.output, end="" if response.output.endswith("\n") else "\n")
            if args.capture:
                artifact = connection.capture_screenshot(args.capture)
                print(f"screenshot={artifact.path} sha256={artifact.sha256}")
            if args.stats:
                for sample in connection.read_stat_lines(args.stats):
                    print(sample.to_dict())
    except HardwareSerialError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
