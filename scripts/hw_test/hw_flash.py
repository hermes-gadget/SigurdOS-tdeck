"""Build, flash, and boot-verify SigurdOS T-Deck firmware."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from hw_test.hw_constants import (  # type: ignore[import-not-found]
        DEFAULT_BUILD_ENV,
        ESP_CHIP,
        FLASH_BAUD,
        IGNORED_BOOT_MESSAGES,
        LOCAL_ESPTOOL_CANDIDATES,
        MERGED_FIRMWARE_NAME,
        PI_ESPTOOL,
        PI_HOSTS,
        PI_TDECK_PORT,
        SERIAL_BAUD,
        boot_wait_for,
        first_existing_local_port,
    )
else:
    from .hw_constants import (
        DEFAULT_BUILD_ENV,
        ESP_CHIP,
        FLASH_BAUD,
        IGNORED_BOOT_MESSAGES,
        LOCAL_ESPTOOL_CANDIDATES,
        MERGED_FIRMWARE_NAME,
        PI_ESPTOOL,
        PI_HOSTS,
        PI_TDECK_PORT,
        SERIAL_BAUD,
        boot_wait_for,
        first_existing_local_port,
    )

from audit_launcher_artifact import audit as audit_merged_artifact


class FlashError(RuntimeError):
    """Raised for build, transfer, flash, or boot verification failure."""


@dataclass(slots=True)
class BuildResult:
    environment: str
    firmware: Path
    duration_s: float
    output: str
    sha256: str


@dataclass(slots=True)
class FlashResult:
    firmware: Path
    target: str
    port: str
    duration_s: float
    flash_output: str
    boot_log: str
    boot_verified: bool
    sha256: str
    cold_boot_requested: bool = False


def _run(
    command: list[str],
    *,
    cwd: Path | None = None,
    timeout: float = 900,
    echo: bool = False,
) -> subprocess.CompletedProcess[str]:
    if echo:
        print("+ " + shlex.join(command), flush=True)
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            capture_output=True,
            text=True,
            errors="replace",
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise FlashError(f"command failed: {shlex.join(command)}: {exc}") from exc
    return result


def find_pi_host(preferred: str | None = None) -> str:
    """Return the first gateway that accepts non-interactive SSH."""

    candidates = (preferred,) if preferred else PI_HOSTS
    errors: list[str] = []
    for host in candidates:
        if not host:
            continue
        try:
            result = _run(
                ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", host, "true"],
                timeout=10,
            )
        except FlashError as exc:
            errors.append(f"{host}: {exc}")
            continue
        if result.returncode == 0:
            return host
        errors.append(f"{host}: {(result.stderr or result.stdout).strip()[-200:]}")
    raise FlashError("no Raspberry Pi gateway reachable (" + "; ".join(errors) + ")")


def resolve_esptool(explicit: str | None = None) -> str:
    candidates = (explicit,) if explicit else LOCAL_ESPTOOL_CANDIDATES
    for raw in candidates:
        if not raw:
            continue
        expanded = str(Path(raw).expanduser())
        if "/" in expanded and Path(expanded).is_file():
            return expanded
        resolved = shutil.which(expanded)
        if resolved:
            return resolved
    raise FlashError("esptool was not found in PATH or the configured virtual environments")


SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


def firmware_sha256(firmware: Path) -> str:
    digest = hashlib.sha256()
    with firmware.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def digest_from_metadata(metadata: Path, firmware: Path) -> str:
    try:
        payload = json.loads(metadata.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise FlashError(f"cannot read artifact metadata {metadata}: {exc}") from exc
    artifacts = payload.get("artifacts")
    if not isinstance(artifacts, dict):
        raise FlashError("artifact metadata has no artifacts mapping")

    direct = artifacts.get(firmware.name)
    if isinstance(direct, str) and SHA256_RE.fullmatch(direct):
        return direct

    aliases = {firmware.name}
    if firmware.name == MERGED_FIRMWARE_NAME:
        aliases.update({"sigurdos-tdeck-full.bin", "full", "merged"})
    for key, record in artifacts.items():
        if not isinstance(record, dict):
            continue
        filename = record.get("file")
        digest = record.get("sha256")
        if (key in aliases or filename in aliases) and isinstance(digest, str):
            if SHA256_RE.fullmatch(digest):
                return digest
    raise FlashError(
        f"artifact metadata has no lowercase SHA-256 for {firmware.name}"
    )


def validate_merged_firmware(
    firmware: Path,
    *,
    expected_sha256: str | None = None,
    metadata: Path | None = None,
    require_provenance: bool = False,
) -> Path:
    """Validate structure and provenance before any offset-zero flash."""

    resolved = firmware.expanduser().resolve()
    if resolved.name != MERGED_FIRMWARE_NAME:
        raise FlashError(
            f"refusing {resolved.name}: offset-zero flashing requires {MERGED_FIRMWARE_NAME}"
        )
    if not resolved.is_file():
        raise FlashError(f"merged firmware is missing: {resolved}")

    result = audit_merged_artifact(resolved)
    if not result.ok:
        raise FlashError("invalid merged ESP32-S3 image: " + "; ".join(result.errors))

    if expected_sha256 is not None and metadata is not None:
        raise FlashError("choose either an expected SHA-256 or artifact metadata")
    if metadata is not None:
        expected_sha256 = digest_from_metadata(metadata.expanduser().resolve(), resolved)
    if require_provenance and expected_sha256 is None:
        raise FlashError(
            "external firmware requires --sha256 or --metadata from the reviewed build"
        )
    if expected_sha256 is not None:
        if not SHA256_RE.fullmatch(expected_sha256):
            raise FlashError("expected SHA-256 must be 64 lowercase hexadecimal characters")
        actual = firmware_sha256(resolved)
        if actual != expected_sha256:
            raise FlashError(
                f"firmware SHA-256 mismatch: got {actual}, expected {expected_sha256}"
            )
    return resolved


class HardwareFlasher:
    def __init__(
        self,
        repo_root: Path,
        *,
        pi_mode: bool,
        port: str,
        pi_host: str | None = None,
        esptool: str | None = None,
    ) -> None:
        self.repo_root = repo_root.resolve()
        self.pi_mode = pi_mode
        self.port = port
        self.pi_host = pi_host
        self.esptool = esptool

    def build(self, environment: str) -> BuildResult:
        """Build any PlatformIO environment and require its merged image."""

        if not re.fullmatch(r"[A-Za-z0-9_.-]+", environment):
            raise FlashError(f"invalid PlatformIO environment name: {environment!r}")
        started = time.monotonic()
        result = _run(
            ["pio", "run", "-e", environment],
            cwd=self.repo_root,
            timeout=1800,
            echo=True,
        )
        output = result.stdout + result.stderr
        if result.returncode != 0:
            raise FlashError(f"build failed for {environment}:\n{output[-4000:]}")
        firmware = validate_merged_firmware(
            self.repo_root / ".pio" / "build" / environment / MERGED_FIRMWARE_NAME
        )
        return BuildResult(
            environment,
            firmware,
            time.monotonic() - started,
            output,
            firmware_sha256(firmware),
        )

    def _flash_local(self, firmware: Path) -> str:
        command = [
            resolve_esptool(self.esptool),
            "--chip",
            ESP_CHIP,
            "--port",
            self.port,
            "--baud",
            str(FLASH_BAUD),
            "--before",
            "default-reset",
            "--after",
            "hard-reset",
            "write-flash",
            "0",
            str(firmware),
        ]
        result = _run(command, timeout=180, echo=True)
        output = result.stdout + result.stderr
        if result.returncode != 0 or "Hash of data verified" not in output:
            raise FlashError(f"local flash failed:\n{output[-3000:]}")
        return output

    def _flash_pi(self, firmware: Path) -> tuple[str, str]:
        host = find_pi_host(self.pi_host)
        remote_dir = f"/tmp/sigurdos-hw-flash-{int(time.time())}"
        mkdir = _run(["ssh", host, "mkdir", "-p", remote_dir], timeout=20)
        if mkdir.returncode != 0:
            raise FlashError(f"cannot create Pi staging directory: {mkdir.stderr.strip()}")
        remote_firmware = f"{remote_dir}/{MERGED_FIRMWARE_NAME}"
        copied = _run(["scp", str(firmware), f"{host}:{remote_firmware}"], timeout=120, echo=True)
        if copied.returncode != 0:
            raise FlashError(f"firmware transfer failed: {copied.stderr.strip()}")
        command = (
            f"{PI_ESPTOOL} --chip {ESP_CHIP} --port {shlex.quote(self.port)} "
            f"--baud {FLASH_BAUD} --before default-reset --after hard-reset "
            f"write-flash 0 {shlex.quote(remote_firmware)}"
        )
        result = _run(["ssh", host, command], timeout=180, echo=True)
        output = result.stdout + result.stderr
        if result.returncode != 0 or "Hash of data verified" not in output:
            raise FlashError(f"Pi flash failed:\n{output[-3000:]}")
        return host, output

    @staticmethod
    def _serial_capture_code(port: str, duration_s: float) -> str:
        # The code is base64-wrapped before SSH transport. It does not touch DTR
        # explicitly and performs bulk reads so boot output is not line-buffered.
        return f"""
