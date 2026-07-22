#!/usr/bin/env python3
"""
SigurdOS Map Downloader

Downloads OpenStreetMap tiles for a specified region and zoom range,
outputting PNG tiles in the directory structure expected by the firmware:
    tiles/{z}/{x}/{y}.png

Copy the resulting 'tiles/' directory to the root of the T-Deck SD card.

Usage examples:
    # Download Teesside area, zooms 8-14
    python3 download_maps.py --name teesside --lat1 54.45 --lon1 -1.45 --lat2 54.65 --lon2 -1.05 --zoom 8 14

    # Download London, zooms 10-15 only
    python3 download_maps.py --name london --bbox "51.3,-0.5,51.7,0.3" --zoom 10 15

    # UK national overview (zooms 5-8 only, larger tiles)
    python3 download_maps.py --name uk --lat1 49.8 --lon1 -8.5 --lat2 58.8 --lon2 1.8 --zoom 5 8 --server cyclosm

Tile servers:
    osm       OpenStreetMap (default, requires User-Agent)
    cyclosm   CyclOSM (bicycle-oriented, OSM data)
    carto     CartoDB (light theme)

Dependencies: pip install requests
License: GPL-3.0-or-later
Copyright (C) 2025 Ben
"""

import argparse
import math
import os
import sys
import time
import json
from concurrent.futures import ThreadPoolExecutor, as_completed
from threading import Lock

try:
    import requests
except ImportError:
    print("ERROR: 'requests' required. Install: pip install requests")
    sys.exit(1)

# ── Tile servers ────────────────────────────────────────────

TILE_SERVERS = {
    "osm": {
        "url": "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
        "attribution": "© OpenStreetMap contributors",
        "max_zoom": 19,
        "user_agent": "SigurdOS-MapDownloader/1.0",
    },
    "cyclosm": {
        "url": "https://{s}.tile-cyclosm.openstreetmap.fr/cyclosm/{z}/{x}/{y}.png",
        "attribution": "© OpenStreetMap contributors, CyclOSM",
        "max_zoom": 20,
        "subdomains": ["a", "b", "c"],
        "user_agent": "SigurdOS-MapDownloader/1.0",
    },
    "carto": {
        "url": "https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}.png",
        "attribution": "© OpenStreetMap contributors, CARTO",
        "max_zoom": 19,
        "subdomains": ["a", "b", "c", "d"],
        "user_agent": "SigurdOS-MapDownloader/1.0",
    },
}

CITIES = {
    "london": (51.3, -0.5, 51.7, 0.3),
    "toronto": (43.55, -79.65, 43.85, -79.15),
    "montreal": (45.4, -73.75, 45.65, -73.45),
    "vancouver": (49.15, -123.3, 49.35, -122.95),
    "ottawa": (45.25, -76.05, 45.55, -75.45),
    "new-york": (40.55, -74.15, 40.9, -73.7),
    "seattle": (47.45, -122.45, 47.8, -122.15),
    "chicago": (41.7, -88.0, 42.05, -87.5),
    "los-angeles": (33.85, -118.55, 34.2, -118.15),
    "san-francisco": (37.65, -122.55, 37.9, -122.3),
    "manchester": (53.35, -2.45, 53.55, -2.1),
    "birmingham": (52.35, -2.05, 52.55, -1.7),
    "leeds": (53.7, -1.7, 53.9, -1.4),
    "liverpool": (53.3, -3.1, 53.5, -2.8),
    "bristol": (51.35, -2.75, 51.55, -2.45),
    "edinburgh": (55.85, -3.35, 56.05, -3.05),
    "glasgow": (55.75, -4.4, 55.95, -4.1),
    "cardiff": (51.4, -3.3, 51.55, -3.05),
    "belfast": (54.5, -6.1, 54.7, -5.8),
    "teesside": (54.45, -1.45, 54.65, -1.05),
    "newcastle": (54.9, -1.75, 55.05, -1.45),
    "sheffield": (53.3, -1.6, 53.45, -1.35),
    "nottingham": (52.85, -1.3, 53.05, -1.05),
    "southampton": (50.85, -1.5, 50.95, -1.3),
    "brighton": (50.75, -0.25, 50.9, -0.05),
    "cambridge": (52.1, 0.05, 52.25, 0.2),
    "oxford": (51.7, -1.35, 51.8, -1.15),
}

