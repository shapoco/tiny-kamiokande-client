#!/bin/bash
# 第 1 引数でポートを指定できる
set -eu
IDF_ROOT="${IDF_ROOT:-${HOME}/esp/5.5/esp-idf}"
cd "$(dirname "$0")"
source "${IDF_ROOT}/export.sh" >/dev/null
if [ $# -ge 1 ]; then idf.py -p "$1" monitor; else idf.py monitor; fi
