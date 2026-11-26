#!/bin/bash
# SWD (Raspberry Pi Debug Probe などの CMSIS-DAP) で書き込んでリセットする
set -e
cd "$(dirname "$0")"
FILE=${1:-build/tiny_kamiokande.elf}
OPENOCD=${OPENOCD:-openocd}
"$OPENOCD" -f interface/cmsis-dap.cfg -f target/rp2350.cfg -c "adapter speed 5000" \
    -c "program $FILE verify reset exit"
