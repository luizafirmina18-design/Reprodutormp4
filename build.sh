#!/bin/bash
set -e

APPID="MUSC00001"
TITLE="PS3 Music"

make

rm -rf pkg output
mkdir -p pkg/USRDIR output

cp ps3music.self pkg/USRDIR/EBOOT.BIN
cp assets/ICON0.PNG pkg/ICON0.PNG

sfo.py --title "$TITLE" --appid "$APPID" pkg/PARAM.SFO
pkg.py --contentid "UP0001-${APPID}_00-0000000000000001" pkg output/ps3music.pkg

ls -la output