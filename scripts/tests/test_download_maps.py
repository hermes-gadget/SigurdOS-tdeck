"""CLI and resume-contract tests for the offline map downloader."""

from __future__ import annotations

import binascii
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from scripts import download_maps


def valid_png_tile(color: bytes = b"\x00\x00\x00") -> bytes:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (
            struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF)
        )

    row = b"\x00" + color * 256
    raw = row * 256
    ihdr = struct.pack(">IIBBBBB", 256, 256, 8, 2, 0, 0, 0)
    return (
        download_maps.PNG_SIGNATURE +
        chunk(b"IHDR", ihdr) +
        chunk(b"IDAT", zlib.compress(raw)) +
        chunk(b"IEND", b"")
    )


class DownloadMapsTests(unittest.TestCase):
    def test_documented_coordinate_mode_is_reachable(self) -> None:
        args = download_maps.parse_args(
            [
                "--name", "teesside",
                "--lat1", "54.45", "--lon1", "-1.45",
                "--lat2", "54.65", "--lon2", "-1.05",
                "--zoom", "8", "14",
                "--dry-run",
            ]
        )
        self.assertEqual(args.bounds, (54.45, -1.45, 54.65, -1.05))

    def test_bbox_and_city_modes_match_documented_commands(self) -> None:
        bbox = download_maps.parse_args(
            ["--name", "london", "--bbox", "51.3,-0.5,51.7,0.3"]
        )
        self.assertEqual(bbox.bounds, (51.3, -0.5, 51.7, 0.3))
        city = download_maps.parse_args(["--city", "London", "--zoom", "10", "14"])
        self.assertEqual(city.name, "london")
        self.assertEqual(city.bounds, download_maps.CITIES["london"])

    def test_partial_mixed_and_missing_modes_are_rejected(self) -> None:
        invalid = (
            ["--name", "partial", "--lat1", "1", "--lon1", "2"],
            [
                "--name", "mixed", "--bbox", "1,2,3,4",
                "--lat1", "1", "--lon1", "2", "--lat2", "3", "--lon2", "4",
            ],
            ["--name", "missing"],
            ["--bbox", "1,2,3,4"],
        )
        for argv in invalid:
            with self.subTest(argv=argv), self.assertRaises(SystemExit):
                download_maps.parse_args(argv)

    def test_resume_skips_existing_tile_but_default_refreshes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tile = root / "1" / "0" / "0.png"
            tile.parent.mkdir(parents=True)
            old = valid_png_tile()
            tile.write_bytes(old)
            config = {"url": "https://example.invalid/{z}/{x}/{y}.png"}

            with mock.patch.object(download_maps, "session") as session:
                self.assertTrue(
                    download_maps.download_tile((0, 0, 1, config, str(root), True))[3]
                )
                session.get.assert_not_called()

            new = valid_png_tile(b"\x01\x02\x03")
            response = SimpleNamespace(status_code=200, content=new)
            with mock.patch.object(
                download_maps, "session", SimpleNamespace(get=mock.Mock(return_value=response))
            ):
                self.assertTrue(
                    download_maps.download_tile((0, 0, 1, config, str(root), False))[3]
                )
            self.assertEqual(tile.read_bytes(), new)

    def test_resume_redownloads_invalid_existing_tile(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tile = root / "1" / "0" / "0.png"
            tile.parent.mkdir(parents=True)
            tile.write_bytes(b"partial")
            config = {"url": "https://example.invalid/{z}/{x}/{y}.png"}
            response = SimpleNamespace(status_code=200, content=valid_png_tile())
            session = SimpleNamespace(get=mock.Mock(return_value=response))

            with mock.patch.object(download_maps, "session", session):
                result = download_maps.download_tile(
                    (0, 0, 1, config, str(root), True)
                )

            self.assertTrue(result[3])
            session.get.assert_called_once()
            self.assertTrue(download_maps.is_valid_png_tile_file(tile))
            self.assertFalse((root / "1" / "0" / "0.png.tmp").exists())

    def test_http_200_non_png_is_not_committed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = {"url": "https://example.invalid/{z}/{x}/{y}.png"}
            response = SimpleNamespace(status_code=200, content=b"<html>error</html>")
            session = SimpleNamespace(get=mock.Mock(return_value=response))

            with mock.patch.object(download_maps, "session", session), \
                    mock.patch.object(download_maps.time, "sleep"):
                result = download_maps.download_tile(
                    (0, 0, 1, config, str(root), False)
                )

            self.assertFalse(result[3])
            self.assertFalse((root / "1" / "0" / "0.png").exists())
            self.assertFalse((root / "1" / "0" / "0.png.tmp").exists())

    def test_tile_index_records_only_valid_nonempty_tiles(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            valid = valid_png_tile()
            for relative in ("8/100/80.png", "8/100/82.png", "8/103/81.png"):
                tile = root / relative
                tile.parent.mkdir(parents=True, exist_ok=True)
                tile.write_bytes(valid)
            (root / "8/100/invalid.png").write_bytes(b"ignored")
            (root / "8/100/83.png").write_bytes(b"partial")
            out_of_world = root / "8" / "256" / "1.png"
            out_of_world.parent.mkdir(parents=True)
            out_of_world.write_bytes(valid)

            index = download_maps.write_tile_index(str(root))

            self.assertEqual(index["version"], 1)
            self.assertEqual(index["tile_size"], 256)
            self.assertEqual(index["zooms"], [{
                "z": 8,
                "min_x": 100,
                "max_x": 103,
                "min_y": 80,
                "max_y": 82,
                "sample_x": 100,
                "sample_y": 80,
                "count": 3,
            }])
            self.assertEqual(
                json.loads((root / "index.json").read_text()), index)
            self.assertFalse((root / "index.json.tmp").exists())

    def test_zoom_range_matches_server_and_firmware_limits(self) -> None:
        valid = download_maps.parse_args(
            ["--city", "London", "--zoom", "0", "18"]
        )
        self.assertEqual(valid.zoom, [0, 18])

        invalid_ranges = (
            ["--city", "London", "--zoom", "14", "8"],
            ["--city", "London", "--zoom", "-1", "2"],
            ["--city", "London", "--zoom", "18", "19"],
        )
        for argv in invalid_ranges:
            with self.subTest(argv=argv), self.assertRaises(SystemExit):
                download_maps.parse_args(argv)

        limited_server = {
            "url": "https://example.invalid/{z}/{x}/{y}.png",
            "attribution": "test",
            "max_zoom": 5,
        }
        with mock.patch.dict(
            download_maps.TILE_SERVERS, {"limited": limited_server}
        ):
            with self.assertRaises(SystemExit):
                download_maps.parse_args(
                    ["--city", "London", "--server", "limited", "--zoom", "0", "6"]
                )


if __name__ == "__main__":
    unittest.main()
