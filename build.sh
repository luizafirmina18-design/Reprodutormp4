#!/bin/bash
set -e

APPID="MUSC00001"
TITLE="PS3 Music"

make

rm -rf pkg output
mkdir -p pkg/USRDIR output

cp ps3music.self pkg/USRDIR/EBOOT.BIN

# Cria um ícone 144x144 sólido, já que o projeto não tem um
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
open('pkg/ICON0.PNG', 'wb').write(png)
EOF

sfo.py --title "$TITLE" --appid "$APPID" pkg/PARAM.SFO
pkg.py --contentid "UP0001-${APPID}_00-0000000000000001" pkg output/ps3music.pkg

ls -la output