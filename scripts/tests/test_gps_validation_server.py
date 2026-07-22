"""Security and protocol tests for the GPS validation capture server."""

from __future__ import annotations

import http.client
import sys
import tempfile
import threading
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from validation.gps_validation_server import (  # noqa: E402
    MAX_RECORD_BYTES,
    TOKEN_HEADER,
    CaptureState,
    ThreadingHTTPServer,
    make_handler,
)


class GpsValidationServerTests(unittest.TestCase):
    TOKEN = "test-token-0123456789abcdef"

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.state = CaptureState(Path(self.temporary.name), max_records=1)
        self.server = ThreadingHTTPServer(
            ("127.0.0.1", 0),
            make_handler(self.state, path="/gps", token=self.TOKEN),
        )
        self.server.daemon_threads = True
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.temporary.cleanup()

    def request(self, method: str, path: str, body: bytes = b"", *, token=True):
        connection = http.client.HTTPConnection(
            "127.0.0.1", self.server.server_port, timeout=2
        )
        headers = {TOKEN_HEADER: self.TOKEN} if token else {}
        connection.request(method, path, body=body, headers=headers)
        response = connection.getresponse()
        status, payload = response.status, response.read()
        connection.close()
        return status, payload

    def test_rejects_wrong_path_and_missing_token(self) -> None:
        self.assertEqual(self.request("POST", "/wrong", b"@gps_hw|fix=0")[0], 404)
        self.assertEqual(
            self.request("POST", "/gps", b"@gps_hw|fix=0", token=False)[0], 401
        )
        self.assertEqual(self.request("GET", "/health", token=False)[0], 401)

    def test_rejects_oversized_and_invalid_records(self) -> None:
        self.assertEqual(
            self.request("POST", "/gps", b"x" * (MAX_RECORD_BYTES + 1))[0], 413
        )
        self.assertEqual(self.request("POST", "/gps", b"not-gps")[0], 400)
        self.assertEqual(self.request("POST", "/gps", b"@gps_hw|fix=0\nextra")[0], 400)

    def test_serializes_valid_record_and_caps_disk_growth(self) -> None:
        self.assertEqual(self.request("POST", "/gps", b"@gps_hw|fix=1")[0], 200)
        self.assertEqual(self.request("POST", "/gps", b"@gps_hw|fix=0")[0], 507)
        status, health = self.request("GET", "/health")
        self.assertEqual(status, 200)
        self.assertIn(b"records=1 lock_seen=1", health)
        self.assertEqual(
            self.state.stream_path.read_text(encoding="utf-8"), "@gps_hw|fix=1\n"
        )

    def test_firmware_and_private_build_config_send_token_header(self) -> None:
        firmware = (SCRIPTS.parent / "src/validation/gps_validation_wifi.cpp").read_text()
        flags = (SCRIPTS / "validation/gps_validation_wifi_flags.py").read_text()
        self.assertIn('"X-SigurdOS-Token: %s\\r\\n"', firmware)
        self.assertIn("SIGURDOS_GPS_VALIDATION_WIFI_TOKEN", firmware)
        self.assertIn('"token": "SIGURDOS_GPS_VALIDATION_WIFI_TOKEN"', flags)
        self.assertIn('(\"ssid\", \"host\", \"token\")', flags)


if __name__ == "__main__":
    unittest.main()
