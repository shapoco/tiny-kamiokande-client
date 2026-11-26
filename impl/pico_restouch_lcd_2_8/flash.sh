#!/bin/bash
# picotool で書き込む。BOOTSEL を押しながら接続するか、動作中なら picotool がリブートさせる (-f)
set -e
cd "$(dirname "$0")"
FILE=${1:-build/tiny_kamiokande.uf2}
PICOTOOL=${PICOTOOL:-$(command -v picotool || ls -d "$HOME"/.pico-sdk/picotool/*/picotool/picotool 2>/dev/null | sort | tail -1)}
"$PICOTOOL" load -f -x "$FILE"
