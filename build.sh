#!/bin/bash
set -e
echo "=== Arquivos ==="
ls -la

# Cria PARAM.SFO simples
if [ ! -f PARAM.SFO ]; then
  echo "Criando PARAM.SFO"
  printf '\x00PSF\x01\x01\x00\x00' > PARAM.SFO
fi

# Cria ICON0.PNG sem precisar de PIL - é um PNG vermelho 1x1
if [ ! -f ICON0.PNG ]; then
  echo "Criando ICON0.PNG"
  echo "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==" | base64 -d > ICON0.PNG
fi

echo "Compilando..."
make
ls -lh EBOOT.BIN

echo "Criando PKG..."
make_package_npdrm --content-id UP0001-YOUP00001_00-YOPLAYER00000001 --package YoPlayer.pkg PARAM.SFO EBOOT.BIN ICON0.PNG videos.txt
ls -lh YoPlayer.pkg