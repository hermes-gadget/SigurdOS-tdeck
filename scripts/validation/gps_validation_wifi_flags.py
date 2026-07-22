Import("env")

import json
import os
from pathlib import Path


def _macro_string(value):
    escaped = str(value).replace("\\", "\\\\").replace('"', '\\"')
    return '\\"' + escaped + '\\"'


def _load_config():
    project_dir = Path(env.subst("$PROJECT_DIR"))
    config_path = Path(os.environ.get(
        "SIGURDOS_GPS_VALIDATION_WIFI_CONFIG",
        project_dir / ".pio" / "gps_validation_wifi_config.json",
    ))

    data = {}
    if config_path.exists():
        with config_path.open("r", encoding="utf-8-sig") as f:
            data.update(json.load(f))

    env_map = {
        "ssid": "SIGURDOS_GPS_VALIDATION_WIFI_SSID",
        "password": "SIGURDOS_GPS_VALIDATION_WIFI_PASS",
        "host": "SIGURDOS_GPS_VALIDATION_WIFI_HOST",
        "port": "SIGURDOS_GPS_VALIDATION_WIFI_PORT",
        "path": "SIGURDOS_GPS_VALIDATION_WIFI_PATH",
        "token": "SIGURDOS_GPS_VALIDATION_WIFI_TOKEN",
    }
    for key, env_name in env_map.items():
        if env_name in os.environ:
            data[key] = os.environ[env_name]

    return data, config_path


config, loaded_from = _load_config()
missing = [key for key in ("ssid", "host", "token") if not str(config.get(key, ""))]
if missing:
    raise RuntimeError(
        "GPS WiFi validation config is missing required field(s): "
        + ", ".join(missing)
        + ". Use environment variables or .pio/gps_validation_wifi_config.json."
    )

port = int(config.get("port", 8765))
if port <= 0 or port > 65535:
    raise RuntimeError("GPS WiFi validation config has an invalid port")

path = str(config.get("path", "/gps"))
if not path.startswith("/"):
    path = "/" + path
if len(path) > 128 or "?" in path or "#" in path or any(ch.isspace() for ch in path):
    raise RuntimeError("GPS WiFi validation path is invalid")

token = str(config["token"])
if len(token) < 22 or len(token) > 128 or any(ch.isspace() for ch in token):
    raise RuntimeError("GPS WiFi validation token must be 22-128 non-whitespace characters")

env.Append(
    CPPDEFINES=[
        ("SIGURDOS_GPS_VALIDATION_WIFI_SSID", _macro_string(config["ssid"])),
        ("SIGURDOS_GPS_VALIDATION_WIFI_PASS", _macro_string(config.get("password", ""))),
        ("SIGURDOS_GPS_VALIDATION_WIFI_HOST", _macro_string(config["host"])),
        ("SIGURDOS_GPS_VALIDATION_WIFI_PORT", str(port)),
        ("SIGURDOS_GPS_VALIDATION_WIFI_PATH", _macro_string(path)),
        ("SIGURDOS_GPS_VALIDATION_WIFI_TOKEN", _macro_string(token)),
    ]
)

print("GPS WiFi validation config loaded; sensitive values are not printed.")
