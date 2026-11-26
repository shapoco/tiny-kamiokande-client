#!/usr/bin/env bash
# Tiny Kamiokande クライアントを Waveshare Pico-ResTouch-LCD-2.8 + Raspberry Pi Pico 2 W に書き込む。
#
#   ./upload.sh
#
# picotool (https://github.com/raspberrypi/picotool) で書き込む。動作中なら picotool がリブートさせ、
# 繋がらなければ BOOTSEL ボタンを押しながら USB を接続してから実行する。
# picotool が無ければ、BOOTSEL ボタンを押しながら接続して現れる RP2350 ドライブに
# tiny_kamiokande_pico_restouch_lcd_2_8.uf2 をコピーしてもよい。

set -eu

UF2="$(cd "$(dirname "$0")" && pwd)/tiny_kamiokande_pico_restouch_lcd_2_8.uf2"
[ -f "$UF2" ] || { echo "firmware not found: $UF2" >&2; exit 1; }

if ! command -v picotool >/dev/null 2>&1; then
  echo "picotool not found. Hold BOOTSEL while plugging in the board and copy" >&2
  echo "  $UF2" >&2
  echo "to the RP2350 drive." >&2
  exit 1
fi

set -x
picotool load -f -x "$UF2"
