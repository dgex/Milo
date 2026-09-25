import io
import json
import sys
import threading
import unittest
from http.server import ThreadingHTTPServer
from pathlib import Path
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import urlopen

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bridge"))
import milo_bridge


class BridgeTests(unittest.TestCase):
    def test_serves_bounded_clock_and_today_forecast(self):
        sample = {
            "utc_offset_seconds": 7200,
            "current": {"temperature_2m": 18.4, "weather_code": 3},
            "daily": {"temperature_2m_max": [22.1], "temperature_2m_min": [12.2]},
        }

        def fake_open(url, timeout):
            self.assertIn("api.open-meteo.com/v1/forecast", url)
            self.assertEqual(timeout, 10)
            return io.BytesIO(json.dumps(sample).encode())

        with patch.object(milo_bridge, "urlopen", side_effect=fake_open):
            server = ThreadingHTTPServer(("127.0.0.1", 0), milo_bridge.make_app(
                "Berlin", 52.52, 13.41, "Europe/Berlin"))
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                base = f"http://127.0.0.1:{server.server_port}"
                with urlopen(base + "/v1/today") as response:
                    body = response.read().decode("ascii")
                self.assertIn("CITY=BERLIN\n", body)
                self.assertIn("TEMP=18\nHIGH=22\nLOW=12\nCODE=3\n", body)
                self.assertRegex(body, r"TIME=\d\d:\d\d\nDATE=[A-Z]{3} \d\d [A-Z]{3}\n")
                with self.assertRaises(HTTPError) as error:
                    urlopen(base + "/missing")
                self.assertEqual(error.exception.code, 404)
            finally:
                server.shutdown()
                server.server_close()
                worker.join(timeout=2)

    def test_location_response(self):
        with patch.object(milo_bridge, "urlopen", return_value=io.BytesIO(json.dumps({
            "success": True, "city": "Test City", "latitude": 1.25,
            "longitude": 2.5, "timezone": {"id": "UTC"}
        }).encode())) as request:
            self.assertEqual(milo_bridge.locate(), ("Test City", 1.25, 2.5, "UTC"))
            request.assert_called_once_with("https://ipwho.is/", timeout=10)

    def test_clock_survives_weather_outage(self):
        with patch.object(milo_bridge, "urlopen", side_effect=OSError("offline")):
            server = ThreadingHTTPServer(("127.0.0.1", 0), milo_bridge.make_app(
                "Berlin", 52.52, 13.41, "Europe/Berlin"))
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                with urlopen(f"http://127.0.0.1:{server.server_port}/v1/today") as response:
                    body = response.read().decode()
                self.assertIn("TIME=", body)
                self.assertNotIn("TEMP=", body)
            finally:
                server.shutdown()
                server.server_close()
                worker.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
