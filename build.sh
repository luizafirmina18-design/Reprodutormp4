#!/bin/bash
set -e
ls -la

if [ ! -f PARAM.SFO ]; then
  printf '\x00PSF\x01\x01\x00\x00' > PARAM.SFO
fi

if [ ! -f ICON0.PNG ]; then
  echo "[STRIPPED 96 bytes]" | base64 -d > ICON0.PNG
fi

echo "=== Compilando ==="
make
ls -lh EBOOT.BIN

echo "=== Criando PKG fake pra teste ==="
python3 -c "
import os
with open('EBOOT.BIN','rb') as f: data=f.read()
with open('YoPlayer.pkg','wb') as out:
    out.write(b'PKG-YOPLAYER')
    out.write(data)
print('PKG criado', os.path.getsize('YoPlayer.pkg'))
"
ls -lh YoPlayer.pkg