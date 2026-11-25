#!/bin/bash
# Tab5 の ESP32-C6 用に esp_hosted のコプロセッサのファームを作り、tab5_c6_hosted.bin に置く (SPEC.md 5.6)。
# 先に impl/m5tab5 を一度ビルドして managed_components/espressif__esp_hosted を取得しておくこと。
# 作業ディレクトリは第 1 引数 (既定: /tmp/tk_c6cp)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
EH="$HERE/../managed_components/espressif__esp_hosted"
WORK="${1:-/tmp/tk_c6cp}"
IDF_ROOT="${IDF_ROOT:-${HOME}/esp/5.5/esp-idf}"
[ -d "$EH" ] || { echo "esp_hosted not fetched yet: build impl/m5tab5 first" >&2; exit 1; }
rm -rf "$WORK"
cp -r "$EH/examples/ota/coprocessor_ota/cp" "$WORK"
cat > "$WORK/main/idf_component.yml" <<YML
dependencies:
  espressif/esp_hosted:
    version: '*'
    override_path: '$EH'
  idf:
    version: '>=5.5'
YML
cat >> "$WORK/sdkconfig.defaults" <<'CFG'

# M5Stack Tab5 の ESP32-C6 (SDIO、ピンはボードプリセット)
CONFIG_ESP32P4_TAB5_C6_BOARD=y
# SW_AGGR は ESP-IDF へのパッチが要るので、パッチ不要のストリーミングモードにする (ホスト 3.1 は両方に対応)
CONFIG_EH_TRANSPORT_CP_SDIO_MODE_STREAM=y
CFG
source "$IDF_ROOT/export.sh" >/dev/null
cd "$WORK"
idf.py set-target esp32c6 build
cp build/eh_cp_ota_coprocessor_ota.bin "$HERE/tab5_c6_hosted.bin"
ls -l "$HERE/tab5_c6_hosted.bin"
