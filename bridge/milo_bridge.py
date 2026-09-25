"""Small LAN bridge: Open-Meteo HTTPS -> fixed, bounded text for the RT1170.

Run on a computer on the same trusted LAN as the board. No API key or packages
are required. The board does not expose or store Wi-Fi credentials here.
"""
from __future__ import annotations

import argparse
import json
import threading
import time
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlencode
from urllib.request import urlopen


def locate():
    """Called only when automatic IP location is explicitly enabled."""
    with urlopen("https://ipwho.is/", timeout=10) as response:
        data = json.load(response)
    if not data.get("success"):
        raise ValueError("Automatic location unavailable")
    return (data["city"].encode("ascii", "replace").decode()[:23],
            float(data["latitude"]), float(data["longitude"]), data["timezone"]["id"])


def make_app(city=None, latitude=None, longitude=None, zone=None, auto_location=False):
    cache: dict = {"until": 0.0, "data": None}
    lock = threading.Lock()
    location_until = 0.0

    def forecast():
        nonlocal city, latitude, longitude, zone, location_until
        with lock:
            if auto_location and time.monotonic() >= location_until:
                city, latitude, longitude, zone = locate()
                location_until = time.monotonic() + 3600
            if time.monotonic() >= cache["until"]:
                query = urlencode({
                    "latitude": latitude,
                    "longitude": longitude,
                    "current": "temperature_2m,weather_code",
                    "daily": "temperature_2m_max,temperature_2m_min",
                    "forecast_days": 1,
                    "timezone": zone,
                })
                with urlopen(f"https://api.open-meteo.com/v1/forecast?{query}", timeout=10) as response:
                    data = json.load(response)
                cache["data"] = data
                cache["until"] = time.monotonic() + 600
            return cache["data"]

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path != "/v1/today":
                self.send_error(404)
                return
            try:
                try:
                    data = forecast() if city is not None or auto_location else None
                except (OSError, ValueError, KeyError, TypeError):
                    data = None
                if data is None:
                    local = datetime.now().astimezone()
                else:
                    local = datetime.now(timezone.utc) + timedelta(seconds=int(data["utc_offset_seconds"]))
                values = {
                    "TIME": local.strftime("%H:%M"),
                    "DATE": local.strftime("%a %d %b").upper(),
                    "CITY": (city or "LOCATING").upper(),
                }
                if data is not None:
                    values.update({
                    "TEMP": round(data["current"]["temperature_2m"]),
                    "HIGH": round(data["daily"]["temperature_2m_max"][0]),
                    "LOW": round(data["daily"]["temperature_2m_min"][0]),
                    "CODE": int(data["current"]["weather_code"]),
                    })
                payload = "".join(f"{key}={value}\n" for key, value in values.items()).encode("ascii")
            except (OSError, ValueError, KeyError, TypeError, UnicodeEncodeError) as exc:
                self.send_error(503, f"Weather unavailable: {exc}")
                return
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=ascii")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--city", default=None)
    parser.add_argument("--auto-location", action="store_true")
    parser.add_argument("--latitude", type=float, default=None)
    parser.add_argument("--longitude", type=float, default=None)
    parser.add_argument("--timezone", default=None)
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if args.city and (args.latitude is None or args.longitude is None or not (-90 <= args.latitude <= 90 and -180 <= args.longitude <= 180)):
        parser.error("latitude or longitude is out of range")
    if args.city and (len(args.city) > 23 or not args.city.isascii()):
        parser.error("city must be ASCII and at most 23 characters")
    server = ThreadingHTTPServer(("0.0.0.0", args.port), make_app(
        args.city, args.latitude, args.longitude, args.timezone, auto_location=args.auto_location))
    print(f"Milo bridge listening on 0.0.0.0:{args.port} for {args.city}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
