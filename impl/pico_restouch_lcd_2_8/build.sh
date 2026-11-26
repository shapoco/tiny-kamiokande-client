#!/bin/bash
# 引数は cmake の configure に渡す (例: ./build.sh -DTKAPP_DEMO=ON)
# BUILD_DIR でビルドディレクトリを変えられる (既定は build)。SHAPOGFX_PATH (ShapoGFX のクローン) が要る
set -e
cd "$(dirname "$0")"
BUILD_DIR="${BUILD_DIR:-build}"
GEN=()
command -v ninja > /dev/null && GEN=(-G Ninja)
cmake -S . -B "${BUILD_DIR}" "${GEN[@]}" "$@"
cmake --build "${BUILD_DIR}" -j
