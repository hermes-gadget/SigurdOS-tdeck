#!/usr/bin/env python3
"""Authenticated local capture server for the GPS validation WiFi harness.

The default listener is loopback-only. For a device test, explicitly pass the
host's LAN address and copy the printed per-run token into the private WiFi
validation build config before flashing the firmware.
"""

from __future__ import annotations

import argparse
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import secrets
import threading


DEFAULT_PATH = "/gps"
MAX_RECORD_BYTES = 255
DEFAULT_MAX_RECORDS = 10_000
READ_TIMEOUT_SECONDS = 5.0
TOKEN_HEADER = "X-SigurdOS-Token"


class CaptureState:
    def __init__(self, out_dir: Path, *, max_records: int = DEFAULT_MAX_RECORDS) -> None:
        self.out_dir = out_dir
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.stream_path = self.out_dir / "gps_hw_stream.txt"
        self.raw_path = self.out_dir / "raw_requests.txt"
        self.count = 0
        self.lock_seen = False
        self.max_records = max_records
        self.lock = threading.Lock()

    def record(self, payload: str) -> bool:
        payload = payload.strip()
        if not payload:
            return False
        with self.lock:
            if self.count >= self.max_records:
                return False
            self.count += 1
            if "|fix=1|" in payload or payload.endswith("|fix=1"):
                self.lock_seen = True
            with self.raw_path.open("a", encoding="utf-8") as f:
                f.write(payload + "\n")
            with self.stream_path.open("a", encoding="utf-8") as f:
                f.write(payload + "\n")
        return True

    def status(self) -> tuple[int, bool]:
        with self.lock:
            return self.count, self.lock_seen


def make_handler(state: CaptureState, *, path: str, token: str):
    class Handler(BaseHTTPRequestHandler):
        def setup(self) -> None:
            super().setup()
            self.connection.settimeout(READ_TIMEOUT_SECONDS)

        def _authorized(self) -> bool:
            supplied = self.headers.get(TOKEN_HEADER, "")
            return secrets.compare_digest(supplied, token)

        def _reply(self, status: int, body: bytes) -> None:
            self.send_response(status)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            if self.path != "/health":
                self.send_error(404)
                return
            if not self._authorized():
                self._reply(401, b"unauthorized\n")
                return
            count, lock_seen = state.status()
            self._reply(200, f"ok records={count} lock_seen={int(lock_seen)}\n".encode())

        def do_POST(self) -> None:
            if self.path != path:
                self.send_error(404)
                return
            if not self._authorized():
                self._reply(401, b"unauthorized\n")
                return
            raw_length = self.headers.get("Content-Length")
            try:
                length = int(raw_length) if raw_length is not None else -1
            except ValueError:
                length = -1
            if length <= 0:
                self._reply(400, b"invalid content length\n")
                return
            if length > MAX_RECORD_BYTES:
                self._reply(413, b"record too large\n")
                return
            raw = self.rfile.read(length)
            if len(raw) != length:
                self._reply(400, b"truncated request\n")
                return
            try:
                payload = raw.decode("utf-8")
            except UnicodeDecodeError:
                self._reply(400, b"record must be UTF-8\n")
                return
            if not payload.startswith("@gps_hw|") or "\r" in payload or "\n" in payload:
                self._reply(400, b"invalid GPS record\n")
                return
            if not state.record(payload):
                self._reply(507, b"record limit reached\n")
                return
            self._reply(200, b"ok\n")

        def log_message(self, fmt: str, *args) -> None:
            return

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--path", default=DEFAULT_PATH)
    parser.add_argument("--token", help="per-run token (generated when omitted)")
    parser.add_argument("--max-records", type=int, default=DEFAULT_MAX_RECORDS)
    parser.add_argument("--out-dir", default=None)
    args = parser.parse_args()

    if args.out_dir:
        out_dir = Path(args.out_dir)
    else:
        stamp = datetime.now().strftime("%Y-%m-%d-%H%M%S")
        out_dir = Path(".pio") / "gps_validation_wifi" / stamp

    if (
        not args.path.startswith("/")
        or len(args.path) > 128
        or "?" in args.path
        or "#" in args.path
        or any(ch.isspace() for ch in args.path)
    ):
        parser.error("--path must be an absolute path without query or fragment")
    if args.max_records <= 0:
        parser.error("--max-records must be positive")
    token = args.token or secrets.token_urlsafe(24)
    if len(token) < 22 or len(token) > 128 or any(c.isspace() for c in token):
        parser.error("--token must be 22-128 non-whitespace characters")

    state = CaptureState(out_dir, max_records=args.max_records)
    server = ThreadingHTTPServer(
        (args.host, args.port), make_handler(state, path=args.path, token=token)
    )
    server.daemon_threads = True
    print(f"GPS validation capture server listening on {args.host}:{args.port}", flush=True)
    print(f"Path: {args.path}", flush=True)
    print(f"Per-run token: {token}", flush=True)
    print(
        "Set the same token as SIGURDOS_GPS_VALIDATION_WIFI_TOKEN before building.",
        flush=True,
    )
    print(f"Writing records to {state.stream_path}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
