"""Generate firmware web assets and a per-build setup password (no user credentials)."""
import json
import base64
import pathlib
import secrets
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).parent / 'vendor'))
from qrcodegen import QrCode

source, output = map(pathlib.Path, sys.argv[1:3])
qr_source = pathlib.Path(sys.argv[4])
output.mkdir(parents=True, exist_ok=True)
key_file = output / 'portal_keys.json'
if not key_file.exists():
    key_file.write_text(json.dumps({'password': secrets.token_hex(8), 'token': secrets.token_hex(16)}), encoding='ascii')
keys = json.loads(key_file.read_text(encoding='ascii'))
page = source.read_text(encoding='utf-8').replace('@@TOKEN@@', keys['token'])
logo = base64.b64encode(pathlib.Path(sys.argv[3]).read_bytes()).decode('ascii')
page = page.replace('@@LOGO@@', logo)
(output / 'index.html').write_text(page, encoding='utf-8')
literal = '\n'.join(json.dumps(line + '\n') for line in page.splitlines())
qr_literal = '\n'.join(json.dumps(line + '\n') for line in qr_source.read_text(encoding='utf-8').splitlines())
header = '#ifndef PORTAL_ASSETS_H\n#define PORTAL_ASSETS_H\n'
header += '#define PORTAL_AP_PASSWORD ' + json.dumps(keys['password']) + '\n'
header += '#define PORTAL_TOKEN ' + json.dumps(keys['token']) + '\n'
header += 'static const unsigned char portal_html[] =\n' + literal + ';\n'
header += 'static const unsigned char portal_qr_js[] =\n' + qr_literal + ';\n#endif\n'
(output / 'portal_assets.h').write_text(header, encoding='ascii')

# The display and the phone-facing image use the exact password compiled above.
wifi_payload = f"WIFI:T:WPA;S:Milo-Setup;P:{keys['password']};;"
qr = QrCode.encode_text(wifi_payload, QrCode.Ecc.MEDIUM)
size = qr.get_size()
if size + 8 > 44:
    raise ValueError(f'Setup QR ({size} modules) exceeds the display panel')
rows = []
for y in range(size):
    row = [sum((1 << (7 - bit)) if x + bit < size and qr.get_module(x + bit, y) else 0
               for bit in range(8)) for x in range(0, size, 8)]
    rows.append('    {' + ', '.join(f'0x{byte:02x}' for byte in row) + '}')
qr_header = ('#ifndef SETUP_WIFI_QR_H\n#define SETUP_WIFI_QR_H\n'
             f'#define SETUP_QR_SIZE {size}\n'
             f'static const unsigned char setup_qr_rows[{size}][{(size + 7) // 8}] = {{\n'
             + ',\n'.join(rows) + '\n};\n#endif\n')
(output / 'setup_wifi_qr.h').write_text(qr_header, encoding='ascii')

module_pixels = 10
image_size = (size + 8) * module_pixels
scanlines = bytearray()
for py in range(image_size):
    scanlines.append(0)
    y = py // module_pixels - 4
    for px in range(image_size):
        x = px // module_pixels - 4
        dark = 0 <= x < size and 0 <= y < size and qr.get_module(x, y)
        scanlines.append(0 if dark else 255)
def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
png = (b'\x89PNG\r\n\x1a\n'
       + chunk(b'IHDR', struct.pack('>IIBBBBB', image_size, image_size, 8, 0, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(scanlines))
       + chunk(b'IEND', b''))
(output / 'setup-wifi-qr.png').write_bytes(png)

