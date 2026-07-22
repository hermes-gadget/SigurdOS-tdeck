Import("env")

import os
import subprocess
from pathlib import Path


def _macro_string(value):
    escaped = str(value).replace("\\", "\\\\").replace('"', '\\"')
    return '\\"' + escaped + '\\"'


def _git(args, cwd):
    try:
        completed = subprocess.run(
            ["git", *args],
            cwd=str(cwd),
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    value = completed.stdout.strip()
    return value if value else "unknown"


def _git_dirty(cwd):
    try:
        completed = subprocess.run(
            ["git", "status", "--porcelain", "--untracked-files=normal"],
            cwd=str(cwd),
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return True
    return bool(completed.stdout.strip())


def _bounded(value, limit):
    return str(value)[:limit]


def _git_describe(cwd):
    try:
        completed = subprocess.run(
            ["git", "describe", "--tags", "--dirty"],
            cwd=str(cwd),
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    value = completed.stdout.strip()
    return value if value else None


project_dir = Path(env.subst("$PROJECT_DIR"))
meshcore_dir = project_dir / "lib" / "meshcore"
board_config = env.BoardConfig()

build_env = env.subst("$PIOENV") or "unknown"
board = env.subst("$BOARD") or "unknown"
mcu = board_config.get("build.mcu", "esp32s3")
partitions = env.GetProjectOption("board_build.partitions", "")
if not partitions:
    partitions = board_config.get("build.partitions", "unknown")

git_sha = _git(["rev-parse", "--short=12", "HEAD"], project_dir)
meshcore_sha = _git(["rev-parse", "--short=12", "HEAD"], meshcore_dir)
git_dirty = _git_dirty(project_dir)
git_tag = _git_describe(project_dir)
if git_tag and git_dirty and not git_tag.endswith("-dirty"):
    git_tag += "-dirty"

build_source = "github_actions" if os.environ.get("GITHUB_ACTIONS") == "true" else "local"
actions_run_id = os.environ.get("GITHUB_RUN_ID", "local")
actions_run_attempt = os.environ.get("GITHUB_RUN_ATTEMPT", "")
actions_ref = os.environ.get("GITHUB_REF_NAME") or _git(["rev-parse", "--abbrev-ref", "HEAD"], project_dir)
actions_run_url = ""
if build_source == "github_actions":
    server_url = os.environ.get("GITHUB_SERVER_URL", "https://github.com").rstrip("/")
    repository = os.environ.get("GITHUB_REPOSITORY", "")
    if repository and actions_run_id:
        actions_run_url = f"{server_url}/{repository}/actions/runs/{actions_run_id}"

build_source = _bounded(build_source, 32)
actions_run_id = _bounded(actions_run_id, 32)
actions_run_attempt = _bounded(actions_run_attempt, 16)
actions_ref = _bounded(actions_ref, 128)
actions_run_url = _bounded(actions_run_url, 256)

# Set SIGURDOS_VERSION from git describe with tdeck_pins.h define as fallback
sigurdos_version = _macro_string(git_tag) if git_tag else None

env.Append(
    CPPDEFINES=[
        ("SIGURDOS_BUILD_GIT_SHA", _macro_string(git_sha)),
        ("SIGURDOS_BUILD_GIT_DIRTY", "1" if git_dirty else "0"),
        ("SIGURDOS_BUILD_MESHCORE_SHA", _macro_string(meshcore_sha)),
        ("SIGURDOS_BUILD_ENV", _macro_string(build_env)),
        ("SIGURDOS_BUILD_PARTITIONS", _macro_string(partitions)),
        ("SIGURDOS_BUILD_BOARD", _macro_string(board)),
        ("SIGURDOS_BUILD_MCU", _macro_string(mcu)),
        ("SIGURDOS_BUILD_SOURCE", _macro_string(build_source)),
        ("SIGURDOS_BUILD_ACTIONS_RUN_ID", _macro_string(actions_run_id)),
        ("SIGURDOS_BUILD_ACTIONS_RUN_ATTEMPT", _macro_string(actions_run_attempt)),
        ("SIGURDOS_BUILD_ACTIONS_REF", _macro_string(actions_ref)),
        ("SIGURDOS_BUILD_ACTIONS_RUN_URL", _macro_string(actions_run_url)),
    ]
)

# Add build-derived SIGURDOS_VERSION if git describe succeeded
# (overrides the static default in tdeck_pins.h)
if sigurdos_version:
    env.Append(CPPDEFINES=[("SIGURDOS_VERSION", sigurdos_version)])

dirty_suffix = "+dirty" if git_dirty else ""
print(
    "SigurdOS build metadata: "
    f"env={build_env} git={git_sha}{dirty_suffix} "
    f"meshcore={meshcore_sha} partitions={partitions} "
    f"source={build_source} run={actions_run_id}"
)
