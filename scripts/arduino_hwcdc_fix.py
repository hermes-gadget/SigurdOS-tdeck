"""Build Arduino-ESP32 2.0.17 with Espressif's HWCDC TX-race fix.

The pinned framework predates arduino-esp32 PR #12606.  Replace only the
framework source node used by this build; never modify PlatformIO's shared
package cache.
"""

from hashlib import sha256
from pathlib import Path
import shutil
import subprocess


Import("env")  # type: ignore  # noqa: F821


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))  # type: ignore  # noqa: F821
BUILD_DIR = Path(env.subst("$BUILD_DIR"))  # type: ignore  # noqa: F821
PACKAGES_DIR = Path(env.subst("$PROJECT_PACKAGES_DIR"))  # type: ignore  # noqa: F821

FRAMEWORK_SOURCE = (
    PACKAGES_DIR / "framework-arduinoespressif32" / "cores" / "esp32" /
    "HWCDC.cpp"
)
PATCH_FILE = PROJECT_DIR / "patches" / "arduino-esp32-2.0.17-hwcdc.patch"
STAGING_ROOT = BUILD_DIR / "hwcdc-backport"
PATCHED_SOURCE = STAGING_ROOT / "cores" / "esp32" / "HWCDC.cpp"

EXPECTED_SOURCE_SHA256 = (
    "d0a8ca606c2729c8522a041113285dbf27033c22a5a6af8649a7305ffe84c449"
)
EXPECTED_PATCHED_SHA256 = (
    "80454c33bf3b8743e7f418905fc461852e0de94fa3cef09b28e33ba75ca78e35"
)


def file_sha256(path):
    return sha256(path.read_bytes()).hexdigest()


if not FRAMEWORK_SOURCE.is_file():
    raise RuntimeError("Arduino HWCDC source not found: %s" % FRAMEWORK_SOURCE)

source_digest = file_sha256(FRAMEWORK_SOURCE)
if source_digest != EXPECTED_SOURCE_SHA256:
    raise RuntimeError(
        "Refusing to apply the Arduino 2.0.17 HWCDC backport to an unknown "
        "framework source (got %s)" % source_digest
    )

if STAGING_ROOT.exists():
    shutil.rmtree(STAGING_ROOT)
PATCHED_SOURCE.parent.mkdir(parents=True)
shutil.copyfile(FRAMEWORK_SOURCE, PATCHED_SOURCE)
# Normalize legacy trailing whitespace in the staged 2.0.17 source so the
# checked-in patch itself remains whitespace-clean.  The package source hash
# above is verified before this build-local normalization.
source_bytes = PATCHED_SOURCE.read_bytes()
PATCHED_SOURCE.write_bytes(
    b"\n".join(line.rstrip(b" \t") for line in source_bytes.split(b"\n"))
)

subprocess.run(
    ["git", "init", "-q"],
    cwd=STAGING_ROOT,
    check=True,
)
subprocess.run(
    ["git", "apply", "--unidiff-zero", str(PATCH_FILE)],
    cwd=STAGING_ROOT,
    check=True,
)

patched_digest = file_sha256(PATCHED_SOURCE)
if patched_digest != EXPECTED_PATCHED_SHA256:
    raise RuntimeError(
        "Arduino HWCDC backport produced unexpected source %s" % patched_digest
    )


def replace_hwcdc_source(node):
    source_path = Path(node.srcnode().get_abspath())
    if source_path != FRAMEWORK_SOURCE:
        raise RuntimeError("Matched unexpected HWCDC source: %s" % source_path)
    print("SigurdOS: using build-local Arduino HWCDC TX-race backport")
    return env.File(str(PATCHED_SOURCE))  # type: ignore  # noqa: F821


env.AddBuildMiddleware(  # type: ignore  # noqa: F821
    replace_hwcdc_source,
    "*cores/esp32/HWCDC.cpp",
)
