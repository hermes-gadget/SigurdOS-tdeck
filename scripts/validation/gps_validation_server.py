#!/usr/bin/env python3
"""Local capture server for the GPS validation WiFi harness."""

from __future__ import annotations

import argparse
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class CaptureState:
    def __init__(self, out_dir: Path) -> None:
        self.out_dir = out_dir
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.stream_path = self.out_dir / "gps_hw_stream.txt"
        self.raw_path = self.out_dir / "raw_requests.txt"
        self.count = 0
        self.lock_seen = False

    def record(self, payload: str) -> None:
        payload = payload.strip()
        if not payload:
            return
        self.count += 1
        if "|fix=1|" in payload or payload.endswith("|fix=1"):
            self.lock_seen = True
        with self.raw_path.open("a", encoding="utf-8") as f:
            f.write(payload + "\n")
        if payload.startswith("@gps_hw|"):
            with self.stream_path.open("a", encoding="utf-8") as f:
                f.write(payload + "\n")


def make_handler(state: CaptureState):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path != "/health":
                self.send_error(404)
                return
            body = f"ok records={state.count} lock_seen={int(state.lock_seen)}\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body.encode("utf-8"))

        def do_POST(self) -> None:
            length = int(self.headers.get("Content-Length", "0"))
            payload = self.rfile.read(length).decode("utf-8", errors="replace")
            state.record(payload)
            body = "ok\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body.encode("utf-8"))

        def log_message(self, fmt: str, *args) -> None:
            return

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--out-dir", default=None)
    args = parser.parse_args()

    if args.out_dir:
        out_dir = Path(args.out_dir)
    else:
        stamp = datetime.now().strftime("%Y-%m-%d-%H%M%S")
        out_dir = Path(".pio") / "gps_validation_wifi" / stamp

    state = CaptureState(out_dir)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(state))
    print(f"GPS validation capture server listening on {args.host}:{args.port}", flush=True)
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
