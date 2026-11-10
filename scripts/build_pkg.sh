#!/bin/bash
set -euo pipefail

APPID="MUSC00001"
TITLE="PS3 Music"
CONTENT_ID="UP0001-${APPID}_00-0000000000000001"

make clean >/dev/null 2>&1 || true
make

rm -rf pkg_root output
mkdir -p pkg_root/USRDIR output

cp ps3music.self pkg_root/USRDIR/EBOOT.BIN

if [ -f assets/ICON0.PNG ]; then
  cp assets/ICON0.PNG pkg_root/ICON0.PNG
else
  # Gera um ícone 144x144 sólido se você não tiver um
  python3 - <<'EOF'
import zlib, struct
w = h = 144
raw = b''.join(b'\x00' + b'\x20\x60\xc0' * w for _ in range(h))
def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = (b'\x89PNG\r\n\x1a\n'
       + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(raw))
       + chunk(b'IEND', b''))
open('pkg_root/ICON0.PNG', 'wb').write(png)
EOF
fi

sfo.py --title "$TITLE" --appid "$APPID" pkg_root/PARAM.SFO
pkg.py --contentid "$CONTENT_ID" pkg_root output/ps3music.pkg

ls -la output