import sys, time
try:
    import serial
except ImportError:
    print('BOOT_PROBE_ERROR: pyserial missing', file=sys.stderr)
    raise SystemExit(3)
s = serial.Serial({port!r}, {SERIAL_BAUD}, timeout=0.2)
end = time.monotonic() + {duration_s!r}
data = bytearray()
while time.monotonic() < end:
    waiting = s.in_waiting
    chunk = s.read(waiting if waiting else 1)
    if chunk:
        data.extend(chunk)
s.close()
sys.stdout.buffer.write(bytes(data))
"""

    def _capture_boot_log(self, host: str | None, environment: str | None) -> str:
        duration = boot_wait_for(environment) + 20.0
        code = self._serial_capture_code(self.port, duration)
        if host:
            encoded = base64.b64encode(code.encode()).decode()
            remote_code = f"import base64;exec(base64.b64decode({encoded!r}))"
            result = _run(
                ["ssh", host, "python3 -c " + shlex.quote(remote_code)],
                timeout=duration + 20,
            )
        else:
            result = _run([sys.executable, "-c", code], timeout=duration + 10)
        output = result.stdout + result.stderr
        if result.returncode != 0:
            raise FlashError(f"boot-log capture failed: {output[-1200:]}")
        return output

    @staticmethod
    def assess_boot_log(log: str) -> tuple[bool, str]:
        filtered = "\n".join(
            line for line in log.splitlines() if not any(item in line for item in IGNORED_BOOT_MESSAGES)
        )
        fatal = (
            "Invalid image block",
            "invalid header",
            "Guru Meditation",
            "abort() was called",
            "TG0WDT_SYS_RST",
        )
        found = next((marker for marker in fatal if marker.casefold() in filtered.casefold()), None)
        if found:
            return False, f"fatal boot marker: {found}"
        if filtered.count("ESP-ROM:esp32s3") > 2:
            return False, "repeated ROM banners indicate a boot loop"
        ready = (
            "SigurdOS T-Deck ready" in filtered
            or "SigurdOS Remote Test Controller" in filtered
            or "[test] >" in filtered
        )
        if not ready:
            return False, "ready marker not observed before boot timeout"
        return True, "ready marker observed"

    @staticmethod
    def request_cold_boot() -> None:
        print(
            "COLD BOOT REQUIRED: unplug and reconnect the T-Deck USB cable so Launcher "
            "cannot continue booting the external-flash image.",
            flush=True,
        )
        if sys.stdin.isatty():
            input("Press Enter after power has been restored: ")
        else:
            print("Non-interactive session: waiting 15 seconds for the operator to power-cycle.")
            time.sleep(15)

    def flash(
        self,
        firmware: Path,
        *,
        environment: str | None = None,
        verify: bool = True,
        cold_boot: bool = False,
        expected_sha256: str | None = None,
        metadata: Path | None = None,
        require_provenance: bool = False,
    ) -> FlashResult:
        firmware = validate_merged_firmware(
            firmware,
            expected_sha256=expected_sha256,
            metadata=metadata,
            require_provenance=require_provenance,
        )
        digest = firmware_sha256(firmware)
        started = time.monotonic()
        if self.pi_mode:
            host, flash_output = self._flash_pi(firmware)
            target = host
        else:
            flash_output = self._flash_local(firmware)
            host = None
            target = "local"
        if cold_boot:
            self.request_cold_boot()
        boot_log = self._capture_boot_log(host, environment) if verify else ""
        verified = False
        if verify:
            verified, reason = self.assess_boot_log(boot_log)
            if not verified:
                raise FlashError(f"flash completed but boot verification failed: {reason}")
        return FlashResult(
            firmware=firmware,
            target=target,
            port=self.port,
            duration_s=time.monotonic() - started,
            flash_output=flash_output,
            boot_log=boot_log,
            boot_verified=verified,
            sha256=digest,
            cold_boot_requested=cold_boot,
        )


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--env", default=DEFAULT_BUILD_ENV, help="PlatformIO environment")
    parser.add_argument("--firmware", type=Path, help="existing firmware-merged.bin (skip build)")
    provenance = parser.add_mutually_exclusive_group()
    provenance.add_argument("--sha256", help="reviewed lowercase SHA-256 for --firmware")
    provenance.add_argument("--metadata", type=Path, help="reviewed artifact/evidence JSON")
    target = parser.add_mutually_exclusive_group()
    target.add_argument("--pi-mode", action="store_true", help="flash through the Pi gateway")
    target.add_argument("--local", action="store_true", help="flash a directly attached T-Deck")
    parser.add_argument("--pi-host")
    parser.add_argument("--port")
    parser.add_argument("--esptool")
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--no-verify", action="store_true")
    parser.add_argument("--cold-boot", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.firmware and not (args.sha256 or args.metadata):
        parser.error("--firmware requires --sha256 or --metadata from the reviewed build")
    if (args.sha256 or args.metadata) and not args.firmware:
        parser.error("--sha256/--metadata require --firmware")
    pi_mode = bool(args.pi_mode)
    port = args.port or (PI_TDECK_PORT if pi_mode else first_existing_local_port())
    flasher = HardwareFlasher(
        repo_root(),
        pi_mode=pi_mode,
        port=port,
        pi_host=args.pi_host,
        esptool=args.esptool,
    )
    try:
        if args.firmware:
            firmware = validate_merged_firmware(
                args.firmware,
                expected_sha256=args.sha256,
                metadata=args.metadata,
                require_provenance=True,
            )
        else:
            build = flasher.build(args.env)
            firmware = build.firmware
            print(
                f"BUILD PASS: {args.env} ({build.duration_s:.1f}s) -> {firmware} "
                f"sha256={build.sha256}"
            )
        if args.build_only:
            return 0
        result = flasher.flash(
            firmware,
            environment=args.env,
            verify=not args.no_verify,
            cold_boot=args.cold_boot,
            expected_sha256=args.sha256,
            metadata=args.metadata,
            require_provenance=bool(args.firmware),
        )
        print(
            f"FLASH PASS: {result.target}:{result.port} ({result.duration_s:.1f}s) "
            f"boot_verified={result.boot_verified} sha256={result.sha256}"
        )
    except FlashError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
