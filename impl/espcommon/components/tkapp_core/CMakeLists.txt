# アプリ本体 (impl/appcore) の ESP-IDF コンポーネント
set(TKAPP_APPCORE ${CMAKE_CURRENT_LIST_DIR}/../../../appcore)

# ShapoGFX のコンポーネント名はクローンしたディレクトリ名
get_filename_component(SHAPOGFX_COMPONENT "$ENV{SHAPOGFX_PATH}" ABSOLUTE)
get_filename_component(SHAPOGFX_COMPONENT "${SHAPOGFX_COMPONENT}" NAME)

idf_component_register(
    SRCS
        "${TKAPP_APPCORE}/src/app.cpp"
        "${TKAPP_APPCORE}/src/anim.cpp"
        "${TKAPP_APPCORE}/src/scene.cpp"
        "${TKAPP_APPCORE}/src/ui.cpp"
        "${TKAPP_APPCORE}/src/detector.cpp"
        "${TKAPP_APPCORE}/src/cbor.cpp"
        "${TKAPP_APPCORE}/src/eventlog.cpp"
    INCLUDE_DIRS "${TKAPP_APPCORE}/include"
    REQUIRES ${SHAPOGFX_COMPONENT}
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
# 履歴機能は MCU では使わない (画面をシンプルにする)。idf.py -DTKAPP_HISTORY=1 build で有効
if(NOT DEFINED TKAPP_HISTORY)
    set(TKAPP_HISTORY 0)
endif()
target_compile_definitions(${COMPONENT_LIB} PUBLIC TKAPP_HISTORY=${TKAPP_HISTORY})
