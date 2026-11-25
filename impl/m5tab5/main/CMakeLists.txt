# cp_firmware/tab5_c6_hosted.bin (ESP32-C6 用の esp_hosted コプロセッサのファーム) があれば埋め込み、
# シリアルの 'c6ota' コマンドで C6 に書き込めるようにする。作り方は SPEC.md 5.6
set(CP_IMAGE ${CMAKE_CURRENT_LIST_DIR}/../cp_firmware/tab5_c6_hosted.bin)
if(EXISTS ${CP_IMAGE})
    idf_component_register(SRCS "app_main.cpp" "cp_ota.c" REQUIRES m5platform tk_runner esp_hosted esp_timer
                           EMBED_FILES ${CP_IMAGE})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TKAPP_CP_OTA=1)
else()
    idf_component_register(SRCS "app_main.cpp" REQUIRES m5platform)
endif()
# idf.py -DTKAPP_DEMO=1 build で通信せず埋め込みのサンプルを表示する
if(DEFINED TKAPP_DEMO)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TKAPP_DEMO=${TKAPP_DEMO})
endif()
