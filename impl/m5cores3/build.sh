#!/bin/bash
# IDF_ROOT は ESP-IDF 5.5 のディレクトリ、SHAPOGFX_PATH は ShapoGFX のクローン
# 使い方: ./build.sh [idf.py の追加引数 (例: -DTKAPP_DEMO=1)]
set -eux
IDF_ROOT="${IDF_ROOT:-${HOME}/esp/5.5/esp-idf}"
export SHAPOGFX_PATH="${SHAPOGFX_PATH:-${HOME}/shapo-gfx}"
cd "$(dirname "$0")"
source "${IDF_ROOT}/export.sh" >/dev/null
idf.py "$@" build
