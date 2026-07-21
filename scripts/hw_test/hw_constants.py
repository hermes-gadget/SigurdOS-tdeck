"""Shared constants and build capabilities for T-Deck hardware tests."""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path

SERIAL_BAUD = 115_200
FLASH_BAUD = 921_600
ESP_CHIP = "esp32s3"

PI_HOSTS: tuple[str, ...] = ("hermes-pi.local", "hermes-pi")
PI_TDECK_PORT = "/dev/ttyACM0"
PI_ESPTOOL = "~/hermes-venv/bin/esptool"

# Prefer the VM's USB-to-UART path when --local is explicitly selected, then
# fall back to native USB CDC. Callers may always override this with --port.
LOCAL_TDECK_PORTS: tuple[str, ...] = ("/dev/ttyUSB0", "/dev/ttyACM0")
LOCAL_ESPTOOL_CANDIDATES: tuple[str, ...] = (
    "esptool",
    "esptool.py",
    "~/hermes-venv/bin/esptool",
    "~/.hermes/hermes-agent/venv/bin/esptool",
    "~/.local/bin/esptool",
)

BOOT_WAIT_REMOTE_TEST_S = 10.0
BOOT_WAIT_RELEASE_S = 4.0
SERIAL_RECOVERY_WAIT_S = 10.0
# The bounded remote-test streamer deliberately yields to mesh/UI work. A
# radio-enabled debug build may need around two minutes for 4,800 CDATA lines.
SCREENSHOT_TIMEOUT_S = 165.0

DEFAULT_BUILD_ENV = "SigurdOS_TDeck_remote_test_radio"
MERGED_FIRMWARE_NAME = "firmware-merged.bin"


class CommandProtocol(StrEnum):
    """Serial command dialect exposed by a firmware build."""

    REMOTE_TEST = "remote_test"
    RELEASE = "release"
    UNKNOWN = "unknown"


@dataclass(frozen=True, slots=True)
class BuildCapabilities:
    """Capabilities expected from a PlatformIO environment."""

    radio: bool
    test_controller: bool
    ble_agent: bool = False
    periodic_stats: bool = False
    command_protocol: CommandProtocol = CommandProtocol.RELEASE


BUILD_ENVIRONMENTS: dict[str, BuildCapabilities] = {
    "SigurdOS_TDeck": BuildCapabilities(radio=True, test_controller=False),
    "SigurdOS_TDeck_meshv2": BuildCapabilities(radio=True, test_controller=False),
    "SigurdOS_TDeck_debug": BuildCapabilities(
        radio=True,
        test_controller=False,
        periodic_stats=True,
    ),
    "SigurdOS_TDeck_debug_869": BuildCapabilities(
        radio=True,
        test_controller=False,
        periodic_stats=True,
    ),
    "SigurdOS_TDeck_remote_test": BuildCapabilities(
        radio=False,
        test_controller=True,
        periodic_stats=True,
        command_protocol=CommandProtocol.REMOTE_TEST,
    ),
    "SigurdOS_TDeck_remote_test_radio": BuildCapabilities(
        radio=True,
        test_controller=True,
        periodic_stats=True,
        command_protocol=CommandProtocol.REMOTE_TEST,
    ),
    "SigurdOS_TDeck_remote_test_radio_testfreq": BuildCapabilities(
        radio=True,
        test_controller=True,
        periodic_stats=True,
        command_protocol=CommandProtocol.REMOTE_TEST,
    ),
    "SigurdOS_TDeck_remote_test_radio_roomtest": BuildCapabilities(
        radio=True,
        test_controller=True,
        periodic_stats=True,
        command_protocol=CommandProtocol.REMOTE_TEST,
    ),
    "SigurdOS_TDeck_remote_test_radio_usca": BuildCapabilities(
        radio=True,
        test_controller=True,
        periodic_stats=True,
        command_protocol=CommandProtocol.REMOTE_TEST,
    ),
    "SigurdOS_TDeck_remote_test_radio_usca_rxonly": BuildCapabilities(
        radio=True,
        test_controller=True,
        periodic_stats=True,
        command_protocol=CommandProtocol.REMOTE_TEST,
    ),
    "SigurdOS_TDeck_remote_test_radio_meshv2": BuildCapabilities(
        radio=True,
        test_controller=True,
        periodic_stats=True,
        command_protocol=CommandProtocol.REMOTE_TEST,
    ),
    "SigurdOS_TDeck_ble_agent": BuildCapabilities(
        radio=True,
        test_controller=True,
        ble_agent=True,
        periodic_stats=True,
        command_protocol=CommandProtocol.REMOTE_TEST,
    ),
    "SigurdOS_TDeck_telemetry": BuildCapabilities(
        radio=True,
        test_controller=True,
        periodic_stats=True,
        command_protocol=CommandProtocol.REMOTE_TEST,
    ),
}


@dataclass(frozen=True, slots=True)
class RadioParameters:
    """Radio parameters used by the explicit --radio test."""

    frequency_mhz: float
    spreading_factor: int
    bandwidth_khz: float
    coding_rate: int
    tx_power_dbm: int


# Low-power bench defaults. Operators remain responsible for choosing a legal,
# coordinated frequency for their region; the CLI supports overriding every
# value and the radio phase only runs after an explicit --radio/--full/--all.
TEST_RADIO_PARAMS = RadioParameters(
    frequency_mhz=868.100,
    spreading_factor=10,
    bandwidth_khz=250.0,
    coding_rate=5,
    tx_power_dbm=2,
)
TEST_RADIO_FREQUENCIES_MHZ: dict[str, float] = {
    "eu": 868.100,
    "uk_mesh_do_not_use": 869.525,
    "us_ca": 915.000,
}
TEST_CHANNEL_PREFIX = "sigurd-hwtest"

UI_SCREEN_TARGETS: tuple[str, ...] = (
    "home",
    "chat",
    "contacts",
    "channels",
    "network",
    "heard",
    "repeaters",
    "map",
    "advertise",
    "settings",
    "trace",
    "terminal",
    "signal",
    "radio",
    "s-radio",
    "s-gps",
    "s-display",
    "s-system",
    "node-status",
    "telemetry",
)

CRASH_MARKERS: tuple[str, ...] = (
    "Guru Meditation",
    "PANIC",
    "panic'ed",
    "Stack overflow",
    "abort() was called",
    "FATAL",
    "TG0WDT_SYS_RST",
    "Invalid image block",
    "invalid header",
)
ROM_BOOT_MARKERS: tuple[str, ...] = ("ESP-ROM:esp32s3", "rst:0x")
IGNORED_BOOT_MESSAGES: tuple[str, ...] = (
    "i2cRead returned Error 263",
    "SPIFFS Already Mounted",
)


def capabilities_for(environment: str | None) -> BuildCapabilities | None:
    """Return known capabilities without preventing unknown env builds."""

    if not environment:
        return None
    return BUILD_ENVIRONMENTS.get(environment)


def boot_wait_for(environment: str | None) -> float:
    """Return the standardized post-open boot wait for an environment."""

    caps = capabilities_for(environment)
    if caps and caps.test_controller:
        return BOOT_WAIT_REMOTE_TEST_S
    return BOOT_WAIT_RELEASE_S


def first_existing_local_port() -> str:
    """Choose the first known local T-Deck path, preserving documented order."""

    for candidate in LOCAL_TDECK_PORTS:
        if Path(candidate).exists():
            return candidate
    return LOCAL_TDECK_PORTS[0]
