#!/usr/bin/env bash
# Tiny Kamiokande クライアントを M5Stack Tab5 (ESP32-P4) に書き込む。
#
#   ./upload.sh [PORT] [BAUD]
#
# PORT は省略すると $PORT または /dev/ttyACM0 (Windows では COM3 など)、
# BAUD は省略すると $BAUD または 921600。esptool が必要 (pip install esptool)。
#
# イメージはブートローダ、パーティションテーブル、アプリを結合したもので、0x0 に一度に書く。
# ポートが現れないときは、BOOT ボタン (USB-C の横) を押したまま RESET を押して離し、BOOT を離す。
#
# 出荷時の ESP32-C6 のファームのままでは HTTPS が通らない。書き込んだ後、USB シリアルのコンソールで
# c6ota コマンドを実行して C6 を更新する (このファームに埋め込み済み。約 11 秒)。

set -eu

PORT="${1:-${PORT:-/dev/ttyACM0}}"
BAUD="${2:-${BAUD:-921600}}"
BIN="$(cd "$(dirname "$0")" && pwd)/tiny_kamiokande_m5tab5.bin"

[ -f "$BIN" ] || { echo "firmware not found: $BIN" >&2; exit 1; }

# esptool は esptool、esptool.py、python のモジュールのいずれかで入っている
if command -v esptool >/dev/null 2>&1; then
  ESPTOOL="esptool"
elif command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL="esptool.py"
elif python3 -m esptool version >/dev/null 2>&1; then
  ESPTOOL="python3 -m esptool"
else
  echo "esptool not found. Install it with: pip install esptool" >&2
  exit 1
fi

# esptool 5 でサブコマンド名が変わった (write_flash -> write-flash)
MAJOR="$($ESPTOOL version 2>/dev/null | grep -oE '[0-9]+' | head -1 || true)"
if [ "${MAJOR:-4}" -ge 5 ]; then WRITE=write-flash; else WRITE=write_flash; fi

set -x
$ESPTOOL --chip esp32p4 --port "$PORT" --baud "$BAUD" "$WRITE" 0x0 "$BIN"