# ── Tile math (Web Mercator) ────────────────────────────────

def latlon_to_tile(lat, lon, zoom):
    """Convert lat/lon to tile x/y at given zoom level."""
    n = 1 << zoom
    x = int((lon + 180.0) / 360.0 * n)
    lat_rad = math.radians(lat)
    y = int((1.0 - math.log(math.tan(lat_rad) + 1.0 / math.cos(lat_rad)) / math.pi) / 2.0 * n)
    return x, y

def tile_to_latlon(x, y, zoom):
    """Convert tile x/y back to lat/lon (top-left corner)."""
    n = 1 << zoom
    lon = x / n * 360.0 - 180.0
    lat_rad = math.atan(math.sinh(math.pi * (1.0 - 2.0 * y / n)))
    lat = math.degrees(lat_rad)
    return lat, lon

def tiles_for_bbox(lat1, lon1, lat2, lon2, zoom):
    """Yield all (x, y, zoom) tile coordinates covering a bounding box."""
    # Normalize: lat1/lon1 = southwest, lat2/lon2 = northeast
    min_lat, max_lat = min(lat1, lat2), max(lat1, lat2)
    min_lon, max_lon = min(lon1, lon2), max(lon1, lon2)

    # Clamp to valid range
    min_lat = max(min_lat, -85.0511)
    max_lat = min(max_lat, 85.0511)
    min_lon = max(min_lon, -180.0)
    max_lon = min(max_lon, 180.0)

    tx1, ty1 = latlon_to_tile(max_lat, min_lon, zoom)  # top-left
    tx2, ty2 = latlon_to_tile(min_lat, max_lon, zoom)  # bottom-right

    n = 1 << zoom
    tx1 = max(0, tx1)
    ty1 = max(0, ty1)
    tx2 = min(n - 1, tx2)
    ty2 = min(n - 1, ty2)

    count = (tx2 - tx1 + 1) * (ty2 - ty1 + 1)
    for x in range(tx1, tx2 + 1):
        for y in range(ty1, ty2 + 1):
            yield x, y, zoom

    return count

# ── Download logic ──────────────────────────────────────────

stats_lock = Lock()
download_stats = {"done": 0, "failed": 0, "total": 0}
session = None

def init_session(server_config):
    global session
    session = requests.Session()
    session.headers.update({
        "User-Agent": server_config.get("user_agent", "Sigurdos/1.0"),
        "Accept": "image/png,image/jpeg,image/webp,*/*",
    })
    return session

def download_tile(args):
    """Download a single PNG tile. Returns (x, y, zoom, success)."""
    x, y, zoom, server_config, output_dir, resume = args
    global session, download_stats

    # Build URL
    url_tpl = server_config["url"]
    sub = server_config.get("subdomains", [None])
    sub_idx = (x + y) % len(sub) if sub[0] else 0
    s = sub[sub_idx] if sub_idx is not None else ""
    url = url_tpl.replace("{z}", str(zoom)).replace("{x}", str(x)).replace("{y}", str(y))
    if "{s}" in url and s:
        url = url.replace("{s}", s)

    # Output path
    out_dir = os.path.join(output_dir, str(zoom), str(x))
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"{y}.png")

    # Resume keeps a prior non-empty tile. The default refreshes it.
    if resume and os.path.exists(out_path) and os.path.getsize(out_path) > 0:
        with stats_lock:
            download_stats["done"] += 1
        return x, y, zoom, True

    # Download with retries
    max_retries = 3
    for attempt in range(max_retries):
        try:
            resp = session.get(url, timeout=30)
            if resp.status_code == 200:
                with open(out_path, "wb") as f:
                    f.write(resp.content)

                with stats_lock:
                    download_stats["done"] += 1
                return x, y, zoom, True
            elif resp.status_code == 404:
                # Tile doesn't exist at this zoom (e.g., ocean tile)
                if os.path.exists(out_path):
                    os.unlink(out_path)
                with stats_lock:
                    download_stats["done"] += 1
                return x, y, zoom, True
            elif resp.status_code == 429:
                time.sleep(2 * (attempt + 1))
            else:
                time.sleep(1 * (attempt + 1))
        except Exception as e:
            if attempt == max_retries - 1:
                with stats_lock:
                    download_stats["failed"] += 1
                return x, y, zoom, False
            time.sleep(1 * (attempt + 1))

    with stats_lock:
        download_stats["failed"] += 1
    return x, y, zoom, False

