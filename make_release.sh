#!/bin/bash
# リリース用のファームをビルドして releases/ に zip を作る (SPEC.md 1 章「リリース」)。
# 環境変数: IDF_ROOT (ESP-IDF 5.5 のディレクトリ)、SHAPOGFX_PATH (ShapoGFX のクローン)、
#           PICO_SDK_PATH (Pico SDK 2.2、既定 ~/pico/pico-sdk)

set -eu
cd "$(dirname "$0")"

VERSION=$(cat version.txt)
echo "Releasing version ${VERSION}"
release_name="tiny-kamiokande-client_v${VERSION}"
dest_dir=$(pwd)"/releases/${release_name}"

IDF_ROOT="${IDF_ROOT:-${HOME}/esp/5.5/esp-idf}"
export SHAPOGFX_PATH="${SHAPOGFX_PATH:-${HOME}/shapo-gfx}"

# Tab5 は C6 の更新用ファームを埋め込む (SPEC.md 5.6)。無ければ先に作る
cp_image="impl/m5tab5/cp_firmware/tab5_c6_hosted.bin"
if [ ! -f "${cp_image}" ]; then
  echo "Building ESP32-C6 coprocessor firmware"
  # build_cp.sh は esp_hosted の取得のために Tab5 を一度ビルドしてあることが前提
  ( cd impl/m5tab5 && source "${IDF_ROOT}/export.sh" > /dev/null && idf.py reconfigure ) > tmp.release-cp-prepare.log 2>&1
  impl/m5tab5/cp_firmware/build_cp.sh > tmp.release-cp.log 2>&1
fi

rm -f "releases/${release_name}.zip"
rm -rf "${dest_dir}"
mkdir -p "${dest_dir}"

# 手元の build/ や sdkconfig (デモモードや menuconfig の変更) の影響を受けないよう、
# build-release/ に sdkconfig.defaults だけからクリーンビルドし、0x0 に書く 1 ファイルにまとめる。
# <board>/ に バイナリと assets/release/<board>/ の書き込み用スクリプトを置く
function build_esp {
  local board=$1
  local board_dir="${dest_dir}/${board}"
  local out="${board_dir}/tiny_kamiokande_${board}.bin"
  echo "Building ${board}"
  mkdir -p "${board_dir}"
  cp -p assets/release/${board}/* "${board_dir}/"
  pushd "impl/${board}" > /dev/null
    rm -rf build-release
    (
      source "${IDF_ROOT}/export.sh" > /dev/null
      idf.py -B build-release -DSDKCONFIG=build-release/sdkconfig merge-bin -o "${out}"
    ) > "build-release.log" 2>&1 || { echo "Build failed: impl/${board}/build-release.log" >&2; exit 1; }
  popd > /dev/null
}

# Pico SDK 版: build-release/ にクリーンビルドし (TKAPP_DEMO などの手元の指定は使わない)、.uf2 を置く
function build_pico {
  local board=$1
  local board_dir="${dest_dir}/${board}"
  echo "Building ${board}"
  mkdir -p "${board_dir}"
  cp -p assets/release/${board}/* "${board_dir}/"
  pushd "impl/${board}" > /dev/null
    rm -rf build-release
    BUILD_DIR=build-release ./build.sh > "build-release.log" 2>&1 \
      || { echo "Build failed: impl/${board}/build-release.log" >&2; exit 1; }
    cp build-release/tiny_kamiokande.uf2 "${board_dir}/tiny_kamiokande_${board}.uf2"
  popd > /dev/null
}

build_esp m5cores3
build_esp m5tab5
build_pico pico_restouch_lcd_2_8

{
  echo "version: ${VERSION}"
  echo "tiny-kamiokande-client: $(git rev-parse --short HEAD)$(git diff --quiet HEAD || echo ' (dirty)')"
  echo "ShapoGFX: $(git -C "${SHAPOGFX_PATH}" rev-parse --short HEAD 2> /dev/null || echo unknown)"
  echo "Pico SDK: $(git -C "${PICO_SDK_PATH:-${HOME}/pico/pico-sdk}" describe --tags 2> /dev/null || echo unknown)"
  echo
  echo "Flash: m5cores3/upload.sh [PORT] [BAUD]   # CoreS3"
  echo "       m5tab5/upload.sh [PORT] [BAUD]     # Tab5"
  echo "       pico_restouch_lcd_2_8/upload.sh        # Pico-ResTouch-LCD-2.8 + Pico 2 W (picotool,"
  echo "                                              # or copy the .uf2 to the RP2350 drive in BOOTSEL mode)"
} > "${dest_dir}/BUILD_INFO.txt"

cp README.md README.ja.md "${dest_dir}/"
cp -r image "${dest_dir}/"

pushd releases > /dev/null
  zip -r "${release_name}.zip" "${release_name}"
popd > /dev/null
