Import("env")

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
            ["git", "status", "--porcelain", "--untracked-files=no"],
            cwd=str(cwd),
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return True
    return bool(completed.stdout.strip())


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

env.Append(
    CPPDEFINES=[
        ("SIGURDOS_BUILD_GIT_SHA", _macro_string(git_sha)),
        ("SIGURDOS_BUILD_GIT_DIRTY", "1" if git_dirty else "0"),
        ("SIGURDOS_BUILD_MESHCORE_SHA", _macro_string(meshcore_sha)),
        ("SIGURDOS_BUILD_ENV", _macro_string(build_env)),
        ("SIGURDOS_BUILD_PARTITIONS", _macro_string(partitions)),
        ("SIGURDOS_BUILD_BOARD", _macro_string(board)),
        ("SIGURDOS_BUILD_MCU", _macro_string(mcu)),
    ]
)

dirty_suffix = "+dirty" if git_dirty else ""
print(
    "SigurdOS build metadata: "
    f"env={build_env} git={git_sha}{dirty_suffix} "
    f"meshcore={meshcore_sha} partitions={partitions}"
)