# ── Metadata ────────────────────────────────────────────────

def write_metadata(output_dir, name, server_config, lat1, lon1, lat2, lon2, zoom_min, zoom_max):
    """Write metadata.json describing the downloaded area."""
    min_lat, max_lat = min(lat1, lat2), max(lat1, lat2)
    min_lon, max_lon = min(lon1, lon2), max(lon1, lon2)

    metadata = {
        "name": name,
        "attribution": server_config["attribution"],
        "bounds": [min_lon, min_lat, max_lon, max_lat],
        "zoom_range": [zoom_min, zoom_max],
        "format": "png",
        "tile_size": 256,
    }

    meta_path = os.path.join(output_dir, "metadata.json")
    with open(meta_path, "w") as f:
        json.dump(metadata, f, indent=2)

    return metadata

# ── Main ────────────────────────────────────────────────────

def build_parser():
    parser = argparse.ArgumentParser(
        description="Download map tiles for Sigurdos offline use",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--name", help="Region name (defaults to --city)")
    parser.add_argument("--output", default=None, help="Output directory (default: ./maps-{name})")

    # Bounding box: either --bbox "lat1,lon1,lat2,lon2" or individual --lat1/--lon1/--lat2/--lon2
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--bbox", help="Bounding box: 'lat1,lon1,lat2,lon2'")
    group.add_argument("--city", help="City name for quick download (e.g. 'London', 'Manchester')")

    parser.add_argument("--lat1", type=float, default=None)
    parser.add_argument("--lon1", type=float, default=None)
    parser.add_argument("--lat2", type=float, default=None)
    parser.add_argument("--lon2", type=float, default=None)

    parser.add_argument("--zoom", type=int, nargs=2, metavar=("MIN", "MAX"),
                        default=[10, 14], help="Zoom range (default: 10 14)")

    parser.add_argument("--server", choices=list(TILE_SERVERS.keys()),
                        default="osm", help="Tile server (default: osm)")
    parser.add_argument("--workers", type=int, default=4,
                        help="Parallel download threads (default: 4)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Calculate tile count without downloading")
    parser.add_argument("--resume", action="store_true",
                        help="Skip already-downloaded tiles")
    return parser


def parse_args(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)

    coordinates = (args.lat1, args.lon1, args.lat2, args.lon2)
    any_coordinates = any(value is not None for value in coordinates)
    all_coordinates = all(value is not None for value in coordinates)
    if any_coordinates and not all_coordinates:
        parser.error("coordinate mode requires --lat1, --lon1, --lat2, and --lon2")
    modes = int(args.bbox is not None) + int(args.city is not None) + int(all_coordinates)
    if modes != 1:
        parser.error("choose exactly one location mode: --city, --bbox, or all four coordinates")
    if (args.bbox is not None or all_coordinates) and not args.name:
        parser.error("--name is required with --bbox or coordinate mode")
    if args.workers < 1:
        parser.error("--workers must be at least 1")

    # Parse bounding box
    if args.bbox is not None:
        try:
            parts = [float(x.strip()) for x in args.bbox.split(",")]
        except ValueError:
            parser.error("--bbox values must be numbers")
        if len(parts) != 4:
            parser.error("--bbox needs exactly 4 values: lat1,lon1,lat2,lon2")
        lat1, lon1, lat2, lon2 = parts
    elif args.city:
        key = args.city.lower()
        if key not in CITIES:
            parser.error(f"unknown city '{args.city}'. Known: {', '.join(sorted(CITIES))}")
        lat1, lon1, lat2, lon2 = CITIES[key]
        args.name = args.name or key
    else:
        lat1, lon1, lat2, lon2 = args.lat1, args.lon1, args.lat2, args.lon2

    args.bounds = (lat1, lon1, lat2, lon2)
    return args


def main(argv=None):
    args = parse_args(argv)
    lat1, lon1, lat2, lon2 = args.bounds
    if args.city:
        print(f"City '{args.city}': bbox={lat1},{lon1},{lat2},{lon2}")

    zoom_min, zoom_max = args.zoom
    if zoom_min > zoom_max:
        zoom_min, zoom_max = zoom_max, zoom_min

    server_config = TILE_SERVERS[args.server]
    output_dir = args.output or f"maps-{args.name}"
    tiles_dir = os.path.join(output_dir, "tiles")

    print(f"Region: {args.name}")
    print(f"Bounds: {lat1:.4f},{lon1:.4f} -> {lat2:.4f},{lon2:.4f}")
    print(f"Zooms:  {zoom_min}-{zoom_max}")
    print(f"Server: {args.server} ({server_config['url']})")
    print(f"Output: {output_dir}/")
    print()

    # Count tiles
    total_tiles = 0
    for zoom in range(zoom_min, zoom_max + 1):
        count = 0
        for _ in tiles_for_bbox(lat1, lon1, lat2, lon2, zoom):
            count += 1
        total_tiles += count
        if count > 0:
            print(f"  zoom {zoom}: {count} tiles")
        else:
            print(f"  zoom {zoom}: 0 tiles (skipped)")

    print(f"\nTotal: {total_tiles} tiles")

    if total_tiles == 0:
        print("No tiles to download. Check your bounding box and zoom range.")
        sys.exit(0)

    # Estimate size and time
    est_mb = total_tiles * 0.025  # ~25KB per PNG tile
    print(f"Estimated size: ~{est_mb:.0f} MB")
    print(f"Estimated time: ~{total_tiles / args.workers / 2:.0f} seconds ({args.workers} workers)")

    if args.dry_run:
        print("\nDry run - no tiles downloaded. Remove --dry-run to download.")
        return

    print(f"\nDownloading with {args.workers} workers...")
    print("Press Ctrl+C to cancel (partial downloads are safe to --resume)\n")
    os.makedirs(tiles_dir, exist_ok=True)

    # Build tile list
    tiles = []
    for zoom in range(zoom_min, zoom_max + 1):
        for x, y, _ in tiles_for_bbox(lat1, lon1, lat2, lon2, zoom):
            tiles.append((x, y, zoom, server_config, tiles_dir, args.resume))

    download_stats["total"] = len(tiles)
    download_stats["done"] = 0
    download_stats["failed"] = 0

    # Download in parallel
    start_time = time.time()
    init_session(server_config)

    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = {executor.submit(download_tile, t): t for t in tiles}

        last_report = 0
        for future in as_completed(futures):
            result = future.result()
            now = time.time()
            if now - last_report > 2.0:  # report every 2 seconds
                elapsed = now - start_time
                rate = download_stats["done"] / elapsed if elapsed > 0 else 0
                pct = download_stats["done"] / download_stats["total"] * 100
                print(f"\r  {download_stats['done']}/{download_stats['total']} tiles "
                      f"({pct:.0f}%) - {rate:.0f} tiles/s "
                      f"- {download_stats['failed']} failed  ", end="", flush=True)
                last_report = now

    elapsed = time.time() - start_time
    print(f"\r  {download_stats['done']}/{download_stats['total']} tiles "
          f"(100%) - {download_stats['done']/elapsed:.0f} tiles/s "
          f"- {download_stats['failed']} failed  ")

    # Write metadata
    write_metadata(tiles_dir, args.name, server_config, lat1, lon1, lat2, lon2,
                   zoom_min, zoom_max)

    print(f"\nDone! {download_stats['done']} tiles downloaded, {download_stats['failed']} failed")
    print(f"Output: {output_dir}/")
    print(f"\nTo use on T-Deck:")
    print(f"  Copy the 'tiles/' folder to the root of the SD card")
    print(f"  The firmware reads from: /sdcard/tiles/{{z}}/{{x}}/{{y}}.png")


if __name__ == "__main__":
    main()
