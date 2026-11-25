# ESP-IDF 版の共通部分: フレームループ (2 コアで描画)、データ取得のパイプライン、WiFi、NVS、シリアルの設定。
# ESP32-P4 (Tab5) の WiFi は esp_wifi_remote + esp_hosted (プロジェクトの idf_component.yml) 経由。
# それらを外したビルドでは idf.py -DTKAPP_WIFI=0 build で通信なし (デモとシリアル設定だけ) にできる。
# コンポーネントの依存 (REQUIRES) はプロジェクトの変数が見えない段階で評価されるので常に付けておく
if(NOT DEFINED TKAPP_WIFI)
    set(TKAPP_WIFI 1)
endif()

idf_component_register(
    SRCS "runner.cpp" "pipeline.cpp" "net.cpp" "serial.cpp" "nvs_store.cpp"
    INCLUDE_DIRS "."
    REQUIRES tkapp_core esp_timer nvs_flash esp_driver_usb_serial_jtag esp_driver_uart vfs
             esp_wifi esp_netif esp_event esp_http_client esp-tls mbedtls
    # デモ用の埋め込みデータ (api.php?fmt=bin の応答)
    EMBED_FILES "../../samples/s1.cbor" "../../samples/s2.cbor" "../../samples/s3.cbor"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
target_compile_definitions(${COMPONENT_LIB} PRIVATE TKAPP_WIFI=${TKAPP_WIFI})
# idf.py -DTKAPP_DUAL_CORE=0 build で描画を core0 だけで行う (比較用)
if(DEFINED TKAPP_DUAL_CORE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TKAPP_DUAL_CORE=${TKAPP_DUAL_CORE})
endif()
