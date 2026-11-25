#!/bin/bash
# 第 1 引数でポートを指定できる (省略時は自動)。残りは idf.py の追加引数
set -eux
IDF_ROOT="${IDF_ROOT:-${HOME}/esp/5.5/esp-idf}"
export SHAPOGFX_PATH="${SHAPOGFX_PATH:-${HOME}/shapo-gfx}"
cd "$(dirname "$0")"
source "${IDF_ROOT}/export.sh" >/dev/null
PORT="${1:-}"
shift || true
idf.py "$@" build
if [ -n "$PORT" ]; then idf.py -p "$PORT" flash; else idf.py flash; fi
