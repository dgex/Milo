"""Keep Milo's clock and weather synchronized over its USB serial connection."""
import argparse
import threading
import time
from http.server import ThreadingHTTPServer
from urllib.request import urlopen
import serial
from milo_bridge import make_app

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM9')
parser.add_argument('--city', default=None)
parser.add_argument('--latitude', type=float, default=None)
parser.add_argument('--longitude', type=float, default=None)
parser.add_argument("--auto-location", action="store_true", help="Allow ipwho.is public-IP location lookup")
args = parser.parse_args()
server = ThreadingHTTPServer(('127.0.0.1', 0), make_app(args.city, args.latitude, args.longitude, None, auto_location=args.auto_location))
threading.Thread(target=server.serve_forever, daemon=True).start()
while True:
    try:
        with urlopen(f'http://127.0.0.1:{server.server_port}/v1/today', timeout=15) as response:
            payload = response.read().decode('ascii').strip().replace('\n', '|')
        with serial.Serial(args.port, 115200, timeout=1, write_timeout=3) as board:
            board.write(('data ' + payload + '\r').encode('ascii'))
            board.flush()
            deadline = time.monotonic() + 5
            reply = b''
            while time.monotonic() < deadline:
                reply += board.read(256)
                if b'Data updated.' in reply:
                    break
            print(time.strftime('%H:%M:%S'), ('Board acknowledged clock/weather' if 'TEMP=' in payload else 'Board acknowledged clock (weather unavailable)') if b'Data updated.' in reply else 'No board acknowledgement', flush=True)
    except Exception as exc:
        print(type(exc).__name__, str(exc), flush=True)
    time.sleep(15)
